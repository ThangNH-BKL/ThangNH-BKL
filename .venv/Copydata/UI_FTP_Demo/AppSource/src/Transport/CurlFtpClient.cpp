#include "CurlFtpClient.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <algorithm>
#include <curl/curl.h>
#include <iomanip>
#include <sstream>
#include <windows.h>
namespace camera {
namespace {
struct CurlGuard {
    CURL *h;
    ~CurlGuard() {
        curl_easy_cleanup(h);
    }
};
struct ListGuard {
    curl_slist *p = nullptr;
    ~ListGuard() {
        curl_slist_free_all(p);
    }
    void add(const std::string &s) {
        auto next = curl_slist_append(p, s.c_str());
        if (!next)
            throw std::bad_alloc();
        p = next;
    }
};
struct FileGuard {
    FILE *f;
    ~FileGuard() {
        if (f)
            fclose(f);
    }
};
size_t discard(char *, size_t a, size_t b, void *) {
    return a * b;
}
size_t collect(char *p, size_t a, size_t b, void *ctx) {
    auto &s = *(std::string *)ctx;
    size_t n = a * b;
    if (s.size() + n > 4 * 1024 * 1024)
        return 0;
    try {
        s.append(p, n);
    } catch (...) {
        return 0;
    }
    return n;
}
struct Progress {
    std::atomic_bool *cancel;
    Notify *notify;
    std::chrono::steady_clock::time_point last{};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};
int progress(void *p, curl_off_t downloadTotal, curl_off_t downloaded, curl_off_t total, curl_off_t current) {
    auto &c = *(Progress *)p;
    if (*c.cancel)
        return 1;
    auto now = std::chrono::steady_clock::now();
    if (*c.notify && now - c.last > std::chrono::seconds(1)) {
        c.last = now;
        try {
            (*c.notify)(
                "Transfer bytes (up/down): " + std::to_string(current) + " / " + std::to_string(total) +
                " ; " + std::to_string(downloaded) + " / " + std::to_string(downloadTotal) + " ; bytes/s=" +
                std::to_string(
                    (uint64_t)((double)(current + downloaded) /
                               std::max(0.001, std::chrono::duration<double>(now - c.started).count()))));
        } catch (...) {
            return 1;
        }
    }
    return 0;
}
} // namespace
CurlFtpClient::CurlFtpClient(Config c, std::string secret, fs::path spool, std::atomic_bool &cancel, Notify n)
    : config_(std::move(c)), password_(std::move(secret)), spool_(std::move(spool)), cancel_(cancel),
      progress_(std::move(n)) {
    static std::once_flag once;
    std::call_once(once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw Error(ErrorKind::Local, "libcurl initialization failed");
    });
    fs::create_directories(spool_);
}
CurlFtpClient::~CurlFtpClient() {
    if (!password_.empty())
        SecureZeroMemory(password_.data(), password_.size());
}
std::string CurlFtpClient::url(const std::string &p) const {
    safeRemote(p);
    std::ostringstream o;
    o << ((config_.security == "ImplicitFTPS" || config_.security == "ImplicitFTPS-NoVerify") ? "ftps://" : "ftp://") << config_.host << ':' << config_.port
      << "/"; // double slash specifies server absolute path
    for (unsigned char c : p) {
        if (c == '/' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
            o << (char)c;
        else
            o << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    return o.str();
}
void *CurlFtpClient::session(const std::string &p) {
    auto *h = curl_easy_init();
    if (!h)
        throw std::bad_alloc();
    curl_easy_setopt(h, CURLOPT_URL, url(p).c_str());
    curl_easy_setopt(h, CURLOPT_USERNAME, config_.username.c_str());
    curl_easy_setopt(h, CURLOPT_PASSWORD, password_.c_str());
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
    curl_easy_setopt(h, CURLOPT_PROXY, "");
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, (long)config_.connectTimeout);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, (long)config_.transferTimeout);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    const bool skipVerification = config_.security == "ExplicitFTPS-NoVerify" ||
                                  config_.security == "ImplicitFTPS-NoVerify";
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, skipVerification ? 0L : 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, skipVerification ? 0L : 2L);
    curl_easy_setopt(h, CURLOPT_USE_SSL,
                     (long)(config_.security == "PlainFTP" ? CURLUSESSL_NONE : CURLUSESSL_ALL));
    curl_easy_setopt(h, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(h, CURLOPT_FTP_USE_EPSV, 1L);
    if (!config_.passive)
        curl_easy_setopt(h, CURLOPT_FTPPORT, "-");
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, discard);
    return h;
}
void CurlFtpClient::perform(void *ptr) {
    auto *h = (CURL *)ptr;
    Progress ctx{&cancel_, &progress_};
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    char errorBuffer[CURL_ERROR_SIZE]{};
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errorBuffer);
    auto result = curl_easy_perform(h);
    // The handle outlives this stack buffer, including on exception paths.
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, nullptr);
    if (result == CURLE_OK)
        return;
    long code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    if (result == CURLE_ABORTED_BY_CALLBACK)
        throw Error(ErrorKind::Cancelled, "Transfer cancelled safely");
    if (result == CURLE_LOGIN_DENIED || code == 530)
        throw Error(ErrorKind::Authentication, "FTP 530: check username/password and account access");
    if (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CONNECT_ERROR ||
        result == CURLE_USE_SSL_FAILED || result == CURLE_SSL_CACERT_BADFILE) {
        std::string detail = errorBuffer[0] ? errorBuffer : curl_easy_strerror(result);
        // Keep the diagnostic on one log line and exclude the configured secret.
        if (!password_.empty()) {
            size_t pos = 0;
            while ((pos = detail.find(password_, pos)) != std::string::npos) {
                detail.replace(pos, password_.size(), "[redacted]");
                pos += 10;
            }
        }
        for (char &c : detail)
            if (static_cast<unsigned char>(c) < 32 || c == 127)
                c = ' ';
        std::string hint;
        if (detail.find("AcquireCredentialsHandle failed") != std::string::npos)
            hint = "Windows Schannel could not initialize local TLS credentials; check Windows TLS policy and cryptographic providers. This occurs before server certificate verification.";
        else if (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CACERT_BADFILE)
            hint = "Check certificate hostname, validity and Windows trusted CA chain.";
        else if (result == CURLE_USE_SSL_FAILED)
            hint = "Check that the server supports the selected FTPS mode and port.";
        else
            hint = "Check explicit/implicit FTPS mode and port, server TLS 1.2+ support, and the TLS detail above.";
        throw Error(ErrorKind::TLS, "FTPS TLS rejected (curl code=" + std::to_string((int)result) +
                                        "): " + detail + ". " + hint);
    }
    if (code == 550 || code == 553 || result == CURLE_QUOTE_ERROR || result == CURLE_REMOTE_ACCESS_DENIED)
        throw Error(
            ErrorKind::Permission,
            "FTP permission/rename failure (550/553): check directory access and server rename support");
    if (code == 552)
        throw Error(ErrorKind::Permission,
                    "FTP quota exceeded: staged upload and rollback need free capacity");
    if (result == CURLE_OPERATION_TIMEDOUT || result == CURLE_COULDNT_RESOLVE_HOST ||
        result == CURLE_COULDNT_CONNECT || result == CURLE_SEND_ERROR || result == CURLE_RECV_ERROR ||
        result == CURLE_PARTIAL_FILE)
        throw Error(ErrorKind::Transient,
                    "FTP network failure: check network, firewall, passive ports and timeout");
    throw Error(ErrorKind::Integrity, "FTP operation failed; no unsafe direct-write fallback");
}
void CurlFtpClient::test() {
    auto root = config_.remoteRoot;
    if (root.back() != '/')
        root += '/';
    CurlGuard h{(CURL *)session(root)};
    curl_easy_setopt(h.h, CURLOPT_DIRLISTONLY, 1L);
    perform(h.h);
}
std::optional<RemoteInfo> CurlFtpClient::stat(const std::string &p) {
    CurlGuard h{(CURL *)session(p)};
    curl_easy_setopt(h.h, CURLOPT_NOBODY, 1L);
    try {
        perform(h.h);
    } catch (const Error &) {
        long code = 0;
        curl_easy_getinfo(h.h, CURLINFO_RESPONSE_CODE, &code);
        if (code != 550)
            throw; // A 550 is ambiguous: prove absence through an accessible parent listing.
        auto slash = p.rfind('/');
        auto parent = p.substr(0, slash + 1);
        auto name = p.substr(slash + 1);
        std::string listing;
        CurlGuard l{(CURL *)session(parent)};
        curl_easy_setopt(l.h, CURLOPT_DIRLISTONLY, 1L);
        curl_easy_setopt(l.h, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(l.h, CURLOPT_WRITEDATA, &listing);
        perform(l.h);
        std::istringstream in(listing);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            auto last = line.find_last_of('/');
            if ((last == std::string::npos ? line : line.substr(last + 1)) == name)
                throw Error(ErrorKind::Permission, "Remote file exists but SIZE access denied");
        }
        return {};
    }
    curl_off_t size = -1;
    curl_easy_getinfo(h.h, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &size);
    if (size < 0)
        throw Error(ErrorKind::Integrity, "Server does not provide usable SIZE");
    return RemoteInfo{(uint64_t)size};
}
std::string CurlFtpClient::hash(const std::string &p, std::optional<uint64_t> limit) {
    auto local = spool_ / fs::u8path(uniqueId() + ".readback");
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code ec;
            fs::remove(p, ec);
        }
    } cleanup{local};
    FILE *stream = nullptr;
    if (_wfopen_s(&stream, local.c_str(), L"wb") != 0)
        stream = nullptr;
    FileGuard f{stream};
    if (!f.f)
        throw Error(ErrorKind::Local, "Cannot stage read-back; check disk space");
    CurlGuard h{(CURL *)session(p)};
    curl_easy_setopt(h.h, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(h.h, CURLOPT_WRITEDATA, f.f);
    perform(h.h);
    if (fflush(f.f) || ferror(f.f))
        throw Error(ErrorKind::Local, "Read-back disk write failed");
    fclose(f.f);
    f.f = nullptr;
    return hashFile(local, &cancel_, limit);
}
void CurlFtpClient::commands(const std::vector<std::string> &cmd) {
    auto root = config_.remoteRoot;
    if (root.back() != '/')
        root += '/';
    CurlGuard h{(CURL *)session(root)};
    ListGuard list;
    for (auto &s : cmd)
        list.add(s);
    curl_easy_setopt(h.h, CURLOPT_QUOTE, list.p);
    curl_easy_setopt(h.h, CURLOPT_NOBODY, 1L);
    perform(h.h);
}
void CurlFtpClient::ensureParents(const std::string &p) {
    std::string root = config_.remoteRoot;
    while (root.size() > 1 && root.back() == '/')
        root.pop_back();
    auto finalRoot = config_.destination("");
    if (p.rfind(finalRoot, 0) != 0)
        throw Error(ErrorKind::Configuration, "Upload outside configured namespace");
    test();
    size_t pos = root == "/" ? 1 : root.size() + 1;
    while ((pos = p.find('/', pos)) != std::string::npos) {
        auto parent =
            p.substr(0, pos); // MKD failure is ignored only here; following CWD/upload must still succeed.
        commands({"*MKD " + parent});
        ++pos;
    }
}
void CurlFtpClient::upload(const fs::path &local, const std::string &remote) {
    ensureParents(remote);
    FILE *stream = nullptr;
    if (_wfopen_s(&stream, local.c_str(), L"rb") != 0)
        stream = nullptr;
    FileGuard f{stream};
    if (!f.f)
        throw Error(ErrorKind::Local, "Snapshot unavailable");
    CurlGuard h{(CURL *)session(remote)};
    curl_easy_setopt(h.h, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(h.h, CURLOPT_READDATA, f.f);
    curl_easy_setopt(h.h, CURLOPT_INFILESIZE_LARGE, (curl_off_t)fs::file_size(local));
    perform(h.h);
    curl_off_t sent = 0;
    curl_easy_getinfo(h.h, CURLINFO_SIZE_UPLOAD_T, &sent);
    if ((uint64_t)sent != fs::file_size(local))
        throw Error(ErrorKind::Integrity, "Uploaded byte count mismatch");
}
void CurlFtpClient::rename(const std::string &a, const std::string &b) {
    safeRemote(a);
    safeRemote(b);
    if (a.substr(0, a.rfind('/')) != b.substr(0, b.rfind('/')))
        throw Error(ErrorKind::Configuration, "Publish rename must stay in one directory");
    auto scope = config_.destination("");
    if (a.rfind(scope, 0) != 0 || b.rfind(scope, 0) != 0)
        throw Error(ErrorKind::Configuration, "Rename outside configured namespace");
    if (stat(b))
        throw Error(ErrorKind::Conflict, "Rename destination already exists");
    commands({"RNFR " + a, "RNTO " + b});
}
void CurlFtpClient::remove(const std::string &p) {
    safeRemote(p);
    if (p.rfind(config_.destination(""), 0) != 0)
        throw Error(ErrorKind::Configuration, "Delete outside configured namespace");
    commands({"DELE " + p});
}
void CurlFtpClient::writeProbe() {
    auto id = uniqueId();
    auto a = config_.destination(".write-test." + id), b = config_.destination(".rename-test." + id);
    auto p = spool_ / fs::u8path(id + ".probe");
    atomicWrite(p, "CameraFtpSync capability probe\n", false);
    try {
        upload(p, a);
        auto expected = hashFile(p);
        if (hash(a) != expected)
            throw Error(ErrorKind::Integrity, "Probe verification failed");
        rename(a, b);
        if (hash(b) != expected)
            throw Error(ErrorKind::Integrity, "Probe rename verification failed");
        remove(b);
        fs::remove(p);
    } catch (...) {
        std::error_code ec;
        fs::remove(p, ec);
        throw Error(ErrorKind::Permission, "Write/rename probe failed; inspect uniquely named "
                                           ".write-test/.rename-test artifacts; cleanup may be required");
    }
}
} // namespace camera
