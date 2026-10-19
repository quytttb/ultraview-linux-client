#!/usr/bin/env python3
"""Build an experimental 26-term registration profile for this Linux machine.

Background
----------
Command 3 ("InitMyPassword") carries 26 `:` separated terms.  The public
reference implementation recovered the *schema* (see
`reference/register_once.py` and `uv_reference.build_registration_command`) but
deliberately never published the mapping file
(`work/vb6-init-password-command3-field-sources.json`) that documents where each
remaining term comes from.  `uv_reference.py` states this explicitly: the caller
must supply the machine/settings values and no defaults are guessed.

The upstream repository does ship the experimental profile its author used
(`linux-controller-private.json`).  This tool treats that file as a *schema
donor only*: every identity-bearing term (machine fingerprint, client password
value, machine name, MAC, user name, cached id) is replaced with a value taken
from or derived on *this* machine.  Non-identity terms keep the reference value
because their semantics were never recovered and any replacement would be an
equally arbitrary guess.

Which terms are replaced
------------------------
  1  machine fingerprint  -> MD5(mac + "0" + "0"), uppercase, cp1252
                             (the recovered `fallback_registration_fingerprint`
                             algorithm; the Windows directory ticks are
                             unrecoverable on Linux, so they are "0" — this is
                             the experimental part)
  2  client password value -> fresh random 8 decimal digits
  6  machine name         -> base64(hostname, UTF-8)
  8  MAC                  -> this machine's MAC, 12 uppercase hex digits
 16  user name            -> this machine's login name
 19  cached id            -> "0" (the reference profile held a server-issued
                             id; reusing someone else's is never acceptable)

Caveats — read before using the result
--------------------------------------
* This is an *experiment*.  The server may accept the profile, ignore fields or
  reject it; nothing here proves server acceptance.
* The upstream profile shows traces of the commit "sanitize personal identifier
  traces"; the published values may already differ from the ones that worked in
  the original test.  Do not treat them as verified semantics.
* The generated file is a live identity the moment the server answers an
  InitMyPassword command with an id.  Keep it private (mode 0600) and do not
  commit or redistribute it.
* This tool is a development aid.  It is not installed by the package and the
  native client never generates these values on its own.

Usage
-----
    python3 tools/make_experimental_profile.py \
        --base-profile /path/to/linux-controller-private.json \
        --out ~/.local/share/ultraview/registration-experimental.json

The base profile is not vendored with this project (it contains the upstream
author's data).  Fetch it from the public reference repository if you want to
run this experiment.
"""
from __future__ import annotations

import argparse
import base64
import getpass
import hashlib
import json
import secrets
import socket
import stat
import sys
from pathlib import Path

# The exact constraints enforced by the native client
# (`RegistrationCodec::validateRegistrationTerms`) and the reference
# (`uv_reference.build_registration_command`).
FIXED_TERMS = {0: "3", 3: "0", 5: "0", 12: ""}

REPLACED_FIELDS = (1, 2, 6, 8, 16, 19)

PROVENANCE = {
    0: "fixed by the recovered schema",
    1: "derived: MD5(mac + \"0\" + \"0\") uppercase — fallback fingerprint"
       " algorithm, Windows ticks unrecoverable (experimental input policy)",
    2: "fresh random 8 decimal digits (the reference shape; becomes the client"
       " password value used in the request digest)",
    3: "fixed by the recovered schema",
    4: "reference profile value (semantics not recovered)",
    5: "fixed by the recovered schema",
    6: "derived: base64(hostname, UTF-8), reference profile shape",
    7: "reference profile value; equals the version string this client already"
       " sends in the relay login",
    8: "derived: this machine's MAC, 12 uppercase hex digits",
    9: "reference profile value",
    10: "reference profile value (six pipe separated components)",
    11: "reference profile value",
    12: "fixed by the recovered schema",
    13: "reference profile value",
    14: "reference profile value",
    15: "reference profile value",
    16: "derived: this machine's login name",
    17: "reference profile value",
    18: "reference profile value",
    19: "chosen: neutral \"0\" — the reference profile held a server-issued id,"
        " which is not reused",
    20: "reference profile value",
    21: "reference profile value",
    22: "fixed shape: three unsigned decimal tick strings; zeros as in the"
        " reference profile",
    23: "reference profile value",
    24: "reference profile value",
    25: "reference profile value",
}


def local_mac_address() -> str:
    """First non-loopback hardware address, 12 uppercase hex digits."""
    directories = sorted(Path("/sys/class/net").glob("*/address"))
    for address_file in directories:
        iface = address_file.parent.name
        if iface == "lo":
            continue
        value = address_file.read_text().strip()
        if not value or value == "00:00:00:00:00:00":
            continue
        digits = value.replace(":", "").upper()
        if len(digits) == 12 and all(c in "0123456789ABCDEF" for c in digits):
            return digits
    raise SystemExit("no usable MAC address found under /sys/class/net; "
                     "cannot derive the fingerprint or the MAC term")


