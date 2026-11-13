# Deployment and build

Use Windows 10/11 x64, VS2022 v143 with Desktop C++, MFC x64 and a Windows SDK. CMake minimum 3.24; development used 3.31.6. No dependency download is needed for application libraries: the curl archive and JSON header are included and their hashes appear in `third_party/SHA256SUMS.txt`. libcurl archive is checked by CMake SHA256 before extraction. The JSON header must also be checked against the manifest before release.

Run in a Developer PowerShell:

```powershell
./build.ps1 -Configuration Release -Package
python -m pip install -r tests/requirements.txt
python tests/run_ftp.py build-vs/Release/CameraFtpIntegration.exe
```

The build script runs core tests. Integration requires Python only on a test machine, not at runtime. Open the generated native `build-vs/CameraFtpSync.sln` to debug individual projects. The root solution delegates to this build script. Packaging with CPack ZIP installs only the GUI executable, documentation and third-party notices. The ZIP is unsigned; code-sign and scan before distributing. Do not distribute test executables as the desktop application.

Install the Visual C++ 2015–2022 x64 Redistributable matching v143 from Microsoft, then unzip into a user-writable application directory. MFC is dynamically linked and the redistributable is required. libcurl is statically linked using Windows Schannel; no OpenSSL DLL, database DLL or Python runtime is required by the app. Dependencies are pinned for reproducibility, **not certified current or vulnerability-free**. Review curl's official security advisories and update/revalidate the pin before production rollout.

Create the configured FTP root with the server administrator. The app creates only station/relative child directories below that root. Grant LIST, SIZE/RETR, STOR, RNFR/RNTO and DELE only in the assigned station subtree; root LIST is also required. Restrict each account to its station where possible. Passive mode requires the server's passive data ports through the firewall. Configure certificate hostname and trusted chain; no TLS downgrade or verification bypass exists.

Set a unique Station ID and keep separate namespaces. Confirm exclusive write ownership and whether the temporary missing-final gap is acceptable. If not acceptable, this two-step implementation cannot be deployed for replacements. Test on the actual server with downstream readers stopped first. No distributed lock/CAS guarantee is offered.

Local data is per Windows user in `%LOCALAPPDATA%\CameraFtpSync`. Protect this directory with normal user ACLs. Back up config, state and journal together while the app is exited. The source folder must be outside app storage; do not place staging/state in production data. Log retention is 30 days and 10 MiB/day plus one overflow file. Source file payloads are never stored in JSON.

Start with Windows writes only the current user's Run entry, only after explicit opt-in and Save. Disable it in the GUI before uninstalling. Exit, remove the executable directory; retain app state until remote data is reconciled. Uncheck Remember Password and Save to remove the credential, or remove `CameraFtpSync/default` in Windows Credential Manager. Do not remove unknown remote files during uninstall.

Recovery: stop all writers. Preserve the source, local state/journal, final, `.uploading.*` and `.rollback.*` remote files. Restart with the same configuration; the engine checks actual remote hashes. It adopts a fully published new revision or restores a hash-proven old rollback if final is absent. Ambiguous/partial artifacts remain untouched and block further publishing. Have an operator compare bytes and restore the intended final using a separate administrative FTP client; archive journal/state before a supervised reconciliation. Never delete the only known good copy. A foreign/modified rollback is a conflict, not garbage.

Bounds: 100,000 checkpoint entries, 64 MiB state/journal, 4 MiB directory listings. Limits fail closed. No automatic checkpoint pruning or journal compaction is currently provided; archive completed jobs/journals offline only after confirming all operations terminal. Do not truncate active journals. Orphan local spool files are preserved after crashes for inspection and require supervised cleanup when no process is running.

## Visual Studio reports MSB3073 / exited with code 1

That line is a wrapper summary, not the underlying compiler error. Open View > Output, select Build, and read the first preceding error. The updated wrapper uses `-ExecutionPolicy Bypass` only for its child PowerShell process; it does not modify the machine or user execution-policy settings. Organization Group Policy still applies.

The build script finds CMake on PATH or inside the VS2022 installation via VSINSTALLDIR/vswhere. If unavailable, install **C++ CMake tools for Windows** through Visual Studio Installer and restart Visual Studio. Also install Desktop development with C++, MSVC v143 x64/x86, a Windows SDK and **C++ MFC for latest v143 build tools (x86 & x64)**.

Each invocation records `build-logs/build-<configuration>-<timestamp>.txt`, identifies Configure / Compile and link / Automated tests / Package, and preserves the failing exit code. Share that log when diagnosing a failure. If PowerShell itself is blocked before the script starts, no transcript can be created; share the Visual Studio Output text instead.

To run the same build outside Visual Studio:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug
```

If a copied `build-vs/CMakeCache.txt` still points to a different source directory or generator, close Visual Studio and rename `build-vs` to `build-vs.previous`, then rebuild so CMake configures a fresh directory. Keep the old directory for inspection. Do not remove application state or production data to fix a build error.

Version 1.0.1 changes job identity and journal validation. Read REVIEW_1.0.1.md before replacing an existing installation; do not upgrade in the middle of a publish. Only FTP/FTPS are compiled into the vendored libcurl 8.23.0 build.

## FTPS connection test reports curl code 35

Code 35 is a TLS handshake failure, not proof of an untrusted certificate. The connection test now includes the underlying Schannel diagnostic. Match ExplicitFTPS to the server AUTH TLS port (commonly 21), or ImplicitFTPS to its dedicated TLS port (commonly 990); confirm the actual port with the administrator. TLS 1.2 or newer is required.

If the detail says `AcquireCredentialsHandle failed: SEC_E_NO_CREDENTIALS`, Windows failed to initialize local TLS credentials before server certificate verification. Check the affected Windows account, Schannel client TLS policy and cryptographic provider availability with the Windows administrator. That message does not identify an incorrect FTP password. Installing a server CA alone is not an established fix for this initialization error.

For certificate verification errors, check the configured hostname, certificate validity, complete chain and trusted issuing CA. Verification stays enabled. The loopback demo server at 127.0.0.1:2121 supports PlainFTP only; use that mode only for the isolated demo with explicit consent.

To bypass certificate checks, select `ExplicitFTPS-NoVerify` or `ImplicitFTPS-NoVerify` in the Security dropdown and Save / Apply. TLS encryption remains enabled, but server identity is not authenticated. This bypass does not resolve Schannel credential initialization failures. To skip TLS entirely on a server that supports plain FTP, select `PlainFTP` and enable the unencrypted FTP consent checkbox.
