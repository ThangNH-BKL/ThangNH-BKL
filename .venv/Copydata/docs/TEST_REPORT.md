# Test report — 2026-11-12

Status: **core/transport validated locally; desktop production release NOT qualified**.

Environment: Windows x64; Python 3.14; Zig 0.13.0 providing Clang 18.1.6/libc++; CMake 3.31.6; Ninja 1.11.1.4. No VS2022 compiler or MFC installation was available. Native Windows core, real libcurl 8.12.1/Schannel transport and three test executables were compiled and linked in Release mode. No GUI executable or runtime deployment ZIP was built, and no GUI screenshot is claimed.

Executed evidence (raw outputs in `docs/test-results/`):

- 44 named core cases pass, including one partially skipped credential test: plaintext absence is checked, but Credential Manager round trip is unavailable in this logon/sandbox (Windows error 1312: no such logon session). CTest groups these as one core suite, with a second streaming-resource suite; they are not 44 separate CTest entries.
- Real loopback pyftpdlib 2.0.1 FTP: valid login, rejected bad password/530, unique write/rename/delete probe, initial snapshot upload, unchanged skip without STOR, append full snapshot, read-back hashes, two-step publish, exact final bytes, and no leftover temp/rollback/probe objects pass. No external production endpoint was contacted.
- Local self-signed explicit FTPS endpoint: fails closed and receives no uploaded data. Schannel returns curl 35 before peer verification; **certificate-specific rejection is skipped**. Neither trusted explicit FTPS nor implicit FTPS is proven here. Trust stores were not modified.
- CNG streamed SHA-256 over a sparse **4,295,032,832-byte** file: digest `bba36e59cfe7128ca94bcdd249819426b61b38eb289784e98d245a8356263607`, independently reproduced by Python hashlib. Final Release measurement: 5,585 ms, peak working set 7,397,376 bytes. This is local hashing, not a 4-GB network upload.
- 1,000 source files scanned/deferred correctly and exclusion masks applied. Separate 1,000-entry metadata serialization: 100,781 bytes, atomic save 24 ms in the final Release measurement. Full 1,000-file FTP throughput/state rewrite performance is not benchmarked.
- Dependency digest/source audit passes: no database source, API or runtime dependency in the audited inventory. Runtime package audit remains pending until an MFC package exists.

The suite exposed and fixed an open input-stream handle preventing atomic journal-tail replacement on Windows. It also tests content-preserving failures before publish, failed rename, crash after moving old, crash after new publish before checkpoint, idempotent reconciliation, corrupt state fallback/quarantine and partial journal tails. This is deterministic fault injection, not proof against hardware power-loss behavior.

| Spec case | Evidence / remaining validation |
|---|---|
| 01 | INI round trip/mapping pass; GUI editing pending |
| 02 | Invalid source/path/numeric validation pass |
| 03 | Real FTP login and LIST pass |
| 04 | Real 530/bad password rejection pass |
| 05 | TLS fail-closed pass; certificate validation assertion skipped |
| 06–08 | Real FTP initial/unchanged/append pass |
| 09 | Same-size rewrite conflict pass |
| 10 | Changing observations and post-copy consistency code; concurrent writer stress pending |
| 11–14 | Mock identical adoption, unknown/larger remote, truncation/reset pass |
| 15–18 | Mock partial STOR, SIZE/hash mismatch, rename failure preserve final pass |
| 19–20 | Mock crash after old/new rename and idempotent recovery pass |
| 21–22 | Run mutex and scheduler coalescing/pause/resume pass; GUI clicks pending |
| 23 | Mock permission/550 failure pass; real server permission matrix pending |
| 24 | 1,000-file scan and >4-GB streaming hash pass; large FTP/performance pending |
| 25 | Credential API implemented; actual secure-store round trip skipped by environment |
| 26 | INI/no-secret checks pass; GUI restart prompt pending |
| 27 | Unicode local paths/mock remote mapping pass; real Unicode FTP pending |
| 28 | Namespace separation pass; two-PC live test pending |
| 29 | Shared namespace consent validation pass; distributed concurrency unsupported |
| 30–31 | Explicit backup and owned-only hash-checked retention pass in mock |
| 32 | Steady-clock scheduler pass; physical suspend/resume pending |
| 33 | Unavailable staging spool fails safely; actual disk/quota exhaustion pending |
| 34–35 | Credential URL rejection and plain FTP consent pass |
| 36 | Mock rename unsupported fails without final STOR pass |
| 37 | Publish-before-checkpoint recovery pass, including first upload |
| 38–39 | State backup/quarantine, future schema and partial journal-tail recovery pass |
| 40 | Exclusive Windows lock-file second-writer rejection pass; GUI second process pending |
| 41 | Secret INI key and traversal rejection; log redaction pass |
| 42 | Source/dependency audit pass; packaged MFC runtime scan pending |

Reproduce with `tests/build-zig.ps1 -InstallTools -Tls` on a compatible Windows account, or use the VS2022 instructions in ADMIN_DEPLOYMENT.md. The helper installs development tools beneath the project only. Optional TLS test requirements are separate from runtime. A Windows security context may skip the same credential/certificate subchecks; this must not be interpreted as their validation.

Release gates: build actual MFC x64 Release; execute GUI/tray/startup/DPI/screenshot checklist; validate Credential Manager in an interactive user logon; validate trusted and untrusted explicit/implicit FTPS; qualify actual FTP SIZE/rename/permissions/Unicode/quota/recovery behavior; accept the two-step visibility gap; review dependency security advisories; sign and test the runtime package on a clean machine. See SECURITY_AND_LIMITATIONS.md for deliberately conservative operational limits and unmet efficiency/UI refinements.

Build-wrapper follow-up: PowerShell script parsing and project XML validation pass. The original invocation reproduces a script-execution-policy failure in this environment. With the process-scoped execution-policy argument, the revised wrapper executes, captures a transcript and reports the next prerequisite failure (CMake absent from the normal PATH/Visual Studio installation here) with exit 1. This verifies diagnostics and process invocation, not an MFC build on the user's machine. CMake's VS-bundled lookup requires validation on that machine.
