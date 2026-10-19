# Author, provenance and redistribution

## Author

**Hai Quy ([@quytttb](https://github.com/quytttb))** — the native C++/Qt client,
its interface and this documentation. This project's own code is MIT licensed
(see [LICENSE](LICENSE)).

## Reference study material (not part of this repository)

The Python comparison material used while recovering the wire format comes from
[caohoangphucs/ultraviewer-linux](https://github.com/caohoangphucs/ultraviewer-linux)
at commit `a0e090fe4de7ae31924a9a19e685c6f74bad74d3`. Credit for that protocol
study and reference implementation goes to its author.

That snapshot is **not vendored in this repository**: `reference/` is ignored by
git and must be fetched from upstream (same commit) if the comparison or the
upstream baseline tests are wanted. It is never a runtime dependency and is never
installed or packaged.

The snapshot had **no LICENSE file**. This repository grants no license for
upstream material, and its MIT license covers only this project's own code.
Obtain permission/legal review before redistributing upstream material or
binaries that embed it.

## Trademark

This is an independent, experimental interoperability client. UltraViewer is a
trademark of its respective owner; this project is not affiliated with or endorsed
by that owner. Use it only with the remote owner's informed authorization.

## Assets and secrets

No proprietary `8bit.dat`, real partner profile, captured session, or upstream
password is shipped. Test identities, passwords and TLS keys are synthetic and
public test-only material; never use the test certificate as a production trust
anchor. The zstd test dictionary is trained from synthetic pixels. Machine
identity material (registration profiles, settings, dictionaries copied from a
Windows installation) is listed in `.gitignore` and must stay local.

Qt, OpenSSL, libzstd and optionally libsecret retain their respective licenses.
AppImage distributors must supply required library notices and comply with Qt's
applicable licensing/relinking obligations. Packaging scripts are not legal clearance.
