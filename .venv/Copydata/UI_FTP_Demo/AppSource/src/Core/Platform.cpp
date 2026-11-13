#include "Core.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <regex>
#include <shlobj.h>
#include <sstream>
#include <wincred.h>
namespace camera {
namespace {
struct Handle {
    HANDLE h;
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE && h)
            CloseHandle(h);
    }
};
void check(bool ok, const char *msg) {
    if (!ok)
        throw Error(ErrorKind::Persistence,
                    std::string(msg) + " (Windows error=" + std::to_string(GetLastError()) + ")");
}
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n)
        throw Error(ErrorKind::Configuration, "Invalid UTF-8");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
    return w;
}
void flushWrite(const fs::path &p, const std::string &s, bool append) {
    Handle f{CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                         append ? OPEN_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    check(f.h != INVALID_HANDLE_VALUE, "Cannot open durable file");
    if (append)
        check(SetFilePointerEx(f.h, {}, nullptr, FILE_END), "Cannot seek journal");
    size_t pos = 0;
    while (pos < s.size()) {
        DWORD n = 0;
        check(WriteFile(f.h, s.data() + pos, (DWORD)std::min<size_t>(s.size() - pos, 1048576), &n, nullptr) &&
                  n,
              "Disk write failed");
        pos += n;
    }
    check(FlushFileBuffers(f.h), "Disk flush failed");
}
} // namespace
std::string utcNow() {
    SYSTEMTIME t;
    GetSystemTime(&t);
    char b[32];
    snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    return b;
}
std::string sourceModifiedUtc(const fs::path &p) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    SYSTEMTIME t{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &data) ||
        !FileTimeToSystemTime(&data.ftLastWriteTime, &t))
        return "unavailable";
    char b[32];
    snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    return b;
}
std::string uniqueId() {
    unsigned char bytes[16];
    if (BCryptGenRandom(nullptr, bytes, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw Error(ErrorKind::Local, "Random generator failed");
    std::ostringstream o;
    for (auto b : bytes)
        o << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return o.str();
}
std::string hashFile(const fs::path &p, std::atomic_bool *cancel, std::optional<uint64_t> limit) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw Error(ErrorKind::Local, "SHA256 unavailable");
    struct Cleanup {
        BCRYPT_ALG_HANDLE &a;
        BCRYPT_HASH_HANDLE &h;
        ~Cleanup() {
            if (h)
                BCryptDestroyHash(h);
            if (a)
                BCryptCloseAlgorithmProvider(a, 0);
        }
    } guard{alg, hash};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw Error(ErrorKind::Local, "SHA256 init failed");
    Handle f{CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (f.h == INVALID_HANDLE_VALUE)
        throw Error(ErrorKind::Local, "Source busy or unreadable");
    char buffer[65536];
    uint64_t remaining = limit.value_or(UINT64_MAX);
    while (remaining) {
        if (cancel && *cancel)
            throw Error(ErrorKind::Cancelled, "Cancelled");
        DWORD n = 0;
        if (!ReadFile(f.h, buffer, (DWORD)std::min<uint64_t>(remaining, sizeof(buffer)), &n, nullptr))
            throw Error(ErrorKind::Local, "Read failed");
        if (!n)
            break;
        if (BCryptHashData(hash, (PUCHAR)buffer, n, 0) < 0)
            throw Error(ErrorKind::Local, "Hash failed");
        remaining -= n;
    }
    if (limit && remaining)
        throw Error(ErrorKind::Integrity, "Prefix shorter than expected");
    unsigned char result[32];
    if (BCryptFinishHash(hash, result, 32, 0) < 0)
        throw Error(ErrorKind::Local, "Hash failed");
    std::ostringstream out;
    for (auto b : result)
        out << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return out.str();
}
void atomicWrite(const fs::path &p, const std::string &s, bool backup) {
    fs::create_directories(p.parent_path());
    auto tmp = p;
    tmp += L".new." + wide(uniqueId());
    try {
        flushWrite(tmp, s, false);
        if (fs::exists(p)) {
            if (backup) {
                auto previous = p;
                previous += L".bak";
                check(CopyFileW(p.c_str(), previous.c_str(), FALSE), "Cannot retain recovery copy");
                Handle b{CreateFileW(previous.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     0, nullptr)};
                check(b.h != INVALID_HANDLE_VALUE && FlushFileBuffers(b.h), "Cannot flush recovery copy");
            }
            check(MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                  "Atomic replacement failed");
        } else
            check(MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_WRITE_THROUGH), "Atomic rename failed");
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
}
void durableAppend(const fs::path &p, const std::string &s) {
    fs::create_directories(p.parent_path());
    flushWrite(p, s, true);
}
void safeRemote(const std::string &p) {
    if (p.empty() || p[0] != '/' || p.find('\\') != std::string::npos || p.find("//") != std::string::npos)
        throw Error(ErrorKind::Configuration, "Remote path must be absolute and normalized");
    for (unsigned char c : p)
        if (c < 32 || c == 127)
            throw Error(ErrorKind::Configuration, "Control character in path");
    std::istringstream i(p);
    std::string part;
    while (std::getline(i, part, '/'))
        if (part == ".." || part == "." || part.find(':') != std::string::npos)
            throw Error(ErrorKind::Configuration, "Unsafe remote path");
}
Config::Config() {
    char name[64]{};
    DWORD size = 63;
    if (!GetComputerNameA(name, &size))
        strcpy_s(name, "CAMERA");
    std::string machine = name;
    for (auto &c : machine)
        if (!isalnum((unsigned char)c) && c != '-' && c != '_')
            c = '_';
    station = machine.substr(0, 40) + "-" + uniqueId().substr(0, 8);
}
void Config::validate() const {
    if (source.empty() || !fs::is_directory(source))
        throw Error(ErrorKind::Configuration, "Select an existing source directory");
    if (host.empty() || host.find_first_of("/@\\\r\n :?#") != std::string::npos)
        throw Error(ErrorKind::Configuration, "Enter host only, or use Parse URL");
    if (username.empty() || username.find_first_of("\r\n") != std::string::npos)
        throw Error(ErrorKind::Configuration, "Username required");
    safeRemote(remoteRoot);
    if (port < 1 || port > 65535 || intervalMinutes < 1 || intervalMinutes > 1440 || stableSeconds < 5 ||
        stableSeconds > 300 || retries < 0 || retries > 10 || connectTimeout < 1 || connectTimeout > 300 ||
        transferTimeout < 1 || transferTimeout > 86400 || backupCount < 1 || backupCount > 1000 ||
        backupDays < 1 || backupDays > 3650)
        throw Error(ErrorKind::Configuration, "Numeric setting outside safe range");
    if (security != "ExplicitFTPS" && security != "ImplicitFTPS" && security != "PlainFTP" &&
        security != "ExplicitFTPS-NoVerify" && security != "ImplicitFTPS-NoVerify")
        throw Error(ErrorKind::Configuration, "Unsupported security mode");
    if (security == "PlainFTP" && !plainConsent)
        throw Error(ErrorKind::Configuration,
                    "Plain FTP requires explicit consent: passwords and data are unencrypted");
    if (!separate && !sharedConsent)
        throw Error(ErrorKind::Configuration, "Shared namespace requires deliberate exclusive owner consent");
    if (!std::regex_match(station, std::regex("[A-Za-z0-9_-]{1,64}")))
        throw Error(ErrorKind::Configuration, "Station ID: use 1-64 letters, digits, underscore or hyphen");
    if (verification != "ReadBackSHA256" && verification != "Size")
        throw Error(ErrorKind::Configuration, "Unknown verification mode");
    if (include.empty() || include.find_first_of("/\\\r\n") != std::string::npos ||
        exclude.find_first_of("/\\\r\n") != std::string::npos)
        throw Error(ErrorKind::Configuration, "Invalid file masks");
    if (credential != "CameraFtpSync/default")
        throw Error(ErrorKind::Configuration, "Invalid credential reference");
}
std::string Config::destination(const std::string &r) const {
    auto root = remoteRoot;
    while (root.size() > 1 && root.back() == '/')
        root.pop_back();
    auto p = (root == "/" ? "" : root) + (separate ? "/" + station : "") + "/" + r;
    safeRemote(p);
    return p;
}
std::string Config::jobKey() const {
    return Json{{"version", 2},
                {"source", fs::weakly_canonical(source).u8string()},
                {"host", host},
                {"port", port},
                {"username", username},
                {"security", security},
                {"destination", destination("")}}
        .dump();
}
void parseUrl(Config &c, const std::string &s) {
    std::smatch m;
    std::regex re("^(ftp|ftps)://([^/@:?#]+)(:([0-9]{1,5}))?(/[^?#]*)?/?$");
    if (!std::regex_match(s, m, re))
        throw Error(ErrorKind::Configuration,
                    "Use ftp(s)://host[:port]/path without credentials; SFTP is unsupported");
    c.host = m[2];
    c.security = m[1] == "ftps" ? "ImplicitFTPS" : "ExplicitFTPS";
    c.port = m[4].matched ? std::stoi(m[4]) : (c.security == "ImplicitFTPS" ? 990 : 21);
    if (m[5].matched)
        c.remoteRoot = m[5];
    if (c.remoteRoot.find('%') != std::string::npos)
        throw Error(ErrorKind::Configuration, "Enter decoded remote path separately");
    safeRemote(c.remoteRoot);
}
Config loadConfig(const fs::path &p) {
    Config c;
    if (!fs::exists(p))
        return c;
    if (fs::file_size(p) > 65536)
        throw Error(ErrorKind::Configuration, "Config too large");
    std::ifstream f(p);
    std::string line;
    std::map<std::string, std::string> v;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
            continue;
        auto n = line.find('=');
        if (n == std::string::npos)
            throw Error(ErrorKind::Configuration, "Malformed INI");
        auto k = line.substr(0, n);
        std::string lower = k;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char a) { return (char)tolower(a); });
        if (lower.find("password") != std::string::npos && k != "RememberPassword")
            throw Error(ErrorKind::Configuration, "Remove secret from INI; use credential storage");
        if (!v.emplace(k, line.substr(n + 1)).second)
            throw Error(ErrorKind::Configuration, "Duplicate INI key");
    }
    if (v.count("SettingsVersion") && v["SettingsVersion"] != "1")
        throw Error(ErrorKind::Configuration,
                    "Unsupported settings version; preserve config and upgrade the application");
    auto str = [&](const char *k, std::string &x) {
        if (v.count(k))
            x = v[k];
    };
    auto num = [&](const char *k, int &x) {
        if (v.count(k)) {
            size_t n = 0;
            x = std::stoi(v[k], &n);
            if (n != v[k].size())
                throw Error(ErrorKind::Configuration, "Invalid integer");
        }
    };
    auto flag = [&](const char *k, bool &x) {
        if (v.count(k)) {
            if (v[k] != "true" && v[k] != "false")
                throw Error(ErrorKind::Configuration, "Invalid boolean");
            x = v[k] == "true";
        }
    };
    if (v.count("Folder"))
        c.source = fs::u8path(v["Folder"]);
    str("Host", c.host);
    str("Username", c.username);
    str("RemoteRoot", c.remoteRoot);
    str("StationID", c.station);
    str("Include", c.include);
    str("Exclude", c.exclude);
    str("Security", c.security);
    str("Verification", c.verification);
    str("Credential", c.credential);
    num("Port", c.port);
    num("IntervalMinutes", c.intervalMinutes);
    num("StableSeconds", c.stableSeconds);
    num("RetryAttempts", c.retries);
    num("ConnectTimeout", c.connectTimeout);
    num("TransferTimeout", c.transferTimeout);
    num("BackupCount", c.backupCount);
    num("BackupDays", c.backupDays);
    flag("Recursive", c.recursive);
    flag("SeparateByStation", c.separate);
    flag("RememberPassword", c.remember);
    flag("PlainConsent", c.plainConsent);
    flag("ExclusiveOwnerConsent", c.sharedConsent);
    flag("AllowEmpty", c.allowEmpty);
    flag("Passive", c.passive);
    flag("AutoStart", c.autoStart);
    flag("StartWindows", c.startWindows);
    flag("Retention", c.retention);
    flag("GapConsent", c.gapConsent);
    c.validate();
    return c;
}
void saveConfig(const fs::path &p, const Config &c) {
    c.validate();
    std::ostringstream o;
    o << "[CameraFtpSync]\nSettingsVersion=1\n";
    auto str = [&](const char *k, const auto &v) {
        std::ostringstream s;
        s << std::boolalpha << v;
        auto t = s.str();
        if (t.find_first_of("\r\n") != std::string::npos)
            throw Error(ErrorKind::Configuration, "Newline in setting");
        o << k << '=' << t << '\n';
    };
    str("Folder", c.source.u8string());
    str("Host", c.host);
    str("Username", c.username);
    str("RemoteRoot", c.remoteRoot);
    str("StationID", c.station);
    str("Include", c.include);
    str("Exclude", c.exclude);
    str("Security", c.security);
    str("Verification", c.verification);
    str("Credential", c.credential);
    str("Port", c.port);
    str("IntervalMinutes", c.intervalMinutes);
    str("StableSeconds", c.stableSeconds);
    str("RetryAttempts", c.retries);
    str("ConnectTimeout", c.connectTimeout);
    str("TransferTimeout", c.transferTimeout);
    str("BackupCount", c.backupCount);
    str("BackupDays", c.backupDays);
    str("Recursive", c.recursive);
    str("SeparateByStation", c.separate);
    str("RememberPassword", c.remember);
    str("PlainConsent", c.plainConsent);
    str("ExclusiveOwnerConsent", c.sharedConsent);
    str("AllowEmpty", c.allowEmpty);
    str("Passive", c.passive);
    str("AutoStart", c.autoStart);
    str("StartWindows", c.startWindows);
    str("Retention", c.retention);
    str("GapConsent", c.gapConsent);
    atomicWrite(p, o.str());
}
std::string credentialRead(const std::string &ref) {
    PCREDENTIALW p = nullptr;
    if (!CredReadW(wide(ref).c_str(), CRED_TYPE_GENERIC, 0, &p))
        return {};
    std::string out((char *)p->CredentialBlob, p->CredentialBlobSize);
    CredFree(p);
    return out;
}
void credentialWrite(const std::string &ref, const std::string &user, const std::string &secret) {
    auto r = wide(ref), u = wide(user);
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = r.data();
    c.UserName = u.data();
    c.CredentialBlob = (LPBYTE)secret.data();
    c.CredentialBlobSize = (DWORD)secret.size();
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    check(CredWriteW(&c, 0), "Cannot save credential");
}
void credentialDelete(const std::string &ref) {
    if (!CredDeleteW(wide(ref).c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND)
        throw Error(ErrorKind::Persistence, "Cannot delete saved credential");
}
fs::path dataDirectory() {
    PWSTR p = nullptr;
    check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)), "LocalAppData unavailable");
    fs::path r = fs::path(p) / L"CameraFtpSync";
    CoTaskMemFree(p);
    fs::create_directories(r);
    return r;
}
void setStartup(bool enabled) {
    HKEY k;
    check(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr,
                          0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS,
          "Cannot open startup settings");
    LONG result;
    if (enabled) {
        wchar_t exe[32768];
        DWORD n = GetModuleFileNameW(nullptr, exe, 32768);
        std::wstring v = L"\"" + std::wstring(exe, n) + L"\"";
        result =
            RegSetValueExW(k, L"CameraFtpSync", 0, REG_SZ, (BYTE *)v.c_str(), (DWORD)((v.size() + 1) * 2));
    } else {
        result = RegDeleteValueW(k, L"CameraFtpSync");
        if (result == ERROR_FILE_NOT_FOUND)
            result = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    check(result == ERROR_SUCCESS, "Cannot update startup setting");
}
InstanceLock::InstanceLock(const fs::path &root) {
    fs::create_directories(root);
    handle_ = CreateFileW((root / L"writer.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
        throw Error(ErrorKind::Persistence, "Another instance is using this state folder");
}
InstanceLock::~InstanceLock() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE)
        CloseHandle(handle_);
}
void Logger::write(const std::string &level, const std::string &category, const std::string &message) {
    std::lock_guard<std::mutex> l(mutex_);
    auto dir = root_ / L"logs";
    fs::create_directories(dir);
    auto p = dir / fs::u8path(utcNow().substr(0, 10) + ".log");
    std::string safe = std::regex_replace(message, std::regex("(ftp|ftps)://[^ /]*@"), "[redacted-url]@");
    if (safe.find("PASS ") != std::string::npos || safe.find("password=") != std::string::npos)
        safe = "[redacted]";
    std::replace(safe.begin(), safe.end(), '\n', ' ');
    if (fs::exists(p) && fs::file_size(p) > 10 * 1024 * 1024) {
        auto archive = p;
        archive += L".1";
        std::error_code ec;
        fs::remove(archive, ec);
        fs::rename(p, archive);
    }
    durableAppend(p, utcNow() + " " + level + " " + category + " " + safe + "\n");
    for (auto &e : fs::directory_iterator(dir))
        if (e.is_regular_file() && std::chrono::duration_cast<std::chrono::hours>(
                                       fs::file_time_type::clock::now() - e.last_write_time())
                                           .count() > 24 * 30) {
            std::error_code ec;
            fs::remove(e.path(), ec);
        }
}
} // namespace camera
