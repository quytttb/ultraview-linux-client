#!/usr/bin/env python3
"""Generate byte-level compatibility fixtures from the Python reference.

The fixtures are the compatibility contract between the reference implementation
(`reference/`) and the native C++ client.  Every value in the output is produced
by running the *reference* code, never by the C++ implementation, so the C++
tests are true parity tests.

Usage:
    python3 tools/generate_fixtures.py --reference reference --out tests/data/fixtures.json

All inputs are synthetic: no real partner IDs, passwords, machine fingerprints
or captured traffic are used.  The test dictionary is trained from locally
generated synthetic samples and is unrelated to the vendor dictionary `8bit.dat`
(which is deliberately *not* redistributed with this project).
"""
from __future__ import annotations

import argparse
import base64
import ctypes
import ctypes.util
import hashlib
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# ---------------------------------------------------------------------------
# import the reference implementation under test
# ---------------------------------------------------------------------------


def import_reference(reference_dir: Path):
    sys.path.insert(0, str(reference_dir))
    import uv_reference
    import input_control
    import screen_decode
    return uv_reference, input_control, screen_decode


# ---------------------------------------------------------------------------
# zstd helpers (ctypes, so the generator does not need the `zstandard` wheel)
# ---------------------------------------------------------------------------

ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"


class ZstdCompressor:
    def __init__(self):
        name = ctypes.util.find_library("zstd")
        if not name:
            raise RuntimeError("libzstd is required")
        self.lib = ctypes.CDLL(name)
        lib = self.lib
        lib.ZSTD_createCCtx.restype = ctypes.c_void_p
        lib.ZSTD_freeCCtx.argtypes = [ctypes.c_void_p]
        lib.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
        lib.ZSTD_compressBound.restype = ctypes.c_size_t
        lib.ZSTD_compress_usingDict.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
            ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        lib.ZSTD_compress_usingDict.restype = ctypes.c_size_t
        lib.ZSTD_isError.argtypes = [ctypes.c_size_t]
        lib.ZSTD_isError.restype = ctypes.c_uint

    def compress(self, payload: bytes, dictionary: bytes, level: int = 3) -> bytes:
        lib = self.lib
        bound = lib.ZSTD_compressBound(len(payload))
        out = ctypes.create_string_buffer(bound)
        ctx = lib.ZSTD_createCCtx()
        if not ctx:
            raise MemoryError("cannot create zstd compression context")
        try:
            count = lib.ZSTD_compress_usingDict(
                ctx, out, bound, payload, len(payload), dictionary, len(dictionary), level)
            if lib.ZSTD_isError(count):
                raise ValueError("zstd compression failed")
            return out.raw[:count]
        finally:
            lib.ZSTD_freeCCtx(ctx)