def build_terms(base_terms: list[str]) -> tuple[list[str], list[str]]:
    terms = list(base_terms)
    mac = local_mac_address()
    hostname = socket.gethostname()
    username = getpass.getuser()
    notes: list[str] = []

    # 1: recovered algorithm, experimental input policy (no Windows ticks).
    fingerprint_input = (mac + "0" + "0").encode("cp1252")
    terms[1] = hashlib.md5(fingerprint_input).hexdigest().upper()

    # 2: fresh client password value, same shape as the reference profile.
    terms[2] = "".join(str(secrets.randbelow(10)) for _ in range(8))

    # 6: machine name, base64 encoded like the reference value.
    terms[6] = base64.b64encode(hostname.encode("utf-8")).decode("ascii")

    # 8: the machine's MAC without separators.
    terms[8] = mac

    # 16: this machine's user name.
    terms[16] = username

    # 19: never adopt a foreign server-issued id.
    terms[19] = "0"

    for index in REPLACED_FIELDS:
        notes.append(f"term {index}: {PROVENANCE[index]}")
    return terms, notes


def validate(terms: list[str]) -> None:
    """Mirror of RegistrationCodec::validateRegistrationTerms and the reference."""
    if len(terms) != 26 or not all(isinstance(t, str) for t in terms):
        raise SystemExit("registration requires exactly 26 string terms")
    for index, expected in FIXED_TERMS.items():
        if terms[index] != expected:
            raise SystemExit(f"term {index} must equal {expected!r}")
    for index, value in enumerate(terms):
        if "\x00" in value:
            raise SystemExit(f"term {index} contains a NUL character")
        if index != 22 and ":" in value:
            raise SystemExit(f"term {index} contains ':'")
    ticks = terms[22].split(":")
    if len(ticks) != 3:
        raise SystemExit("term 22 requires three ':' separated tick strings")
    for tick in ticks:
        if not tick or not tick.isascii() or not tick.isdecimal():
            raise SystemExit(f"term 22 tick {tick!r} must be unsigned decimal ASCII digits")
    # Strict encoding: the native client never silently replaces characters.
    try:
        ":".join(terms).encode("cp1252")
    except UnicodeEncodeError as error:
        raise SystemExit(f"a term cannot be encoded as cp1252: {error}") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base-profile", type=Path, required=True,
                        help="published reference profile used as the schema donor")
    parser.add_argument("--out", type=Path,
                        default=Path.home() / ".local/share/ultraview/registration-experimental.json",
                        help="where to write the generated profile (default: %(default)s)")
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing output file")
    args = parser.parse_args()

    base = json.loads(args.base_profile.read_text())
    base_terms = base.get("terms")
    if not isinstance(base_terms, list) or len(base_terms) != 26:
        raise SystemExit("the base profile must contain exactly 26 terms")
    if not all(isinstance(t, str) for t in base_terms):
        raise SystemExit("the base profile terms must be strings")
    if not args.force and args.out.exists():
        raise SystemExit(f"{args.out} already exists; pass --force to replace it")

    terms, notes = build_terms(base_terms)
    validate(terms)

    base_digest = hashlib.sha256(args.base_profile.read_bytes()).hexdigest()
    document = {
        "terms": terms,
        "ansi_codec": base.get("ansi_codec", "cp1252"),
        "experimental_linux_metadata_policy": True,
        "_comment": (
            "EXPERIMENTAL registration profile generated by "
            "tools/make_experimental_profile.py. The term schema comes from the "
            "public reference implementation; the mapping of several terms to "
            "machine values was never recovered, so the non-identity terms keep "
            "the reference profile values and the identity terms were derived "
            "on this machine. Server acceptance is unproven."
        ),
        "_base_profile_sha256": base_digest,
        "_provenance": {str(index): PROVENANCE[index] for index in range(26)},
        "_replaced_terms": {str(index): terms[index] for index in REPLACED_FIELDS},
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(document, indent=2) + "\n")
    args.out.chmod(stat.S_IRUSR | stat.S_IWUSR)

    print(f"wrote {args.out} (mode 0600)")
    print(f"base profile sha256: {base_digest}")
    for index in range(26):
        value = terms[index]
        if index == 2:
            value = "********"  # the client password value stays on disk only
        print(f"  term {index:2d} = {value!r:34}  {PROVENANCE[index]}")
    for note in notes:
        print(f"note: {note}")
    print("reminder: use this profile for one bounded experiment only; the "
          "server response decides whether the experimental policy is accepted.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
