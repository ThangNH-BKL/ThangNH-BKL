#include "Core.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <ctime>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <windows.h>
namespace camera {
namespace {
void validateEntry(const Json &e) {
    if (!e.is_object() || !e.at("size").is_number_unsigned() || !e.at("mtime").is_number_integer() ||
        !std::regex_match(e.at("hash").get<std::string>(), std::regex("[0-9a-f]{64}")))
        throw Error(ErrorKind::Persistence, "Invalid checkpoint entry");
    safeRemote(e.at("final"));
    if (e.at("key").get<std::string>().empty())
        throw Error(ErrorKind::Persistence, "Empty checkpoint key");
    e.at("identity").get<std::string>();
    e.at("confirmed").get<std::string>();
}
bool ownedBackup(const std::string &final, const std::string &path) {
    if (path.rfind(final + "^", 0) != 0)
        return false;
    auto suffix = path.substr(final.size() + 1);
    return std::regex_match(suffix, std::regex("[0-9]{14}\\.[a-f0-9]{32}"));
}
void validateOperation(const Json &op) {
    validateEntry(op);
    auto final = op.at("final").get<std::string>(), temp = op.at("temp").get<std::string>(),
         rollback = op.at("rollback").get<std::string>(), id = op.at("id").get<std::string>();
    safeRemote(temp);
    safeRemote(rollback);
    auto slash = final.rfind('/');
    if (slash == final.size() - 1)
        throw Error(ErrorKind::Persistence, "Journal final is not a file");
    auto dir = final.substr(0, slash + 1), name = final.substr(slash + 1),
         prefix = dir + "." + name + ".uploading.";
    if (temp.rfind(prefix, 0) != 0 || temp.size() <= prefix.size() + id.size() + 1 ||
        temp.substr(temp.size() - id.size() - 1) != "." + id)
        throw Error(ErrorKind::Persistence, "Invalid journal temporary ownership");
    auto station = temp.substr(prefix.size(), temp.size() - prefix.size() - id.size() - 1);
    if (!std::regex_match(station, std::regex("[A-Za-z0-9_-]{1,64}")))
        throw Error(ErrorKind::Persistence, "Invalid journal station");
    if (!op.at("destructive").is_boolean() ||
        (!op.at("destructive").get<bool>() && rollback != dir + "." + name + ".rollback." + id) ||
        (op.at("destructive").get<bool>() &&
         (!ownedBackup(final, rollback) || rollback.substr(rollback.size() - id.size()) != id)))
        throw Error(ErrorKind::Persistence, "Invalid journal rollback ownership");
    auto oldHash = op.at("old_hash").get<std::string>();
    if ((!oldHash.empty() && !std::regex_match(oldHash, std::regex("[0-9a-f]{64}"))) ||
        !op.at("old_size").is_number_unsigned() || op.at("job").get<std::string>().empty())
        throw Error(ErrorKind::Persistence, "Invalid journal revision metadata");
    static const std::set<std::string> phases = {
        "planned",       "staged",    "verified",        "moving_old",    "old_moved", "publishing_new",
        "new_published", "confirmed", "cleanup_pending", "restoring_old", "aborted",   "done"};
    if (!phases.count(op.at("phase").get<std::string>()))
        throw Error(ErrorKind::Persistence, "Unknown journal phase");
}
Json readState(const fs::path &p) {
    if (fs::file_size(p) > 64 * 1024 * 1024)
        throw Error(ErrorKind::Persistence, "State exceeds 64 MiB limit; archive job metadata offline");
    std::ifstream f(p);
    Json j = Json::parse(f);
    if (j.contains("schema_version") && j.at("schema_version").is_number_integer() &&
        j.at("schema_version") != 1)
        throw Error(ErrorKind::UnsupportedVersion,
                    "Unsupported state schema version; original state preserved");
    if (j.at("schema_version") != 1 || !j.at("files").is_object() || j["files"].size() > 100000 ||
        !j.at("backups").is_array())
        throw Error(ErrorKind::Persistence, "Unsupported state schema or resource limit");
    for (auto &i : j["files"].items()) {
        validateEntry(i.value());
        if (i.key() != i.value()["key"])
            throw Error(ErrorKind::Persistence, "Checkpoint key mismatch");
    }
    for (auto &b : j["backups"]) {
        safeRemote(b.at("path"));
        if (!std::regex_match(b.at("hash").get<std::string>(), std::regex("[0-9a-f]{64}")))
            throw Error(ErrorKind::Persistence, "Invalid backup record");
        b.at("created").get<std::string>();
        b.at("key").get<std::string>();
    }
    return j;
}
std::string fingerprint(const fs::path &p) {
    HANDLE h =
        CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw Error(ErrorKind::Local, "Source attributes unavailable");
    BY_HANDLE_FILE_INFORMATION i{};
    bool ok = GetFileInformationByHandle(h, &i) != 0;
    CloseHandle(h);
    if (!ok || (i.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error(ErrorKind::Local, "Reparse points are not supported");
    return std::to_string(i.dwVolumeSerialNumber) + ":" + std::to_string(i.nFileIndexHigh) + ":" +
           std::to_string(i.nFileIndexLow);
}
bool matchOne(const std::string &s, const std::string &p) {
    size_t i = 0, j = 0, star = std::string::npos, mark = 0;
    while (i < s.size()) {
        if (j < p.size() && (p[j] == '?' || tolower((unsigned char)p[j]) == tolower((unsigned char)s[i]))) {
            ++i;
            ++j;
        } else if (j < p.size() && p[j] == '*') {
            star = j++;
            mark = i;
        } else if (star != std::string::npos) {
            j = star + 1;
            i = ++mark;
        } else
            return false;
    }
    while (j < p.size() && p[j] == '*')
        ++j;
    return j == p.size();
}
bool matches(const std::string &s, const std::string &patterns) {
    size_t start = 0;
    do {
        auto end = patterns.find(';', start);
        if (matchOne(s, patterns.substr(start, end - start)))
            return true;
        if (end == std::string::npos)
            break;
        start = end + 1;
    } while (true);
    return false;
}
struct RemoveLocal {
    fs::path p;
    ~RemoveLocal() {
        std::error_code ec;
        fs::remove(p, ec);
    }
};
} // namespace
Store::Store(fs::path root) : root_(std::move(root)) {
    fs::create_directories(root_ / "spool");
    auto p = root_ / "sync_state.json";
    if (fs::exists(p)) {
        try {
            state = readState(p);
        } catch (...) {
            try {
                throw;
            } catch (const Error &e) {
                if (e.kind == ErrorKind::UnsupportedVersion)
                    throw;
            } catch (...) {
            }
            auto q = p;
            q += L".corrupt." + fs::u8path(uniqueId()).wstring();
            fs::rename(p, q);
            auto bak = p;
            bak += L".bak";
            if (fs::exists(bak)) {
                try {
                    state = readState(bak);
                    atomicWrite(p, state.dump(2), false);
                } catch (...) {
                    throw Error(ErrorKind::Persistence,
                                "State and recovery copy invalid; preserve files and reconcile manually");
                }
            } else
                throw Error(ErrorKind::Persistence,
                            "State quarantined; remote reconciliation required before restart");
        }
    } else if (fs::exists(root_ / "operations.jsonl")) { // Missing checkpoint is not authority to replace
                                                         // anything; remote hashes are mandatory.
        state["reconciliation_required"] = true;
    }
}
void Store::save() {
    if (state["files"].size() > 100000)
        throw Error(ErrorKind::Persistence,
                    "100000 file state limit reached; split source into jobs offline");
    auto serialized = state.dump(2);
    if (serialized.size() > 64 * 1024 * 1024)
        throw Error(ErrorKind::Persistence,
                    "State exceeds 64 MiB limit; preserve journal and archive metadata offline");
    atomicWrite(root_ / "sync_state.json", serialized);
}
void Store::journal(Json op, const std::string &phase) {
    op["phase"] = phase;
    op["schema_version"] = 1;
    op["time"] = utcNow();
    auto serialized = op.dump() + "\n";
    auto path = root_ / "operations.jsonl";
    if ((fs::exists(path) ? fs::file_size(path) : 0) + serialized.size() > 64 * 1024 * 1024)
        throw Error(ErrorKind::Persistence,
                    "Journal capacity reached; preserve transactions and compact terminal history offline");
    durableAppend(path, serialized);
}
std::map<std::string, Json> Store::operations() {
    std::map<std::string, Json> result;
    auto path = root_ / "operations.jsonl";
    if (!fs::exists(path))
        return result;
    if (fs::file_size(path) > 64 * 1024 * 1024)
        throw Error(ErrorKind::Persistence, "Journal exceeds safety bound; supervised compaction required");
    std::ifstream f(path, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (f.eof()) { // Never append behind a partial record; quarantine and retain complete transactions.
            auto raw = root_ / fs::u8path("operations.partial." + uniqueId());
            atomicWrite(raw, line, false);
            std::ifstream original(path, std::ios::binary);
            std::string all((std::istreambuf_iterator<char>(original)), {});
            auto end = all.rfind('\n');
            original.close();
            f.close();
            atomicWrite(path, end == std::string::npos ? "" : all.substr(0, end + 1));
            break;
        }
        try {
            auto j = Json::parse(line);
            if (j.at("schema_version") != 1)
                throw std::runtime_error("schema");
            auto id = j.at("id").get<std::string>();
            if (!std::regex_match(id, std::regex("[a-f0-9]{32}")))
                throw std::runtime_error("id");
            safeRemote(j.at("final"));
            safeRemote(j.at("temp"));
            safeRemote(j.at("rollback"));
            validateOperation(j);
            if (result.count(id))
                for (auto field : {"job", "key", "final", "temp", "rollback", "size", "hash", "mtime",
                                   "identity", "old_hash", "old_size", "destructive"})
                    if (result.at(id).at(field) != j.at(field))
                        throw Error(ErrorKind::Persistence, "Journal transaction identity changed");
            result[id] = j;
        } catch (...) {
            throw Error(ErrorKind::Persistence, "Invalid complete journal record; remote recovery blocked");
        }
    }
    return result;
}
Engine::Engine(Config c, IFtpClient &f, Store &s, std::atomic_bool &cancel, Notify n,
               std::function<std::chrono::steady_clock::time_point()> clock)
    : config_(std::move(c)), ftp_(f), store_(s), cancel_(cancel), notify_(std::move(n)),
      clock_(std::move(clock)) {
    config_.validate();
}
Snapshot Engine::snapshot(const fs::path &p) {
    auto identity = fingerprint(p);
    auto size = fs::file_size(p);
    auto mtime =
        std::chrono::duration_cast<std::chrono::microseconds>(fs::last_write_time(p).time_since_epoch())
            .count();
    auto token = identity + ":" + std::to_string(size) + ":" + std::to_string(mtime);
    auto now = clock_();
    auto &seen = observed_[p.u8string()];
    if (seen.first != token) {
        seen = {token, now};
        throw Error(ErrorKind::Local, "Waiting for stable source");
    }
    if (now - seen.second < std::chrono::seconds(config_.stableSeconds))
        throw Error(ErrorKind::Local, "Waiting for stable source");
    if (size == 0 && !config_.allowEmpty)
        throw Error(ErrorKind::Conflict, "Empty source is disabled");
    auto spool = store_.root() / "spool" / fs::u8path(uniqueId() + ".snapshot");
    RemoveLocal cleanup{spool};
    HANDLE in = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (in == INVALID_HANDLE_VALUE)
        throw Error(ErrorKind::Local, "Source busy or unreadable");
    HANDLE out = CreateFileW(spool.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        CloseHandle(in);
        throw Error(ErrorKind::Local, "Cannot create staging file; check free disk space");
    }
    bool ok = true;
    uint64_t copied = 0;
    char b[65536];
    while (ok) {
        if (cancel_) {
            ok = false;
            break;
        }
        DWORD n = 0;
        if (!ReadFile(in, b, sizeof(b), &n, nullptr)) {
            ok = false;
            break;
        }
        if (!n)
            break;
        DWORD w = 0;
        ok = WriteFile(out, b, n, &w, nullptr) && w == n;
        copied += n;
        if (copied > size)
            ok = false;
    }
    ok = ok && FlushFileBuffers(out);
    CloseHandle(in);
    CloseHandle(out);
    if (!ok)
        throw Error(cancel_ ? ErrorKind::Cancelled : ErrorKind::Local,
                    "Snapshot interrupted; source preserved");
    auto hash = hashFile(spool, &cancel_);
    if (copied != size || fs::file_size(p) != size ||
        std::chrono::duration_cast<std::chrono::microseconds>(fs::last_write_time(p).time_since_epoch())
                .count() != mtime ||
        fingerprint(p) != identity || hashFile(p, &cancel_) != hash)
        throw Error(ErrorKind::Local, "Source changed during snapshot; retry later");
    cleanup.p.clear();
    return {spool, size, mtime, identity, hash};
}
void Engine::verify(const std::string &p, uint64_t size, const std::string &hash, bool full) {
    auto info = ftp_.stat(p);
    if (!info || info->size != size)
        throw Error(ErrorKind::Integrity, "Remote SIZE verification failed; nothing committed");
    if (full && ftp_.hash(p) != hash)
        throw Error(ErrorKind::Integrity, "Remote SHA256 verification failed; preserve all copies");
}
void Engine::checkpoint(const Json &op) {
    Json e = {{"key", op.at("key")},     {"final", op.at("final")},       {"size", op.at("size")},
              {"mtime", op.at("mtime")}, {"identity", op.at("identity")}, {"hash", op.at("hash")},
              {"confirmed", utcNow()},   {"status", "confirmed"}};
    store_.state["files"][op.at("key").get<std::string>()] = e;
    store_.save();
}
void Engine::recover() {
    for (auto &pair : store_.operations()) {
        auto op = pair.second;
        std::string phase = op.at("phase");
        if (phase == "done" || phase == "aborted")
            continue;
        if (op.at("job") != config_.jobKey())
            throw Error(ErrorKind::Conflict, "Incomplete operation belongs to another configuration; restore "
                                             "its settings before recovery");
        auto finalPath = op.at("final").get<std::string>(), job = op.at("job").get<std::string>(),
             namespacePrefix = config_.destination("");
        if (finalPath.rfind(namespacePrefix, 0) != 0 ||
            op.at("key") != job + "|" + finalPath.substr(namespacePrefix.size()) + "|" + finalPath)
            throw Error(ErrorKind::Conflict, "Journal paths/key escape configured job namespace");
        std::string final = op.at("final"), tmp = op.at("temp"), old = op.at("rollback"),
                    hash = op.at("hash"), oldhash = op.value("old_hash", "");
        auto f = ftp_.stat(final), r = ftp_.stat(old), t = ftp_.stat(tmp);
        if (f && ftp_.hash(final) == hash) {
            verify(final, op.at("size"), hash, true);
            checkpoint(op);
            store_.journal(op, "confirmed");
            if (r) {
                if (oldhash.empty() || ftp_.hash(old) != oldhash)
                    throw Error(ErrorKind::Conflict, "Rollback changed; recovery requires review");
                if (op.value("destructive", false)) {
                    bool recorded = false;
                    for (auto &b : store_.state["backups"])
                        if (b["path"] == old)
                            recorded = true;
                    if (!recorded) {
                        store_.state["backups"].push_back(
                            {{"path", old}, {"hash", oldhash}, {"key", op["key"]}, {"created", utcNow()}});
                        store_.save();
                    }
                } else {
                    store_.journal(op, "cleanup_pending");
                    ftp_.remove(old);
                }
            }
            if (t) {
                if (ftp_.hash(tmp) != hash)
                    throw Error(ErrorKind::Conflict, "Temporary object is ambiguous; preserve it");
                ftp_.remove(tmp);
            }
            store_.journal(op, "done");
            continue;
        }
        if (!f && r && !oldhash.empty() && ftp_.hash(old) == oldhash) {
            store_.journal(op, "restoring_old");
            ftp_.rename(old, final);
            verify(final, op.at("old_size"), oldhash, true);
            if (t)
                op["retained_temp"] = true;
            store_.journal(op, "aborted");
            continue;
        }
        if ((!f && oldhash.empty() && !r) ||
            (f && !oldhash.empty() && ftp_.hash(final) == oldhash &&
             !r)) { // No destructive transition happened. Only delete a fully verified known staging object.
            if (t) {
                // The final is proven unchanged/absent and no rollback exists. Preserve all
                // staging bytes (including interrupted partial transfers) under their unique
                // journaled name. A new attempt must use another UUID; never reuse this path.
                op["retained_temp"] = true;
                if (notify_)
                    notify_("Retained interrupted staging object for review: " + tmp);
            }
            store_.journal(op, "aborted");
            continue;
        }
        throw Error(ErrorKind::Conflict,
                    "Ambiguous publish recovery: preserve final/temp/rollback and review journal");
    }
}
void Engine::syncFile(const fs::path &p, Totals &totals, bool destructive) {
    auto s = snapshot(p);
    RemoveLocal cleanup{s.path};
    std::string relative = fs::relative(p, config_.source).generic_u8string();
    auto final = config_.destination(relative), key = config_.jobKey() + "|" + relative + "|" + final;
    ftp_.prepareFile(final);
    auto remote = ftp_.stat(final);
    std::string remoteHash = remote ? ftp_.hash(final) : "";
    auto &files = store_.state["files"];
    Json prior = files.contains(key) ? files[key] : Json();
    Json op = {{"id", uniqueId()},
               {"job", config_.jobKey()},
               {"key", key},
               {"final", final},
               {"size", s.size},
               {"mtime", s.mtime},
               {"identity", s.identity},
               {"hash", s.hash},
               {"confirmed", utcNow()},
               {"old_hash", remoteHash},
               {"old_size", remote ? remote->size : 0},
               {"destructive", destructive}};
    if (!destructive && !prior.is_null()) {
        if (!remote || remoteHash != prior.at("hash"))
            throw Error(ErrorKind::Conflict, "Remote revision differs from confirmed checkpoint");
        if (s.size < prior.at("size").get<uint64_t>() ||
            s.identity != prior.at("identity").get<std::string>())
            throw Error(ErrorKind::Conflict, "Source truncated, reset or identity changed");
    }
    if (remote && remoteHash == s.hash) {
        checkpoint(op);
        ++totals.skipped;
        return;
    }
    if (!destructive) {
        if (!prior.is_null()) {
            if (!remote || remoteHash != prior["hash"])
                throw Error(ErrorKind::Conflict, "Remote revision differs from confirmed checkpoint");
            if (s.size < prior["size"].get<uint64_t>() || s.identity != prior["identity"].get<std::string>())
                throw Error(ErrorKind::Conflict, "Source truncated, reset or identity changed");
            auto previousSize = prior["size"].get<uint64_t>();
            if (hashFile(s.path, &cancel_, previousSize) != prior["hash"])
                throw Error(ErrorKind::Conflict, "Source is not an append of the confirmed revision");
        } else if (remote)
            throw Error(ErrorKind::Conflict, "Unknown existing remote file; manual decision required");
    }
    if (remote && !config_.gapConsent)
        throw Error(ErrorKind::Conflict,
                    "Two-step publish has a visibility gap; explicit acceptance required");
    auto slash = final.rfind('/');
    auto dir = final.substr(0, slash + 1), name = final.substr(slash + 1), id = op["id"].get<std::string>();
    std::string tmp = dir + "." + name + ".uploading." + config_.station + "." + id;
    std::string rollback = dir + "." + name + ".rollback." + id;
    if (destructive) {
        auto stamp = utcNow();
        stamp.erase(std::remove_if(stamp.begin(), stamp.end(),
                                   [](char c) { return c == '-' || c == ':' || c == 'T' || c == 'Z'; }),
                    stamp.end());
        rollback = dir + name + "^" + stamp + "." + id;
    }
    op["temp"] = tmp;
    op["rollback"] = rollback;
    if (ftp_.stat(tmp) || ftp_.stat(rollback))
        throw Error(ErrorKind::Conflict, "Unique staging name already exists");
    store_.journal(op, "planned");
    if (notify_)
        notify_("Uploading: " + relative);
    ftp_.upload(s.path, tmp);
    store_.journal(op, "staged");
    if (notify_)
        notify_("Verifying: " + relative);
    // Even legacy Size profiles must prove staged bytes before replacing a valid final.
    verify(tmp, s.size, s.hash, true);
    store_.journal(op, "verified");
    if (cancel_)
        throw Error(ErrorKind::Cancelled, "Cancelled before publish");
    // Re-read immediately before destructive operations. FTP has no compare-and-swap; exclusive namespace
    // remains mandatory.
    auto current = ftp_.stat(final);
    if (bool(current) != bool(remote) || (current && ftp_.hash(final) != remoteHash))
        throw Error(ErrorKind::Conflict, "Remote changed before publish");
    if (remote) {
        store_.journal(op, "moving_old");
        ftp_.rename(final, rollback);
        store_.journal(op, "old_moved");
    }
    store_.journal(op, "publishing_new");
    ftp_.rename(tmp, final);
    store_.journal(op, "new_published");
    verify(final, s.size, s.hash, true);
    checkpoint(op);
    store_.journal(op, "confirmed");
    if (remote) {
        if (destructive) {
            store_.state["backups"].push_back(
                {{"path", rollback}, {"hash", remoteHash}, {"key", key}, {"created", utcNow()}});
            store_.save();
        } else {
            store_.journal(op, "cleanup_pending");
            if (ftp_.hash(rollback) != remoteHash)
                throw Error(ErrorKind::Conflict, "Rollback changed before cleanup");
            ftp_.remove(rollback);
        }
    }
    store_.journal(op, "done");
    ++totals.uploaded;
    totals.bytes += s.size;
}
void Engine::retention() {
    if (!config_.retention)
        return;
    auto &backups = store_.state["backups"];
    std::map<std::string, int> count;
    for (auto i = backups.size(); i > 0; --i) {
        auto &b = backups[i - 1];
        auto key = b.at("key").get<std::string>();
        if (key.rfind(config_.jobKey() + "|", 0) != 0)
            continue;
        int rank = ++count[key]; // Lexical UTC cutoff is computed with a monotonic age-independent system
                                 // time conversion.
        auto created = b.at("created").get<std::string>();
        std::tm tm{};
        std::istringstream in(created);
        in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if (in.fail())
            throw Error(ErrorKind::Persistence, "Invalid backup timestamp");
        auto seconds = _mkgmtime(&tm);
        bool aged = std::time(nullptr) - seconds > int64_t(config_.backupDays) * 86400;
        if (rank <= config_.backupCount && !aged)
            continue;
        std::string path = b.at("path");
        if (!store_.state["files"].contains(key))
            throw Error(ErrorKind::Conflict, "Retention lacks confirmed final checkpoint; backups preserved");
        auto checkpoint = store_.state["files"][key];
        auto currentFinal = checkpoint.at("final").get<std::string>();
        if (currentFinal.rfind(config_.destination(""), 0) != 0 || !ownedBackup(currentFinal, path))
            throw Error(ErrorKind::Conflict, "Backup ownership/path mismatch; retention stopped");
        verify(currentFinal, checkpoint.at("size"), checkpoint.at("hash"), true);
        auto r = ftp_.stat(path);
        if (r) {
            if (ftp_.hash(path) != b.at("hash"))
                throw Error(ErrorKind::Conflict, "Owned backup changed; retention stopped");
            ftp_.remove(path);
        }
        backups.erase(backups.begin() + (i - 1));
        store_.save();
    }
}
Totals Engine::run(const std::optional<std::string> &approved) {
    std::unique_lock<std::mutex> lock(runMutex_, std::try_to_lock);
    if (!lock.owns_lock())
        throw Error(ErrorKind::Conflict, "Job already running");
    Totals totals;
    ftp_.prepare();
    recover();
    std::vector<fs::path> candidates;
    auto accept = [&](const fs::directory_entry &e) {
        auto attrs = GetFileAttributesW(e.path().c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            return;
        if (e.is_regular_file() && matches(e.path().filename().u8string(), config_.include) &&
            !matches(e.path().filename().u8string(), config_.exclude)) {
            if (candidates.size() >= 100000)
                throw Error(ErrorKind::Configuration, "Scan exceeds 100000 candidate limit");
            candidates.push_back(e.path());
        }
    };
    auto source = fs::weakly_canonical(config_.source), data = fs::weakly_canonical(store_.root());
    for (auto current = source; !current.empty();) {
        std::error_code ec;
        if (fs::equivalent(current, data, ec))
            throw Error(ErrorKind::Configuration, "Source may not be in application storage");
        auto parent = current.parent_path();
        if (parent == current)
            break;
        current = parent;
    }
    if (config_.recursive) {
        for (auto i = fs::recursive_directory_iterator(source); i != fs::recursive_directory_iterator();
             ++i) {
            auto attrs = GetFileAttributesW(i->path().c_str());
            if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) || fs::equivalent(i->path(), data)) {
                if (i->is_directory())
                    i.disable_recursion_pending();
                continue;
            }
            accept(*i);
        }
    } else
        for (auto &e : fs::directory_iterator(source))
            accept(e);
    if (approved) {
        safeRemote(config_.destination(*approved));
        bool found = false;
        for (auto &p : candidates)
            if (fs::relative(p, source).generic_u8string() == *approved)
                found = true;
        if (!found)
            throw Error(ErrorKind::Configuration, "Selected replacement file is not an eligible source file");
    }
    std::set<std::string> present;
    for (auto &p : candidates)
        present.insert(p.u8string());
    for (auto it = observed_.begin(); it != observed_.end();)
        if (!present.count(it->first))
            it = observed_.erase(it);
        else
            ++it;
    for (auto &p : candidates) {
        if (cancel_)
            break;
        if (approved && fs::relative(p, source).generic_u8string() != *approved)
            continue;
        ++totals.scanned;
        auto report = [&](const std::string &result, const std::string &reason) {
            if (!notify_)
                return;
            auto relative = fs::relative(p, source).generic_u8string();
            auto final = config_.destination(relative);
            auto key = config_.jobKey() + "|" + relative + "|" + final;
            std::string confirmed;
            auto &files = store_.state["files"];
            if (files.contains(key))
                confirmed = files[key].value("confirmed", "");
            std::error_code sizeError;
            auto currentSize = fs::file_size(p, sizeError);
            Json row = {{"source", p.u8string()},
                        {"relative", relative},
                        {"size", sizeError ? Json() : Json(currentSize)},
                        {"remote", final},
                        {"confirmed", confirmed},
                        {"result", result},
                        {"error", reason},
                        {"mtime", sourceModifiedUtc(p)}};
            notify_("@file " + row.dump());
        };
        for (int attempt = 0;; ++attempt) {
            try {
                auto uploads = totals.uploaded;
                syncFile(p, totals, approved.has_value());
                report(totals.uploaded > uploads ? "Uploaded" : "Skipped/adopted", "");
                break;
            } catch (const Error &e) {
                if (notify_)
                    notify_(std::string(e.kind == ErrorKind::Conflict ? "Conflict: " : "Pending/Failed: ") +
                            p.filename().u8string() + " - " + e.what());
                if (e.kind == ErrorKind::Authentication || e.kind == ErrorKind::TLS ||
                    e.kind == ErrorKind::Configuration || e.kind == ErrorKind::Persistence)
                    throw;
                if (e.kind == ErrorKind::Transient && attempt < config_.retries) {
                    ++totals.retries;
                    std::mt19937 rng(std::random_device{}());
                    int delay = std::min(300, 1 << std::min(attempt, 8)) + int(rng() % 3);
                    for (int tick = 0; tick < delay * 10 && !cancel_; ++tick)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    recover();
                    continue;
                }
                if (!cancel_ && e.kind != ErrorKind::Local) {
                    // A failed second rename can leave final absent. Reconcile now, not at
                    // the next timer tick; recovery itself fails closed if evidence conflicts.
                    recover();
                }
                report(e.kind == ErrorKind::Conflict ? "Conflict"
                       : e.kind == ErrorKind::Local  ? "Pending"
                                                     : "Failed",
                       e.what());
                if (e.kind == ErrorKind::Conflict)
                    ++totals.conflicts;
                else if (e.kind == ErrorKind::Local)
                    ++totals.pending;
                else
                    ++totals.failed;
                break;
            } catch (const std::exception &) {
                if (!cancel_)
                    recover();
                ++totals.failed;
                if (notify_)
                    notify_("Failed: local filesystem or persistence error");
                break;
            }
        }
        // Never begin another publish while an operation is unresolved.
        for (auto &x : store_.operations())
            if (x.second["phase"] != "done" && x.second["phase"] != "aborted")
                return totals;
    }
    retention();
    return totals;
}
Scheduler::~Scheduler() {
    stop();
}
void Scheduler::stop() {
    stop_ = true;
    if (worker_.joinable())
        worker_.join();
}
void Scheduler::launch(std::function<void()> job, int minutes) {
    if (worker_.joinable())
        throw Error(ErrorKind::Configuration, "Scheduler already launched");
    worker_ = std::thread([this, job, minutes] {
        auto next = std::chrono::steady_clock::now();
        while (!stop_) {
            bool manual = now_.exchange(false);
            if (manual || (!paused_ && std::chrono::steady_clock::now() >= next)) {
                try {
                    job();
                } catch (...) { /* UI job reports errors. Scheduler remains alive. */
                }
                next = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}
} // namespace camera