def train_dictionary(samples: list[bytes], out_path: Path) -> bytes:
    """Train a real (formatted) zstd dictionary from synthetic samples."""
    with tempfile.TemporaryDirectory() as tmp:
        sample_dir = Path(tmp) / "samples"
        sample_dir.mkdir()
        for index, data in enumerate(samples):
            (sample_dir / f"s{index:03d}.bin").write_bytes(data)
        subprocess.run(["zstd", "--train", *map(str, sorted(sample_dir.iterdir())),
                        "-o", str(out_path), "-f"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out_path.read_bytes()


# ---------------------------------------------------------------------------
# synthetic sample / screen helpers
# ---------------------------------------------------------------------------


def synthetic_samples(count: int = 64, size: int = 64) -> list[bytes]:
    """Deterministic screen-like RGB samples for dictionary training."""
    rng = 0x12345678
    samples = []
    for index in range(count):
        rows = bytearray()
        base = []
        for channel in range(3):
            rng = (1103515245 * rng + 12345) & 0x7FFFFFFF
            base.append((rng >> 16) % 256)
        for y in range(size):
            for x in range(size):
                rng = (1103515245 * rng + 12345) & 0x7FFFFFFF
                noise = (rng >> 20) % 7
                for channel in range(3):
                    value = base[channel] + ((x // 8 + y // 8) * 7) % 40 - 20 + noise
                    rows.append(min(255, max(0, value)))
        samples.append(bytes(rows))
    return samples


def rgb888_to_rgb555_stream(pixels: bytes, width: int, height: int) -> bytes:
    """Top-down RGB555 little-endian stream, i.e. the observed reference layout.

    `pixels` is packed RGB888 top-down (`Image.tobytes()` order); rows are stored
    in that same order.  The reference reads the stream with
    `Image.frombytes(..., 'raw', 'BGR;15', 0, 1)`, and Pillow only reverses rows
    for orientation `-1`, so the observed decode is top-down: row 0 of the stream
    becomes row 0 of the canvas.  Whether a real Windows partner sends rows
    bottom-up is *not* verified here (see docs/compatibility.md).
    """
    out = bytearray()
    for y in range(height):
        row = pixels[y * width * 3:(y + 1) * width * 3]
        for x in range(width):
            r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            value = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
            out += struct.pack("<H", value)
    return bytes(out)


def crop_bytes(pixels: bytes, stride_w: int, x: int, y: int, w: int, h: int) -> bytes:
    out = bytearray()
    for row in range(y, y + h):
        out += pixels[(row * stride_w + x) * 3:(row * stride_w + x + w) * 3]
    return bytes(out)


def make_png_free_expected(image) -> str:
    return base64.b64encode(image.tobytes()).decode("ascii")


# ---------------------------------------------------------------------------
# fixture builders
# ---------------------------------------------------------------------------


def build_fixtures(reference_dir: Path, out_path: Path, dict_out: Path) -> dict:
    uv, input_control, screen_decode = import_reference(reference_dir)
    zstd = ZstdCompressor()

    dictionary = train_dictionary(synthetic_samples(), dict_out)
    # A *formatted* zstd dictionary starts with the dictionary magic 0xEC30A437
    # (little-endian 37 A4 30 EC), unlike a frame which starts with ZSTD_MAGIC.
    if len(dictionary) < 256 or dictionary[:4] == ZSTD_MAGIC:
        raise SystemExit("trained dictionary is not a formatted zstd dictionary")

    fixtures: dict = {
        "meta": {
            "generator": "tools/generate_fixtures.py",
            "reference": "reference/ (caohoangphucs/ultraviewer-linux, revision recorded in reference/README.md)",
            "note": "All values are generated by running the Python reference code.",
            "dictionary": {
                "file": dict_out.name,
                "sha256": hashlib.sha256(dictionary).hexdigest(),
                "bytes": len(dictionary),
                "origin": "trained with `zstd --train` from synthetic samples generated by this script",
            },
        }
    }

    # --- VFunction key derivation ------------------------------------------
    fixtures["vfunction_key"] = [
        {"password": password, "length": length,
         "hex": uv.derive_vfunction_key_bytes(password, length).hex()}
        for password, length in [("", 8), ("", 24), ("abc", 8), ("abc", 24),
                                 ("p" * 64, 24), ("\u00e9\u4e2d", 24), ("abc", 32), ("abc", 0)]
    ]

    # --- registration command + identity ----------------------------------
    registration_terms = [
        "3", "11111111", "12345678", "0", "555012345678", "0", "DESKTOP-TEST",
        "10.0.0.5", "192.0.2.10", "", "7", "0", "", "", "0", "1440", "1080",
        "1", "0", "20240101120000", "0", "1234567890123456",
        "1700000000000:1700000000001:1700000000002", "", "", ""]
    fixtures["registration_command"] = {
        "terms": registration_terms,
        "ansi_codec": "cp1252",
        "hex": uv.build_registration_command(registration_terms, ansi_codec="cp1252").hex(),
    }
    fixtures["command3_payloads"] = [
        {"hex": payload.hex(), "ansi_codec": "cp1252",
         "client_id": uv.extract_command3_identity(payload, ansi_codec="cp1252").client_id,
         "public_source_port": uv.extract_command3_identity(payload, ansi_codec="cp1252").public_source_port,
         "public_ip": uv.extract_command3_identity(payload, ansi_codec="cp1252").public_ip}
        for payload in [b"3:45678:123456789:203.0.113.7:unused",
                        b"3:1:1:127.0.0.1",
                        b"3:65535:2147483647:198.51.100.9:extra:fields"]
    ]
    fixtures["invalid_command3_payloads"] = [
        payload.hex() for payload in
        [b"", b"3", b"3:45678", b"3:45678:abc:203.0.113.7", b"3:45678:2147483648:x",
         b"3:70000:123:x", b"3:45678:0:x", b"38:1:123:x", b"3:45678:-1:x",
         b"3:123456:1:x", b"3:45678:12345678901:x"]]

    # --- registration 3DES -------------------------------------------------
    registration_payloads = [b"3:a:b:c", b"", b"A" * 7, bytes(range(16))]
    fixtures["registration_cipher"] = [
        {"plaintext_hex": payload.hex(),
         "hex": uv.encrypt_registration_bytes(payload).hex(),
         "decrypted_hex": uv.decrypt_registration_bytes(
             uv.encrypt_registration_bytes(payload)).hex()}
        for payload in registration_payloads]
    fixtures["registration_password"] = {
        "chars": [ord(c) for c in uv.REGISTRATION_PASSWORD]}

    # --- fingerprint / version / referral ---------------------------------
    fixtures["fingerprint"] = [
        {"preimage_hex": preimage.hex(), "digest": uv.registration_fingerprint_digest(preimage)}
        for preimage in [b"", b"abc", b"00112233445517000000000001700000000000"]]
    fixtures["fallback_fingerprint"] = [{
        "mac_address": "001122334455", "program_files_ticks": "1700000000000",
        "system_directory_ticks": "1700000000001", "ansi_codec": "cp1252",
        "digest": uv.fallback_registration_fingerprint(
            "001122334455", "1700000000000", "1700000000001", ansi_codec="cp1252")}]
    fixtures["version_compare"] = [
        {"current": current, "minimum": minimum,
         "expected": uv.version_equal_or_greater(current, minimum)}
        for current, minimum in [("6.6.133", "6.6.0"), ("6.4.24", "6.4.24"),
                                 ("6.4.23", "6.4.24"), ("6.5.999", "6.6.0"),
                                 ("7.0.0", "6.999.999"), ("5.999.999", "6.0.0"),
                                 ("6.10.0", "6.9.999"), ("6.6.0.0", "6.6.0.999"),
                                 ("6.6.0.ignored", "6.6.0")]]
    fixtures["version_compare_false"] = [
        {"current": "6.6", "minimum": "6.6.0"}]
    fixtures["referral_int32"] = [
        {"hex": struct.pack("<i", value).hex() + "ffff", "value": value}
        for value in [0, 1, -1, 2147483647, -2147483648]]
    fixtures["os_version_combine"] = [
        {"major": 10, "minor": 0, "build": 19045, "revision": 1,
         "value": uv.format_os_version_combine(10, 0, 19045, 1)},
        {"major": 6, "minor": 3, "build": 9600, "revision": 0x00010002,
         "value": uv.format_os_version_combine(6, 3, 9600, 0x00010002)},
        {"major": 0, "minor": 0, "build": 0, "revision": 0,
         "value": uv.format_os_version_combine(0, 0, 0, 0)}]

    # --- password / login --------------------------------------------------
    fixtures["password_values"] = [
        {"password": password, "password_type": password_type,
         "value": uv.connection_request_password_value(password, password_type, ansi_codec="cp1252")}
        for password, password_type in
        [("test-password", 0), ("27296", 0), ("", 0), ("alt", 1), ("alt", -1),
         ("alt", 2147483647), ("\u00e9\u00e8", 0)]]
    fixtures["salt_digest"] = [
        {"password": password, "salt": salt,
         "digest": uv.login_numeric_salt_digest(password, salt, ansi_codec="cp1252")}
        for password, salt in [("test-password", "12345678"), ("27296", "00000000"),
                               ("", "99999999")]]
    login_metadata = [f"metadata{i}" for i in range(22)]
    login_fields = list(login_metadata)
    fixtures["login"] = {
        "metadata_fields": login_metadata,
        "cases": [{
            "password": "test-password", "password_type": 0, "salt": "12345678",
            "ansi_codec": "cp1252",
            "positions": [
                {"index": 0, "value": uv.login_password_fields("test-password", 0, salt="12345678", ansi_codec="cp1252").salt},
                {"index": 1, "value": uv.login_password_fields("test-password", 0, salt="12345678", ansi_codec="cp1252").primary},
                {"index": 12, "value": uv.login_password_fields("test-password", 0, salt="12345678", ansi_codec="cp1252").secondary}],
            "hex": uv.build_login_with_password(
                login_fields, password="test-password", password_type=0,
                salt="12345678", ansi_codec="cp1252").hex(),
        }, {
            "password": "alternate", "password_type": 1, "salt": None,
            "ansi_codec": "cp1252",
            "positions": [{"index": 0, "value": " " * 32},
                          {"index": 1, "value": "alternate"},
                          {"index": 12, "value": "alternate"}],
            "hex": uv.build_login_with_password(
                login_fields, password="alternate", password_type=1,
                salt=None, ansi_codec="cp1252").hex(),
        }],
    }

    # --- command8 connection request --------------------------------------
    request_terms = ["8", "90135737", "", "6.6.133", "", "", "", "", "-1", "0",
                     "0", "0", "", "420", "0", "", ""]
    fixtures["connection_request"] = {
        "template_terms": request_terms,
        "cases": [{
            "terms": [*request_terms[:2],
                      uv.connection_request_password_value("27296", 0, ansi_codec="cp1252"),
                      *request_terms[3:6], "123456789",
                      hashlib.md5(("123456789" + "12345678").encode("cp1252")).hexdigest(),
                      *request_terms[8:]],
            "ansi_codec": "cp1252",
            "password": "27296",
            "password_type": 0,
            "client_id": 123456789,
            "registration_term2": "12345678",
        }],
    }
    fixtures["connection_request"]["cases"][0]["hex"] = \
        uv.build_connection_request_command(
            fixtures["connection_request"]["cases"][0]["terms"],
            ansi_codec="cp1252").hex()
    fixtures["p2p_suffix"] = [
        {"enabled": True, "public_ip": "203.0.113.7", "public_source_port": "50000",
         "local_ip": "10.0.0.5", "local_port": "50001", "encryption_key": "KEY",
         "value": uv.connection_request_p2p_suffix(
             enabled=True, public_ip="203.0.113.7", public_source_port="50000",
             local_ip="10.0.0.5", local_port="50001", encryption_key="KEY")},
        {"enabled": False, "public_ip": "", "public_source_port": "", "local_ip": "",
         "local_port": "", "encryption_key": "", "value": ""}]

    # --- byte array fields -------------------------------------------------
    field_cases = [
        [b"\x00", b"A\x00B", None, b""],
        [b"", b"", b""],
        [b"\x01"],
        [bytes(range(256))],
    ]
    fixtures["byte_array_fields"] = [
        {"fields_hex": [None if f is None else f.hex() for f in fields],
         "wire_hex": uv.encode_byte_array_fields(fields).hex(),
         "decoded_hex": [f.hex() for f in uv.decode_byte_array_fields(
             uv.encode_byte_array_fields(fields), expected_count=len(fields))]}
        for fields in field_cases]

    # --- transport frames --------------------------------------------------
    fixtures["transport_frames"] = [
        {"body_hex": body.hex(), "wire_hex": uv.encode_transport_frame(body).hex()}
        for body in [b"", b"hi", b"\x013:123:0:abc", bytes(range(32))]]
    fixtures["typed_transport_frames"] = [
        {"kind": kind, "payload_hex": payload.hex(),
         "wire_hex": uv.encode_typed_transport_frame(payload, kind=kind).hex()}
        for kind, payload in [(0, b"a"), (1, b"\x01\x02"), (255, b"")]
    ]
    fixtures["relay_frames"] = [
        {"payload_hex": payload.hex(), "ipv4": ipv4, "port": port, "flag": flag,
         "wire_hex": uv.encode_relay_frame(payload, ipv4=ipv4, port=port, flag=flag).hex()}
        for payload, ipv4, port, flag in
        [(b"hi", "1.2.3.4", 2000, 1), (b"", "0.0.0.0", 0, 0),
         (b"\xff\x00", "255.255.255.255", 65535, 2),
         (b"login", "203.0.113.7", 443, 1)]]
    fixtures["transport_bodies"] = [
        {"body_hex": body.hex(), "kind": kind, "flag": flag,
         "address_value": address, "port": port, "data_hex": data.hex()}
        for body, (kind, flag, address, port, data) in [
            (b"\x00\x02\x78\x56\x34\x12\xd0\x07PAY", ("relay", True, 0x12345678, 2000, b"PAY")),
            (b"\x00\x00\x00\x00\x00\x00\x00\x00", ("relay", False, 0, 0, b"")),
            (b"\x01PAY", ("direct", False, None, None, b"PAY")),
            (b"\x01", ("direct", False, None, None, b"")),
            (b"\x00\x01\x05\x00\x00\x00\x00\x00Z", ("relay", True, 5, 0, b"Z"))]]
    fixtures["unknown_transport_bodies"] = [
        {"body_hex": b"\x7fPAY".hex()}, {"body_hex": b"\x02\x00".hex()}]
    fixtures["invalid_transport_bodies"] = [{"body_hex": b"\x00".hex()},
                                            {"body_hex": b"".hex()}]

    # --- frame stream ------------------------------------------------------
    transport_wire = uv.encode_transport_frame(b"\x013:123:0:abc")
    stream_cases = [
        {"chunks_hex": [transport_wire.hex()], "expect_http_header": False,
         "frames_hex": [b"\x013:123:0:abc".hex()], "remaining_hex": ""},
        {"chunks_hex": [transport_wire[:3].hex(), transport_wire[3:7].hex(),
                        transport_wire[7:].hex()], "expect_http_header": False,
         "frames_hex": [b"\x013:123:0:abc".hex()], "remaining_hex": ""},
        {"chunks_hex": [(b"HTTP/1.1 200 OK\r\nX: y\r\n\r").hex(), b"\n\x03\x00".hex(),
                        b"\x00\x00AB".hex(), b"C\x01\x00\x00\x00Z".hex()],
         "expect_http_header": True, "frames_hex": [b"ABC".hex(), b"Z".hex()],
         "remaining_hex": ""},
        {"chunks_hex": [bytes(4).hex(), b"X".hex()], "expect_http_header": False,
         "frames_hex": [b"".hex()], "remaining_hex": "58"},
        {"chunks_hex": [bytes(4).hex()], "expect_http_header": False,
         "frames_hex": [], "remaining_hex": "00000000"},
    ]
    for case in stream_cases:
        stream = uv.FrameStream(case["expect_http_header"])
        frames: list[bytes] = []
        for chunk_hex in case["chunks_hex"]:
            frames.extend(stream.feed(bytes.fromhex(chunk_hex)))
        assert [f.hex() for f in frames] == case["frames_hex"], case
        assert stream.buffer.hex() == case["remaining_hex"], (stream.buffer.hex(), case)
    fixtures["frame_stream"] = stream_cases
    fixtures["http_preface"] = [
        {"host": host, "post": post,
         "hex": uv.http_service_preface(host, post).hex()}
        for host, post in [("example.test", True), ("example.test", False),
                           ("functions3.ultraviewer.net", True)]]

    # --- p2p ---------------------------------------------------------------
    p2p_payload = "login:12345678".encode("utf-16le")
    fixtures["p2p"] = {
        "key_derivation": [
            {"password": password, "hex": uv.derive_p2p_aes_key(password).hex()}
            for password in ["fixture-P2P!", "abc12345"]],
        "encrypt": [{
            "password": "fixture-P2P!", "iv_hex": bytes(range(16)).hex(),
            "plaintext_hex": p2p_payload.hex(),
            "body_hex": uv.encrypt_p2p_aes_body(p2p_payload, "fixture-P2P!",
                                                iv=bytes(range(16))).hex()}],
        "frames": [{
            "password": "fixture-P2P!", "iv_hex": bytes(range(16)).hex(),
            "unicode_bytes": True, "payload_hex": p2p_payload.hex(),
            "wire_hex": uv.encode_p2p_frame(p2p_payload, unicode_bytes=True,
                                            encryption_password="fixture-P2P!",
                                            iv=bytes(range(16))).hex()}],
        "plain_frames": [{
            "payload_hex": b"\xff\x00\x80binary".hex(), "unicode_bytes": False,
            "wire_hex": uv.encode_p2p_frame(b"\xff\x00\x80binary", unicode_bytes=False,
                                            encryption_password=None).hex()}],
    }
    fixtures["p2p"]["decrypt_body"] = [
        {"body_hex": uv.encode_p2p_frame(p2p_payload, unicode_bytes=True,
                                         encryption_password="fixture-P2P!",
                                         iv=bytes(range(16)))[4:].hex(),
         "password": "fixture-P2P!", "payload_hex": p2p_payload.hex(), "flag": True}]

    # --- input commands ----------------------------------------------------
    fixtures["input_commands"] = [
        {"op": "move", "x": 250, "y": 200, "payload": input_control.mouse_move(250, 200).hex()},
        {"op": "move", "x": 0, "y": 0, "payload": input_control.mouse_move(0, 0).hex()},
        {"op": "move", "x": -1, "y": -1, "payload": input_control.mouse_move(-1, -1).hex()},
        {"op": "move", "x": 32767, "y": -32768, "payload": input_control.mouse_move(32767, -32768).hex()},
        {"op": "button", "button": "left", "down": True, "payload": input_control.mouse_button("left", True).hex()},
        {"op": "button", "button": "left", "down": False, "payload": input_control.mouse_button("left", False).hex()},
        {"op": "button", "button": "right", "down": True, "payload": input_control.mouse_button("right", True).hex()},
        {"op": "button", "button": "right", "down": False, "payload": input_control.mouse_button("right", False).hex()},
        {"op": "button", "button": "middle", "down": True, "payload": input_control.mouse_button("middle", True).hex()},
        {"op": "button", "button": "middle", "down": False, "payload": input_control.mouse_button("middle", False).hex()},
        {"op": "wheel", "delta": 120, "payload": input_control.mouse_wheel(120).hex()},
        {"op": "wheel", "delta": -120, "payload": input_control.mouse_wheel(-120).hex()},
        {"op": "wheel", "delta": 0, "payload": input_control.mouse_wheel(0).hex()},
        {"op": "wheel", "delta": -32768, "payload": input_control.mouse_wheel(-32768).hex()},
        {"op": "key", "vk": 85, "down": True, "system": False, "payload": input_control.keyboard(85, True).hex()},
        {"op": "key", "vk": 85, "down": False, "system": False, "payload": input_control.keyboard(85, False).hex()},
        {"op": "key", "vk": 91, "down": True, "system": False, "payload": input_control.keyboard(91, True).hex()},
        {"op": "key", "vk": 91, "down": False, "system": False, "payload": input_control.keyboard(91, False).hex()},
        {"op": "key", "vk": 13, "down": True, "system": True, "payload": input_control.keyboard(13, True, system=True).hex()},
        {"op": "key", "vk": 13, "down": False, "system": True, "payload": input_control.keyboard(13, False, system=True).hex()},
        {"op": "key", "vk": 0, "down": True, "system": False, "payload": input_control.keyboard(0, True).hex()},
        {"op": "key", "vk": 255, "down": True, "system": False, "payload": input_control.keyboard(255, True).hex()},
    ]

    # --- route -------------------------------------------------------------
    route_payload = ("8:1:10.0.0.7:0:203.0.113.7:50000:50001:49999:DESKTOP-ABC:"
                     "4433:0:relay.example.test:1:7654321:relay.example.test:"
                     "0:0:0:0:0:secret-token-value")
    fixtures["route"] = {
        "payload_hex": route_payload.encode("cp1252").hex(),
        "ansi_codec": "cp1252",
        "lan_ip": "10.0.0.7", "public_ip": "203.0.113.7", "public_port": 50000,
        "lan_port": 50001, "command_port": 49999, "computer_name": "DESKTOP-ABC",
        "tcp_port": 4433, "tcp_server_ip": "relay.example.test",
        "matched_custom_password": 1, "target_session_id": 7654321,
        "tcp_server_hostname": "relay.example.test",
        "partner_secret_token": "secret-token-value"}
    short_route = "8:1:10.0.0.7:0:203.0.113.7:50000:50001:49999:DESKTOP-ABC:4433:0:relay.example.test:0:1:relay.example.test"
    fixtures["route_short"] = {"payload_hex": short_route.encode("cp1252").hex(),
                               "partner_secret_token": None}
    fixtures["route_status"] = [
        {"status": status, "payload_hex": f"8:{status}:extra".encode("cp1252").hex()}
        for status in ["2", "11", "99", "7"]]

    # --- decoder primitives ------------------------------------------------
    source = bytes([7, 8, 9, 99, 4, 5, 6, 99, 1, 2, 3, 99])
    fixtures["rows"] = {
        "dib_stride": [{"width": width, "bpp": bpp, "stride": uv.dib_stride(width, bpp)}
                       for width, bpp in [(1, 1), (3, 24), (4, 32), (5, 16), (7, 24), (32, 24)]],
        "extract_tile": [{"src_hex": source.hex(), "bpp": 1, "stride": 4, "x": 1, "y": 0,
                          "width": 2, "height": 2, "full_height": 3,
                          "result_hex": uv.extract_tile(source, 1, 4, 1, 0, 2, 2, 3).hex()}],
        "pack_atlas": [{"src_hex": source.hex(), "dst_len": 12, "bpp": 1, "dest_stride": 4,
                        "src_stride": 4, "src_x": 1, "src_y": 0, "dest_x": 1, "dest_y": 1,
                        "width": 2, "height": 2, "src_height": 3, "fill": 88,
                        "count": uv.pack_atlas(source, bytearray([88] * 12), 1, 4, 4, 1, 0, 1, 1, 2, 2, 3),
                        "dst_hex": bytes(bytearray([88] * 12)).hex()}],
        "tile_sample_score": [{"width": 7, "height": 5, "stride": 21,
                               "src_hex": bytes(v for i in range(35) for v in (10, 20, i)).hex(),
                               "unequal": 0, "total": 6}],
    }
    # pack_atlas/restore_atlas mutate the destination through the reference helper
    atlas_dst = bytearray([88] * 12)
    uv.pack_atlas(source, atlas_dst, 1, 4, 4, 1, 0, 1, 1, 2, 2, 3)
    fixtures["rows"]["pack_atlas"][0]["dst_hex"] = bytes(atlas_dst).hex()
    restore_dst = bytearray([77] * 12)
    fixtures["rows"]["restore_atlas"] = [{
        "src_hex": bytes(atlas_dst).hex(), "dst_len": 12, "bpp": 1, "dest_stride": 4,
        "src_stride": 4, "src_x": 1, "src_y": 1, "dest_x": 0, "dest_y": 1,
        "width": 2, "height": 2, "dest_height": 3, "fill": 77,
        "count": uv.restore_atlas(bytes(atlas_dst), restore_dst, 1, 4, 4, 1, 1, 0, 1, 2, 2, 3),
        "dst_hex": bytes(restore_dst).hex()}]

    fixtures["atlas_layout"] = [
        {"rectangles": [[10, 20, 3, 3], [30, 40, 1, 1]], "block_size": 2, "atlas_height": 3,
         "tiles": [list(t) for t in uv.atlas_layout([(10, 20, 3, 3), (30, 40, 1, 1)], 2, 3)]},
        {"rectangles": [[0, 0, 32, 32], [32, 0, 32, 32], [64, 0, 32, 15]],
         "block_size": 32, "atlas_height": 79,
         "tiles": [list(t) for t in uv.atlas_layout(
             [(0, 0, 32, 32), (32, 0, 32, 32), (64, 0, 32, 15)], 32, 79)]},
    ]

    item = uv.CachedItem(2, 1, -7, b"AB")
    cache_fixture = uv.export_cache([(123, item)])
    fixtures["cache_wire"] = {
        "item": {"width": 2, "height": 1, "rect_hash": -7, "data_hex": b"AB".hex(),
                 "serialized_hex": item.serialize().hex()},
        "export_hex": cache_fixture.hex(),
        "import_keys": sorted(uv.import_cache(cache_fixture).keys()),
    }

    # --- screen update sequences ------------------------------------------
    fixtures["screen_sequences"] = build_screen_sequences(uv, screen_decode, zstd, dictionary)
    fixtures["desktop_dims"] = {
        "valid": [{"width": 1024, "height": 768}, {"width": 1, "height": 1},
                  {"width": 1920, "height": 1080}],
        "invalid": [{"width": 0, "height": 1080}, {"width": -1, "height": 768},
                    {"width": 100000, "height": 100000}, {"width": 8192, "height": 8192},
                    {"width": 1920, "height": 0}],
    }
    for case in fixtures["desktop_dims"]["valid"] + fixtures["desktop_dims"]["invalid"]:
        fields = [b"\x00\x00", struct.pack("<i", case["width"]),
                  struct.pack("<i", case["height"])]
        try:
            size = screen_decode.desktop_size_from_login(fields)
            case["accepted"] = True
            case["value"] = list(size)
        except ValueError:
            case["accepted"] = False

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(fixtures, indent=1, sort_keys=False) + "\n")
    return fixtures


def encode_field_packet(fields: list[bytes]) -> bytes:
    """encode_byte_array_fields without importing the reference again."""
    out = bytearray()
    for field in fields:
        out += struct.pack("<I", len(field)) + field
    return bytes(out)


def build_screen_sequences(uv, screen_decode, zstd, dictionary: bytes) -> list:
    """Screen update sequences validated against the reference RGB555Screen."""
    sequences = []

    def canvas_pixels(image) -> str:
        return base64.b64encode(image.tobytes()).decode("ascii")

    def run(name: str, canvas: tuple, block_size: int, steps: list, note: str = ""):
        screen = screen_decode.RGB555Screen(canvas, screen_decode.ZstdDictionaryDecoder(dictionary),
                                            block_size=block_size)
        entry = {"name": name, "canvas": list(canvas), "block_size": block_size,
                 "updates": [], "note": note}
        for step in steps:
            fields = step["fields"]
            if step.get("expect_error"):
                try:
                    screen.apply(fields)
                except ValueError:
                    entry["updates"].append({"packet_hex": encode_field_packet(fields).hex(),
                                             "error": True, "canvas_hex": canvas_pixels(screen.canvas)})
                    continue
                raise SystemExit(f"reference accepted an update that must fail: {name}")
            count = screen.apply(fields)
            entry["updates"].append({
                "packet_hex": encode_field_packet(fields).hex(),
                "error": False, "applied": count,
                "canvas_sha256": hashlib.sha256(screen.canvas.tobytes()).hexdigest(),
                "canvas_hex": canvas_pixels(screen.canvas),
                "cache_keys": sorted(str(k) for k in screen.cache),
            })
        sequences.append(entry)

    def empty_fields():
        return [b"\x01", b"", b"", b"\x00", b"\x00" * 4, b"", b"", b"", b"", b"", b""]

    # 1. RGB555 pixel path with two rectangles, using reference top-down rows.
    from PIL import Image
    width, height = 64, 48
    base = Image.new("RGB", (width, height), (10, 20, 30))
    def paint(img, x, y, w, h, color):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                img.putpixel((xx, yy), color)
    paint(base, 1, 0, 2, 1, (255, 0, 0))
    paint(base, 0, 1, 1, 1, (0, 255, 0))
    paint(base, 20, 10, 8, 6, (0, 0, 255))
    canonical = base.tobytes()
    rects = [(0, 0, 16, 12), (16, 8, 24, 20)]
    stream = bytearray()
    for (x, y, w, h) in rects:
        stream += rgb555_to_stream(crop_bytes(canonical, width, x, y, w, h), w, h)
    compressed = zstd.compress(bytes(stream), dictionary)
    assert compressed.startswith(ZSTD_MAGIC)
    fields = empty_fields()
    fields[1] = b"".join(struct.pack("<HHHH", *r) for r in rects)
    fields[2] = compressed
    run("rgb555_two_rectangles", (width, height), 32, [{"fields": fields}],
        "reference top-down RGB555 rows, two rectangles in one zstd stream")

    # 2. Same pixels delivered as two separate updates.
    fields_a, fields_b = empty_fields(), empty_fields()
    fields_a[1] = struct.pack("<HHHH", *rects[0])
    fields_a[2] = zstd.compress(rgb555_to_stream(crop_bytes(canonical, width, *rects[0]), rects[0][2], rects[0][3]), dictionary)
    fields_b[1] = struct.pack("<HHHH", *rects[1])
    fields_b[2] = zstd.compress(rgb555_to_stream(crop_bytes(canonical, width, *rects[1]), rects[1][2], rects[1][3]), dictionary)
    run("rgb555_incremental", (width, height), 32,
        [{"fields": fields_a}, {"fields": fields_b}], "two sequential partial updates")

    # 3. Cache additions then a reference to the stored tile.
    from PIL import ImageOps
    cache_canvas = Image.new("RGB", (32, 16), (5, 6, 7))
    paint(cache_canvas, 0, 0, 4, 4, (200, 100, 50))
    fields = empty_fields()
    fields[1] = struct.pack("<HHHH", 0, 0, 32, 16)
    fields[2] = zstd.compress(rgb555_to_stream(cache_canvas.tobytes(), 32, 16), dictionary)
    fields[10] = struct.pack("<hhhhi", 0, 0, 4, 4, 4242)
    step_one = {"fields": fields}
    fields_ref = empty_fields()
    fields_ref[9] = struct.pack("<hhi", 8, 8, 4242)
    step_two = {"fields": fields_ref}
    fields_missing = empty_fields()
    fields_missing[9] = struct.pack("<hhi", 0, 0, 9999)
    run("cache_add_then_reference", (32, 16), 32,
        [step_one, step_two, {"fields": fields_missing, "expect_error": True}],
        "cache addition then reference; missing reference must fail atomically")

    # 4. Scroll copy using the pre-update canvas.
    scroll_canvas = Image.new("RGB", (8, 8))
    for y in range(8):
        for x in range(8):
            scroll_canvas.putpixel((x, y), (y * 30, x * 30, 0))
    fields = empty_fields()
    fields[6] = struct.pack("<hhhhh", 2, 0, 2, 8, 4) + struct.pack("<hhhh", 0, 0, 8, 2)
    seed = empty_fields()
    seed[1] = struct.pack("<HHHH", 0, 0, 8, 8)
    seed[2] = zstd.compress(rgb555_to_stream(scroll_canvas.tobytes(), 8, 8), dictionary)
    bad = empty_fields()
    bad[6] = struct.pack("<hhhhh", 2, 0, 0, 4, 4) + struct.pack("<hhhh", 0, 3, 4, 4)
    run("scroll_copy", (8, 8), 32,
        [{"fields": seed}, {"fields": fields}, {"fields": bad, "expect_error": True}],
        "scroll-copy from the pre-update canvas followed by an out-of-range record")

    # 5. JPEG atlas (bottom-up wire rows), block size 32 (regression case).
    # Synthetic input is flipped before JPEG encoding so the reference's
    # ImageOps.flip restores the intended tile colours (not a real capture).
    atlas = Image.new("RGB", (32, 79), (0, 180, 0))
    paint(atlas, 0, 0, 32, 32, (220, 40, 40))
    paint(atlas, 0, 32, 32, 32, (40, 40, 220))
    paint(atlas, 0, 64, 32, 15, (240, 240, 40))
    buffer = io.BytesIO()
    ImageOps.flip(atlas).save(buffer, format="JPEG", quality=95, subsampling=0)
    jpeg = buffer.getvalue()
    rects = [(0, 0, 32, 32), (32, 0, 32, 32), (64, 0, 32, 15)]
    fields = empty_fields()
    fields[7] = jpeg
    fields[8] = b"".join(struct.pack("<hhhh", *r) for r in rects)
    jpeg_screen = screen_decode.RGB555Screen((96, 32), None, block_size=32)
    jpeg_count = jpeg_screen.apply(fields)
    sequences.append({
        "name": "jpeg_atlas_block32",
        "canvas": [96, 32], "block_size": 32, "approximate": True,
        "updates": [{"packet_hex": encode_field_packet(fields).hex(), "error": False,
                     "applied": jpeg_count, "jpeg_hex": jpeg.hex()}],
        # Historical key name retained, but these are decoded RGB888 samples.
        "samples": [{"point": list(p), "rgb555": list(jpeg_screen.canvas.getpixel(p))}
                    for p in ((0, 0), (33, 0), (65, 14))],
        "note": "JPEG is lossy; C++ compares samples within tolerance",
    })
    # atlas wrong block size must be rejected
    wrong = screen_decode.RGB555Screen((96, 32), None, block_size=16)
    try:
        wrong.apply(fields)
        raise SystemExit("reference accepted a 16-pixel block size for a 32-pixel atlas")
    except ValueError:
        pass
    sequences[-1]["wrong_block_size_error"] = True

    # 6. BitBlt / unsupported field is rejected explicitly.
    unsupported = empty_fields()
    unsupported[5] = b"\x00\x01\x02"
    run("bitblt_unsupported", (16, 16), 32,
        [{"fields": unsupported, "expect_error": True}], "field 5 must not be silently ignored")

    # 7. Rectangle outside canvas and decompression mismatch.
    outside = empty_fields()
    outside[1] = struct.pack("<HHHH", 8, 8, 16, 16)
    outside[2] = zstd.compress(rgb555_to_stream(bytes(32 * 32 * 3), 32, 32), dictionary)
    mismatch = empty_fields()
    mismatch[1] = struct.pack("<HHHH", 0, 0, 4, 4)
    mismatch[2] = zstd.compress(rgb555_to_stream(bytes(2 * 2 * 3), 2, 2), dictionary)
    truncated = empty_fields()
    truncated[1] = struct.pack("<HHHH", 0, 0, 4, 4)
    truncated[2] = b"\x28\xb5\x2f\xfd\x00\x00"
    not_zstd = empty_fields()
    not_zstd[1] = struct.pack("<HHHH", 0, 0, 4, 4)
    not_zstd[2] = b"not-zstd-data"
    run("invalid_updates", (16, 16), 32,
        [{"fields": outside, "expect_error": True},
         {"fields": mismatch, "expect_error": True},
         {"fields": truncated, "expect_error": True},
         {"fields": not_zstd, "expect_error": True}],
        "every rejected update must leave the canvas untouched")

    # 8. Full component ramps distinguish Pillow floor scaling from the common
    # but byte-incompatible bit-replication shortcut. Rows distinguish orientation.
    fields = empty_fields()
    fields[1] = struct.pack("<HHHH", 0, 0, 32, 3)
    raw = b"".join(struct.pack("<H", v << shift) for shift in (10, 5, 0) for v in range(32))
    fields[2] = zstd.compress(raw, dictionary)
    run("rgb555_component_ramps", (32, 3), 32, [{"fields": fields}],
        "all 5-bit components: reference floor(v*255/31), top-down rows")
    return sequences


def rgb555_to_stream(pixels: bytes, width: int, height: int) -> bytes:
    return rgb888_to_rgb555_stream(pixels, width, height)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    here = Path(__file__).resolve().parent
    parser.add_argument("--reference", type=Path, default=here.parent / "reference")
    parser.add_argument("--out", type=Path, default=here.parent / "tests/data/fixtures.json")
    parser.add_argument("--dictionary", type=Path,
                        default=here.parent / "tests/data/test-dict.bin")
    args = parser.parse_args()
    if not (args.reference / "uv_reference.py").is_file():
        parser.error(f"reference implementation not found in {args.reference}")
    fixtures = build_fixtures(args.reference, args.out, args.dictionary)
    counts = {k: (len(v) if isinstance(v, list) else 'object')
              for k, v in fixtures.items() if k != "meta"}
    print(f"wrote {args.out} ({args.out.stat().st_size} bytes)")
    print(f"wrote {args.dictionary} ({args.dictionary.stat().st_size} bytes, "
          f"sha256={fixtures['meta']['dictionary']['sha256'][:16]}...)")
    for key, value in counts.items():
        print(f"  {key}: {value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
