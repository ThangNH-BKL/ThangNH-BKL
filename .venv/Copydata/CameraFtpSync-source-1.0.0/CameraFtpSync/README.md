# CameraFtpSync

Independent Windows 10/11 x64 C++17/MFC desktop application. Reads camera output without changing it. One connection and one synchronization job per user profile. No database is used.

Open `CameraFtpSync.sln` in Visual Studio 2022 and build Release|x64, or run `./build.ps1 -Configuration Release`. The checked-in solution is a CMake build wrapper; configuration generates a full native solution at `build-vs/CameraFtpSync.sln` with separate core, transport, GUI and test projects.

Prerequisites: VS2022 Desktop development with C++, v143 x64 compiler, Windows 10/11 SDK, **C++ MFC for latest v143 build tools (x86 & x64)**, CMake 3.24+. Dependencies are vendored/pinned: libcurl 8.12.1 with Schannel, nlohmann/json 3.11.3; SHA-256 uses Windows CNG; saved passwords use Windows Credential Manager. Normal use needs no administrator privileges. Install the matching Microsoft Visual C++ x64 Redistributable on deployment machines.

Read [Vietnamese guide](docs/USER_GUIDE_VI.md), [deployment](docs/ADMIN_DEPLOYMENT.md), [safety and limitations](docs/SECURITY_AND_LIMITATIONS.md), and [test evidence](docs/TEST_REPORT.md) before deployment. This implementation is **not production-qualified until the pending MFC build, GUI smoke tests and onsite FTP server checks pass**.

Safety behavior: stable local spool snapshots; content and prefix hashes; unknown remote conflict/adoption; unique remote upload files; verified two-step publish; durable JSONL recovery journal and atomic JSON/INI files with recovery copies. Normal updates retain no permanent backups. Manual replacement requires a selected relative filename and operator confirmation; retention touches only recorded, hash-verified owned backups.

`ReadBackSHA256` verifies staging cryptographically. `Size` verifies staging length only; ownership checks and final commit confirmation still use read-back SHA-256. For safety, unchanged files are rehashed locally and remotely each cycle. They are not re-uploaded, but remote read-back consumes bandwidth. There is no APPE or live-final STOR fallback.

Application data: `%LOCALAPPDATA%\CameraFtpSync\{config.ini,sync_state.json,operations.jsonl,logs,spool}`. Closing X hides to tray; **Exit stops synchronization**. Logging out stops this desktop application.

Build tests without MFC: `cmake -S . -B build-core -DCAMERA_BUILD_GUI=OFF`, build, then `ctest --test-dir build-core -C Release --output-on-failure`. Run real loopback FTP integration using `python tests/run_ftp.py build-core/Release/CameraFtpIntegration.exe` after installing `tests/requirements.txt`. See the test report for the exact compiler used in this workspace.

For reproducible core/transport testing without Visual Studio, run `tests/build-zig.ps1 -InstallTools -Tls`. It installs isolated development tools under `.test-tools`, builds Release, and runs core, large-file, loopback FTP, optional FTPS and inventory checks. `tests/run_validation.py <build-directory> --tls` saves raw evidence under `docs/test-results`. Neither helper builds MFC.
