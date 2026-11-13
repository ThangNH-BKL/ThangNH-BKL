#pragma once
#include "../Core/Core.h"
namespace camera {
class CurlFtpClient : public IFtpClient {
    Config config_;
    std::string password_;
    fs::path spool_;
    std::atomic_bool &cancel_;
    Notify progress_;
    std::string url(const std::string &) const;
    void *session(const std::string &);
    void perform(void *);
    void commands(const std::vector<std::string> &);
    void ensureParents(const std::string &);

  public:
    CurlFtpClient(Config, std::string, fs::path, std::atomic_bool &, Notify = {});
    ~CurlFtpClient();
    void prepare() override {
        ensureParents(config_.destination(".prepare"));
    }
    void prepareFile(const std::string &path) override {
        ensureParents(path);
    }
    void test() override;
    std::optional<RemoteInfo> stat(const std::string &) override;
    std::string hash(const std::string &, std::optional<uint64_t> limit = {}) override;
    void upload(const fs::path &, const std::string &) override;
    void rename(const std::string &, const std::string &) override;
    void remove(const std::string &) override;
    void writeProbe();
};
} // namespace camera
