# Third-party inventory

- libcurl 8.12.1: https://curl.se/download/curl-8.12.1.tar.xz, curl license in CURL-LICENSE.txt. Vendored source archive, built statically with Schannel; archive hash enforced by CMake. All application network handles restrict protocols to FTP/FTPS.
- nlohmann/json 3.11.3: https://github.com/nlohmann/json/releases/tag/v3.11.3, MIT license in JSON-LICENSE.txt. Vendored single header; hash enforced by CMake.
- Microsoft MFC, Windows SDK, Windows CNG, Credential Manager and Schannel: OS/Visual Studio components under Microsoft's terms, not third-party database dependencies. Distribution requires the appropriate VC++/MFC redistributable license.

See SHA256SUMS.txt for exact downloaded artifacts and license file digests. Before production, check the official curl security advisories (https://curl.se/docs/security.html), update dependencies if needed, pin the new digests and rerun all tests.

Development-only (not shipped): Zig 0.13.0/Clang 18.1.6, CMake 3.31.6, Ninja 1.11.1.4, clang-format 18.1.8; Python 3.14; loopback server dependencies in tests/requirements.txt and requirements-tls.txt. These test dependencies do not become application runtime dependencies.
