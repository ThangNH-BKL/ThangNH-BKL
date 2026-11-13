#include "Core.h"
#include <fstream>
#include <future>
#include <iostream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
using namespace camera;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("CHECK failed: " #x);                                                   \
    } while (false)
struct Fake : IFtpClient {
    std::map<std::string, std::string> files;
    int uploads = 0, renames = 0;
    std::string fault;
    fs::path scratch;
    std::function<void()> duringUpload;
    explicit Fake(fs::path p) : scratch(p) {}
    void test() override {
        if (fault == "auth")
            throw Error(ErrorKind::Authentication, "530");
    }
    std::optional<RemoteInfo> stat(const std::string &p) override {
        if (!files.count(p))
            return {};
        return RemoteInfo{files[p].size() +
                          (fault == "size" && p.find("uploading") != std::string::npos ? 1 : 0)};
    }
    std::string hash(const std::string &p, std::optional<uint64_t> limit = {}) override {
        if (fault == "hash" && p.find("uploading") != std::string::npos)
            return std::string(64, '0');
        if (!files.count(p))
            throw Error(ErrorKind::Permission, "550");
        auto s = files.at(p);
        if (limit)
            s.resize(std::min<uint64_t>(*limit, s.size()));
        auto tmp = scratch / "hash.tmp";
        atomicWrite(tmp, s, false);
        auto h = hashFile(tmp);
        fs::remove(tmp);
        return h;
    }
    void upload(const fs::path &p, const std::string &r) override {
        ++uploads;
        std::ifstream f(p, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(f)), {});
        files[r] = data;
        if (duringUpload)
            duringUpload();
        if (fault == "upload") {
            files[r] = data.substr(0, data.size() / 2);
            throw Error(ErrorKind::Transient, "disconnect");
        }
        if (fault == "once") {
            fault = "";
            throw Error(ErrorKind::Transient, "temporary network failure");
        }
        if (fault == "auth")
            throw Error(ErrorKind::Authentication, "530");
        if (fault == "permission")
            throw Error(ErrorKind::Permission, "550");
    }
    void rename(const std::string &a, const std::string &b) override {
        ++renames;
        if (fault == "rename")
            throw Error(ErrorKind::Permission, "rename unsupported");
        CHECK(!files.count(b));
        CHECK(files.count(a));
        files[b] = files.at(a);
        files.erase(a);
        if (fault == "after_old" && b.find("rollback") != std::string::npos)
            throw Error(ErrorKind::Transient, "crash after old moved");
        if (fault == "after_new" && a.find("uploading") != std::string::npos)
            throw Error(ErrorKind::Transient, "crash after publish");
    }
    void remove(const std::string &p) override {
        files.erase(p);
    }
};
struct Fixture {
    fs::path root = fs::temp_directory_path() / fs::u8path("camera-test-" + uniqueId());
    Config c;
    std::atomic_bool cancel{false};
    std::chrono::steady_clock::time_point time{};
    std::unique_ptr<Store> store;
    std::unique_ptr<Fake> ftp;
    std::unique_ptr<Engine> engine;
    Fixture() {
        fs::create_directories(root / "source");
        c.source = root / "source";
        c.station = "PC01";
        c.host = "localhost";
        c.username = "user";
        c.security = "PlainFTP";
        c.plainConsent = true;
        c.gapConsent = true;
        c.stableSeconds = 5;
        c.retries = 0;
        store = std::make_unique<Store>(root / "data");
        ftp = std::make_unique<Fake>(root);
        engine = makeEngine();
    }
    ~Fixture() {
        engine.reset();
        ftp.reset();
        store.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::unique_ptr<Engine> makeEngine() {
        return std::make_unique<Engine>(c, *ftp, *store, cancel, Notify{}, [this] { return time; });
    }
    void source(const std::string &s, const std::string &n = "lsc.txt") {
        std::ofstream f(c.source / fs::u8path(n), std::ios::binary | std::ios::trunc);
        f << s;
    }
    Totals sync(std::optional<std::string> approved = {}) {
        engine->run(approved);
        time += std::chrono::seconds(6);
        return engine->run(approved);
    }
    std::string final() {
        return c.destination("lsc.txt");
    }
};
int passed = 0;
void test(const char *name, std::function<void()> fn) {
    try {
        fn();
        ++passed;
        std::cout << "PASS " << name << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << name << ": " << e.what() << std::endl;
        throw;
    }
}
int main() {
    try {
        test("SHA256 known vector and streaming prefix", [] {
            Fixture f;
            f.source("abc");
            CHECK(hashFile(f.c.source / "lsc.txt") ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
            CHECK(hashFile(f.c.source / "lsc.txt", nullptr, 3) == hashFile(f.c.source / "lsc.txt"));
        });
        test("INI round trip and destination preview", [] {
            Fixture f;
            saveConfig(f.root / "config.ini", f.c);
            auto c = loadConfig(f.root / "config.ini");
            CHECK(c.destination("sub/lsc.txt") == "/production/camera/PC01/sub/lsc.txt");
            CHECK(c.intervalMinutes == 5);
        });
        test("invalid source, hostile path, plaintext and shared namespace rejected", [] {
            Fixture f;
            for (int i = 0; i < 4; ++i) {
                auto c = f.c;
                if (i == 0)
                    c.source = f.root / "missing";
                if (i == 1)
                    c.remoteRoot = "/safe/../escape";
                if (i == 2)
                    c.plainConsent = false;
                if (i == 3) {
                    c.separate = false;
                    c.sharedConsent = false;
                }
                bool caught = false;
                try {
                    c.validate();
                } catch (...) {
                    caught = true;
                }
                CHECK(caught);
            }
        });
        test("URL userinfo and SFTP rejected; URL parsed", [] {
            Config c;
            parseUrl(c, "ftps://example.org:990/camera");
            CHECK(c.port == 990 && c.security == "ImplicitFTPS");
            for (auto s : {"ftp://user:secret@host/a", "sftp://host/a", "ftp://host/a/../b"}) {
                bool caught = false;
                try {
                    parseUrl(c, s);
                } catch (...) {
                    caught = true;
                }
                CHECK(caught);
            }
        });
        test("secret in config rejected", [] {
            Fixture f;
            atomicWrite(f.root / "bad.ini", "Password=secret\n", false);
            bool caught = false;
            try {
                loadConfig(f.root / "bad.ini");
            } catch (...) {
                caught = true;
            }
            CHECK(caught);
        });
        test("initial upload then unchanged cycles create no backup", [] {
            Fixture f;
            f.source("abc");
            CHECK(f.sync().uploaded == 1);
            CHECK(f.ftp->files.at(f.final()) == "abc");
            auto n = f.ftp->uploads;
            CHECK(f.engine->run().skipped == 1);
            CHECK(f.engine->run().skipped == 1);
            CHECK(f.ftp->uploads == n);
            CHECK(f.ftp->files.size() == 1);
        });
        test("append full snapshot and transient rollback cleanup", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            f.source("abcdef");
            CHECK(f.sync().uploaded == 1);
            CHECK(f.ftp->files.at(f.final()) == "abcdef");
            CHECK(f.ftp->files.size() == 1);
        });
        test("same size rewrite conflicts", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            f.source("xyz");
            CHECK(f.sync().conflicts == 1);
            CHECK(f.ftp->files.at(f.final()) == "abc");
        });
        test("initial identical adopt without upload", [] {
            Fixture f;
            f.source("abc");
            f.ftp->files[f.final()] = "abc";
            CHECK(f.sync().skipped == 1);
            CHECK(f.ftp->uploads == 0);
        });
        test("unknown and larger remote preserved", [] {
            Fixture f;
            f.source("abc");
            f.ftp->files[f.final()] = "unrelated larger";
            CHECK(f.sync().conflicts == 1);
            CHECK(f.ftp->uploads == 0);
        });
        test("truncated local conflicts", [] {
            Fixture f;
            f.source("abcdef");
            f.sync();
            f.source("ab");
            CHECK(f.sync().conflicts == 1);
            CHECK(f.ftp->files.at(f.final()) == "abcdef");
        });
        test("external remote revision conflicts", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            f.ftp->files[f.final()] = "xyz";
            CHECK(f.engine->run().conflicts == 1);
        });
        test("source changing between observations is deferred", [] {
            Fixture f;
            f.source("abc");
            CHECK(f.engine->run().pending == 1);
            f.time += std::chrono::seconds(6);
            f.source("abcdef");
            CHECK(f.engine->run().pending == 1);
            CHECK(f.ftp->uploads == 0);
        });
        for (auto fault : {"upload", "size", "hash", "rename", "permission"})
            test((std::string("failure preserves final: ") + fault).c_str(), [fault] {
                Fixture f;
                f.source("abc");
                f.sync();
                f.source("abcdef");
                f.ftp->fault = fault;
                f.engine->run();
                f.time += std::chrono::seconds(6);
                auto t = f.engine->run();
                CHECK(t.failed == 1);
                CHECK(f.ftp->files.at(f.final()) == "abc");
                CHECK(f.store->state["files"].begin().value()["size"] == 3);
            });
        test("crash after old rename restores old final", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            f.source("abcdef");
            f.ftp->fault = "after_old";
            f.sync();
            CHECK(!f.ftp->files.count(f.final()));
            f.ftp->fault = "";
            f.engine->recover();
            CHECK(f.ftp->files.at(f.final()) == "abc");
        });
        test("crash after publish reconciles checkpoint idempotently", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            f.source("abcdef");
            f.ftp->fault = "after_new";
            f.sync();
            f.ftp->fault = "";
            auto n = f.ftp->uploads;
            f.engine->recover();
            f.engine->recover();
            CHECK(f.ftp->files.at(f.final()) == "abcdef");
            CHECK(f.engine->run().skipped == 1);
            CHECK(f.ftp->uploads == n);
            CHECK(f.ftp->files.size() == 1);
        });
        test("manual destructive replacement retains owned backup", [] {
            Fixture f;
            f.source("new");
            f.ftp->files[f.final()] = "old";
            CHECK(f.sync(std::string("lsc.txt")).uploaded == 1);
            CHECK(f.store->state["backups"].size() == 1);
            CHECK(f.ftp->files.size() == 2);
            CHECK(f.ftp->files.at(f.store->state["backups"][0]["path"]) == "old");
        });
        test("retention only deletes recorded owned verified backups", [] {
            Fixture f;
            f.c.retention = true;
            f.c.backupCount = 1;
            f.engine = f.makeEngine();
            f.source("new1");
            f.ftp->files[f.final()] = "old";
            f.sync(std::string("lsc.txt"));
            f.source("new2");
            f.ftp->files[f.c.destination("foreign^20260101.txt")] = "foreign";
            f.sync(std::string("lsc.txt"));
            CHECK(f.store->state["backups"].size() == 1);
            CHECK(f.ftp->files.at(f.c.destination("foreign^20260101.txt")) == "foreign");
        });
        test("corrupt checkpoint uses known good backup", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            f.store->save();
            atomicWrite(f.root / "data/sync_state.json", "{", false);
            Store recovered(f.root / "data");
            CHECK(recovered.state["files"].size() == 1);
        });
        test("corrupt checkpoint without backup fails closed", [] {
            Fixture f;
            atomicWrite(f.root / "data/sync_state.json", "{", false);
            bool caught = false;
            try {
                Store bad(f.root / "data");
            } catch (...) {
                caught = true;
            }
            CHECK(caught);
        });
        test("future state schema rejected", [] {
            Fixture f;
            atomicWrite(f.root / "data/sync_state.json", "{\"schema_version\":99}", false);
            bool caught = false;
            try {
                Store bad(f.root / "data");
            } catch (...) {
                caught = true;
            }
            CHECK(caught);
        });
        test("partial journal tail preserved and complete records recover", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            auto before = f.store->operations().size();
            durableAppend(f.root / "data/operations.jsonl", "{\"partial\":");
            CHECK(f.store->operations().size() == before);
            CHECK(f.store->operations().size() == before);
        });
        test("single writer lock", [] {
            Fixture f;
            InstanceLock first(f.root);
            bool caught = false;
            try {
                InstanceLock second(f.root);
            } catch (...) {
                caught = true;
            }
            CHECK(caught);
        });
        test("station namespaces differ", [] {
            Fixture f;
            auto other = f.c;
            other.station = "PC02";
            CHECK(other.destination("lsc.txt") != f.final());
        });
        test("Unicode local source and relative path", [] {
            Fixture f;
            auto name = std::string("camera-\xE1\xBB\x87.txt");
            f.source("abc", name);
            CHECK(f.sync().uploaded == 1);
            CHECK(f.ftp->files.at(f.c.destination(name)) == "abc");
        });
        test("cancelled snapshot cannot publish", [] {
            Fixture f;
            f.source("abc");
            f.engine->run();
            f.time += std::chrono::seconds(6);
            f.cancel = true;
            f.engine->run();
            CHECK(f.ftp->uploads == 0);
        });
        test("non-overlap guard", [] {
            Fixture f;
            f.source("abc");
            f.engine->run();
            f.time += std::chrono::seconds(6);
            bool blocked = false;
            f.ftp->duringUpload = [&] {
                try {
                    f.engine->run();
                } catch (const Error &) {
                    blocked = true;
                }
            };
            CHECK(f.engine->run().uploaded == 1);
            CHECK(blocked);
        });
        test("scheduler coalesces and pause/resume", [] {
            Scheduler scheduler;
            std::atomic_int active{0}, maximum{0}, runs{0};
            scheduler.launch(
                [&] {
                    int a = ++active;
                    if (a > maximum)
                        maximum = a;
                    ++runs;
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    --active;
                },
                1);
            scheduler.start();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            for (int i = 0; i < 20; ++i)
                scheduler.syncNow();
            std::this_thread::sleep_for(std::chrono::milliseconds(450));
            scheduler.pause();
            int n = runs;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            CHECK(runs == n);
            scheduler.start();
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            scheduler.stop();
            CHECK(maximum == 1);
            CHECK(runs > n);
        });
        test("credential opt-in round trip, no config secret", [] {
            Fixture f;
            auto ref = "CameraFtpSync/test-" + uniqueId();
            try {
                credentialWrite(ref, "test-user", "isolated-test-secret");
                CHECK(credentialRead(ref) == "isolated-test-secret");
                credentialDelete(ref);
                CHECK(credentialRead(ref).empty());
            } catch (const Error &e) {
                std::cout << "SKIP Windows credential API unavailable in session: " << e.what() << std::endl;
            }
            saveConfig(f.root / "config.ini", f.c);
            std::ifstream in(f.root / "config.ini");
            std::string ini((std::istreambuf_iterator<char>(in)), {});
            CHECK(ini.find("isolated-test-secret") == std::string::npos);
        });
        test("recovery after published file before checkpoint on first upload", [] {
            Fixture f;
            f.source("abc");
            f.ftp->fault = "after_new";
            f.sync();
            CHECK(f.store->state["files"].empty());
            f.ftp->fault = "";
            f.store = std::make_unique<Store>(f.root / "data");
            f.engine = f.makeEngine();
            f.engine->recover();
            CHECK(f.store->state["files"].size() == 1);
            CHECK(f.ftp->uploads == 1);
        });
        test("missing remote after confirmation conflicts", [] {
            Fixture f;
            f.source("abc");
            f.sync();
            f.ftp->files.erase(f.final());
            CHECK(f.engine->run().conflicts == 1);
            CHECK(f.ftp->uploads == 1);
        });
        test("incomplete transaction from changed job blocks recovery", [] {
            Fixture f;
            f.source("abc");
            f.ftp->fault = "after_new";
            f.sync();
            f.ftp->fault = "";
            f.c.station = "OTHER";
            f.engine = f.makeEngine();
            bool caught = false;
            try {
                f.engine->recover();
            } catch (const Error &e) {
                caught = e.kind == ErrorKind::Conflict;
            }
            CHECK(caught);
        });
        test("empty file cannot truncate confirmed remote", [] {
            Fixture f;
            f.c.allowEmpty = true;
            f.engine = f.makeEngine();
            f.source("abc");
            f.sync();
            f.source("");
            CHECK(f.sync().conflicts == 1);
            CHECK(f.ftp->files.at(f.final()) == "abc");
        });
        test("unaccepted publish gap blocks replacement", [] {
            Fixture f;
            f.c.gapConsent = false;
            f.engine = f.makeEngine();
            f.source("abc");
            f.sync();
            f.source("abcdef");
            CHECK(f.sync().conflicts == 1);
            CHECK(f.ftp->files.at(f.final()) == "abc");
        });
        test("1000 file scan filters and staging observations", [] {
            Fixture f;
            for (int i = 0; i < 1000; ++i)
                f.source("abc", "lsc" + std::to_string(i) + ".txt");
            f.source("ignored", "ignore.tmp");
            auto t = f.engine->run();
            CHECK(t.scanned == 1000);
            CHECK(t.pending == 1000);
            CHECK(f.ftp->uploads == 0);
        });
        test("transient retry reconciles staged file before retry", [] {
            Fixture f;
            f.c.retries = 1;
            f.engine = f.makeEngine();
            f.source("abc");
            f.ftp->fault = "once";
            auto t = f.sync();
            CHECK(t.uploaded == 1);
            CHECK(t.retries == 1);
            CHECK(f.ftp->files.size() == 1);
        });
        test("authentication failure stops attempt without retry", [] {
            Fixture f;
            f.source("abc");
            f.engine->run();
            f.time += std::chrono::seconds(6);
            f.ftp->fault = "auth";
            bool caught = false;
            try {
                f.engine->run();
            } catch (const Error &e) {
                caught = e.kind == ErrorKind::Authentication;
            }
            CHECK(caught);
            CHECK(f.ftp->uploads == 1);
        });
        test("exclusive source lock is deferred without upload", [] {
            Fixture f;
            f.source("abc");
            f.engine->run();
            f.time += std::chrono::seconds(6);
            HANDLE h = CreateFileW((f.c.source / "lsc.txt").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                   0, nullptr);
            CHECK(h != INVALID_HANDLE_VALUE);
            auto t = f.engine->run();
            CloseHandle(h);
            CHECK(t.pending == 1);
            CHECK(f.ftp->uploads == 0);
        });
        test("unavailable spool fails safely", [] {
            Fixture f;
            f.source("abc");
            f.engine->run();
            f.time += std::chrono::seconds(6);
            fs::remove(f.root / "data/spool");
            atomicWrite(f.root / "data/spool", "occupied", false);
            CHECK(f.engine->run().pending == 1);
            CHECK(f.ftp->uploads == 0);
            CHECK(fs::file_size(f.c.source / "lsc.txt") == 3);
        });
        test("logs redact URL userinfo and password assignments", [] {
            Fixture f;
            Logger logger(f.root / "logging");
            logger.write("WARN", "test", "ftp://user:secret@host/path");
            logger.write("WARN", "test", "password=another-secret");
            auto p = f.root / "logging/logs" / fs::u8path(utcNow().substr(0, 10) + ".log");
            std::ifstream in(p);
            std::string log((std::istreambuf_iterator<char>(in)), {});
            CHECK(log.find("secret") == std::string::npos);
            CHECK(log.find("redacted") != std::string::npos);
        });
        std::cout << passed << " tests passed\n";
        return 0;
    } catch (...) {
        return 1;
    }
}
