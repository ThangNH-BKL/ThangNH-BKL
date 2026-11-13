#pragma once
#include "json.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace camera {
static_assert(sizeof(void *) == 8, "CameraFtpSync requires a 64-bit Windows target");
namespace fs = std::filesystem;
using Json = nlohmann::json;
enum class ErrorKind {
    Local,
    Transient,
    Authentication,
    TLS,
    Permission,
    Conflict,
    Integrity,
    Persistence,
    Cancelled,
    Configuration,
    UnsupportedVersion
};
struct Error : std::runtime_error {
    ErrorKind kind;
    Error(ErrorKind k, const std::string &s) : std::runtime_error(s), kind(k) {}
};
struct Config {
    Config();
    fs::path source;
    std::string host, username, remoteRoot = "/production/camera", station = "PC01", include = "*.txt",
                                exclude = "*.tmp;*.part;.*", security = "ExplicitFTPS",
                                verification = "ReadBackSHA256";
    int port = 21, intervalMinutes = 5, stableSeconds = 30, retries = 5, connectTimeout = 15,
        transferTimeout = 600, backupCount = 3, backupDays = 7;
    bool recursive = false, separate = true, remember = false, plainConsent = false, sharedConsent = false,
         allowEmpty = false, passive = true, autoStart = false, startWindows = false, retention = false,
         gapConsent = false;
    std::string credential = "CameraFtpSync/default";
    void validate() const;
    std::string destination(const std::string &) const;
    std::string jobKey() const;
};
void parseUrl(Config &, const std::string &);
void safeRemote(const std::string &);
std::string utcNow();
std::string sourceModifiedUtc(const fs::path &);
std::string uniqueId();
std::string hashFile(const fs::path &, std::atomic_bool *cancel = nullptr,
                     std::optional<uint64_t> limit = {});
void atomicWrite(const fs::path &, const std::string &, bool backup = true);
void durableAppend(const fs::path &, const std::string &);
Config loadConfig(const fs::path &);
void saveConfig(const fs::path &, const Config &);
std::string credentialRead(const std::string &);
void credentialWrite(const std::string &, const std::string &, const std::string &);
void credentialDelete(const std::string &);
void setStartup(bool);
fs::path dataDirectory();
class InstanceLock {
    void *handle_ = nullptr;

  public:
    explicit InstanceLock(const fs::path &);
    ~InstanceLock();
    InstanceLock(const InstanceLock &) = delete;
};
class Logger {
    fs::path root_;
    std::mutex mutex_;

  public:
    explicit Logger(fs::path r) : root_(std::move(r)) {}
    void write(const std::string &, const std::string &, const std::string &);
};
struct RemoteInfo {
    uint64_t size = 0;
};
class IFtpClient {
  public:
    virtual ~IFtpClient() = default;
    virtual void prepare() {}
    virtual void prepareFile(const std::string &) {}
    virtual void test() = 0;
    virtual std::optional<RemoteInfo> stat(const std::string &) = 0;
    virtual std::string hash(const std::string &, std::optional<uint64_t> limit = {}) = 0;
    virtual void upload(const fs::path &, const std::string &) = 0;
    virtual void rename(const std::string &, const std::string &) = 0;
    virtual void remove(const std::string &) = 0;
};
struct Snapshot {
    fs::path path;
    uint64_t size;
    int64_t mtime;
    std::string identity, hash;
};
struct Totals {
    uint64_t scanned = 0, skipped = 0, uploaded = 0, failed = 0, conflicts = 0, bytes = 0, retries = 0,
             pending = 0;
};
using Notify = std::function<void(const std::string &)>;
class Store {
    fs::path root_;

  public:
    Json state = {{"schema_version", 1}, {"files", Json::object()}, {"backups", Json::array()}};
    explicit Store(fs::path);
    void save();
    void journal(Json, const std::string &);
    std::map<std::string, Json> operations();
    const fs::path &root() const {
        return root_;
    }
};
class Engine {
    Config config_;
    IFtpClient &ftp_;
    Store &store_;
    std::atomic_bool &cancel_;
    Notify notify_;
    std::map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point>> observed_;
    std::mutex runMutex_;
    std::function<std::chrono::steady_clock::time_point()> clock_;
    Snapshot snapshot(const fs::path &);
    void syncFile(const fs::path &, Totals &, bool);
    void checkpoint(const Json &);
    void verify(const std::string &, uint64_t, const std::string &, bool);
    void retention();

  public:
    Engine(Config, IFtpClient &, Store &, std::atomic_bool &, Notify = {},
           std::function<std::chrono::steady_clock::time_point()> clock = std::chrono::steady_clock::now);
    void recover();
    Totals run(const std::optional<std::string> &approvedRelative = {});
};
class Scheduler {
    std::thread worker_;
    std::atomic_bool stop_{false}, paused_{true}, now_{false};

  public:
    ~Scheduler();
    void launch(std::function<void()>, int);
    void start() {
        paused_ = false;
        now_ = true;
    }
    void pause() {
        paused_ = true;
    }
    void wake() {
        if (!paused_)
            now_ = true;
    }
    bool isPaused() const {
        return paused_;
    }
    void syncNow() {
        now_ = true;
    }
    void stop();
};
} // namespace camera
