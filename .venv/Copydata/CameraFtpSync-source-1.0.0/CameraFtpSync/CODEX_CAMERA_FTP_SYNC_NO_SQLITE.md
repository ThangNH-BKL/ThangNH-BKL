# CODEX IMPLEMENTATION SPECIFICATION
# Standalone Camera Data FTP Sync Manager (Windows)

**Document version:** 3.0 — standalone application, file-based persistence only (replaces v2.0)  
**Target:** A separate Windows desktop application that synchronizes production-data files from a user-selected local folder to a user-configured FTP/FTPS server.  
**Required stack:** **C++17, Microsoft Foundation Classes (MFC), Visual Studio 2022, libcurl, Windows built-in APIs, INI + JSON + TXT files**. **Do not use SQLite, SQL Server, or any database engine.** Keep the data-processing engine independent from MFC to enable automated testing. If the development environment cannot compile MFC, still implement and test the platform-independent core, then clearly document the pending Windows GUI build step; do not silently replace the requested application with a CLI.  
**Language of interface:** Vietnamese as default, English labels/tooltips optional.  
**Absolute priority:** No data loss or unexpected overwrite > verifiable consistency > recovery > operational reliability > network efficiency > appearance.

---

## 0. EXACT INSTRUCTION TO CODEX

Build a **new, independent Windows application**, not a plugin and not a modification of the existing camera production/data-collection software. Source files are produced by another program; the new application may **read** them but must never alter, truncate, lock exclusively for long periods, or delete them.

Implement real source code, production-oriented error handling, a user-friendly MFC GUI, reliable FTP/FTPS synchronization, automated tests, documentation, and a distributable build configuration. Do not stop at a design document, pseudocode, or GUI mockup.

**The user MUST be able to manually enter and edit all of the following from the GUI (no hardcoding):**

1. Local source folder containing camera production data; browse button and editable text box.
2. Remote FTP/FTPS host or URL, port, remote destination directory; credentials (username and password).
3. Synchronization interval (default 5 minutes; selectable 1/5/10/15/30/60 minutes and custom 1–1440 minutes).
4. File inclusion pattern, e.g. `*.txt` or `lsc*.txt`; recursion on/off; source file stability interval.
5. Connection security mode, retry settings, backup/retention options, destination layout/station identifier.

The user must be able to **Test Connection**, **Save Settings**, **Sync Now**, **Start**, **Pause**, and **View Log/History**. Provide clear, actionable error messages. Never require editing source code or a configuration file to change the FTP account, paths, or interval.

First inspect the repository and available toolchains, create a new solution/project in its own directory, implement iteratively, and actually run all feasible tests. Explicitly exclude SQLite and all database dependencies from the project, installer, and runtime. Report build/test results honestly. If a remote FTP test server is unavailable, use a local integration-test FTP server or mock. Do not claim live server validation without access.

---

## 1. BUSINESS BACKGROUND

Production Camera Modules generate text-based data that may accumulate in an actively written file. For illustration: 1 module → 1 KB, 2 modules → 2 KB, and 10,000 modules → 10,000 KB. The original proposed mechanism checks the remote file's size and renames the old remote file before copying a larger file, potentially after every unit. This can repeatedly transmit identical old data, produce a large number of backups, and lose data on failure.

**New target behavior:** a dedicated app checks periodically (typically every 5 or 10 minutes), transfers only data files whose content has actually changed, and publishes each replacement only after the upload has completed and passed the configured verification. It must not create a permanent backup after every normal update. It must never trust file size alone to declare two files equivalent, and it must never silently overwrite a remote file belonging to another station/source.

**Important scope decision:** The MVP uses **periodic full-file snapshot upload only when changed**, NOT unsafe FTP `APPE`/in-place incremental append. Incremental transfer is a future enhancement only if correctness can be proven end to end. This is intentional: correctness and resilience come before maximum bandwidth efficiency.

---

## 2. APPLICATION MODE AND DEPLOYMENT

- Target Windows 10/11 x64, with independent executable(s). Existing Camera production software must not be changed.
- GUI can minimize to the Windows notification area (system tray). **Closing the window using X may minimize to tray** while sync continues; add explicit `Exit` action, clearly explaining that Exit stops synchronization.
- Option `Start with Windows` for the current account; disabled by default until user explicitly enables it. No hidden persistence. Show whether sync is currently running.
- Core worker runs in background threads, not the MFC UI thread. No overlapping runs for the same sync job.
- Clean cancellation and shutdown: allow an active upload to finish or cancel safely; never publish an incomplete upload.
- Optional later phase: Windows Service + separate GUI to keep syncing without a logged-in session. **Do not incorrectly claim the tray app continues when Windows is logged out or the executable exits.**
- Provide x64 Release build steps, versioned dependencies/build reproducibility, executable/config locations and optional installer packaging. No need for admin rights for normal operation.

---

## 3. GUI REQUIREMENTS (USER-EDITABLE)

### 3.1 Main screen: Connection and folder settings

All inputs should be visible and editable, with Browse/Test buttons and inline validation:

| Label (Vietnamese) | Control | Example / behavior |
|---|---|---|
| `Thư mục nguồn (Local Data Folder)` | Editable path + Browse Folder | `D:\\CameraData\\` |
| `Bộ lọc file` | Text | `lsc*.txt` (or `*.txt`) |
| `Bao gồm thư mục con` | Checkbox | Off by default |
| `Máy chủ FTP` | Text host / optionally paste full FTP URL | `ftp.company.local`; parse URL into host/path when provided |
| `Giao thức bảo mật` | Selector | `FTPS Explicit TLS (recommended)`, `FTPS Implicit TLS`, `Plain FTP (insecure)` |
| `Cổng` | Integer | Typical 21 for FTP/explicit FTPS, 990 for implicit FTPS; user-editable |
| `Tài khoản FTP` | Text | User-supplied username |
| `Mật khẩu FTP` | Masked password | Never display plain text in logs |
| `Lưu mật khẩu trên máy này` | Checkbox | Off unless user opts in; use Windows Credential Manager/DPAPI |
| `Thư mục đích trên FTP` | Text | `/production/camera/` |
| `Mã máy / Station ID` | Text | Optional but strongly encouraged (`PC01`) |
| `Tách thư mục theo Station ID` | Checkbox | On by default for multi-PC safety, with preview of exact remote path |
| `Test Connection` | Button | Verify login and access, report server capabilities without altering production files |
| `Kiểm tra quyền ghi & đổi tên` | Explicit opt-in test | Create/rename/delete uniquely named temporary test file only; remove after test; warn if cleanup fails |

URL behavior: allow the operator to paste `ftp://hostname:21/path/` or `ftps://hostname/path/`; show resulting host/port/mode/path fields for review. **Do not encourage or persist passwords embedded in URLs.** Do not equate FTP, FTPS, and SFTP; SFTP is a different protocol and can be added later as its own adapter, not mislabeled as FTPS.

### 3.2 Main screen: Sync policy

- `Chu kỳ đồng bộ`: default 5 minutes; can select 10 minutes/custom as above.
- `Chỉ gửi file thay đổi`: On, non-disableable in normal mode.
- `Chờ file ổn định`: default 30 seconds, configurable 5–300 seconds (does not by itself guarantee source consistency; see snapshot algorithm).
- `Số lần thử lại`: e.g. 5, with exponential backoff + jitter; adjustable within safe limits.
- `Timeout`: connection and transfer timeouts; protect against hung sessions.
- `Xác minh sau upload`: Standard = remote file SIZE + optional supported server hash; High Integrity = remote read-back SHA-256 if server hash cannot be trusted/used. Label tradeoffs accurately.
- `Backup`: default `On conflict/manual destructive replacement only`; configurable retention e.g. last 3 and/or 7 days, applied only to files managed by this application, with explicit permission; never indiscriminately purge preexisting server files.
- `Mode`: normal automatic mode; manual conflict-resolution workflow (no unsafe automatic overwrite).
- File type exclusions: temporary/partial files (`*.tmp`, `*.part`, application spool/state files, etc.) configurable.
- Show a **read-only preview** mapping local path to final remote destination (including Station ID if enabled).

### 3.3 Status and history screen

- States: `Not configured`, `Disconnected`, `Ready`, `Running`, `Paused`, `Uploading`, `Verifying`, `Success`, `Retrying`, `Conflict`, `Failed`.
- Per-file listing: source, relative path, last change, local size, remote target, last sync time, result, error reason.
- Current progress (bytes, file count, throughput when known), next scheduled time, last success/error.
- Totals: files scanned, skipped, uploaded, failed, conflicts, bytes transferred, retries.
- Display persistent logs with date-time, severity, category, and selectable/exportable text. Protect passwords, tokens, URLs with userinfo, and raw production data in logs.
- Start / Pause / Resume / Sync Now / Open Log Folder / Save Settings.
- Make it obvious if the app is not running, not authenticated, or if data is not yet synchronized.

---

## 4. SYNC ALGORITHM — FUNCTIONAL REQUIREMENTS

### 4.1 Scheduling and scanning

1. After Start (and once valid configuration exists), schedule the first scan promptly and subsequent scans every configured interval.
2. Enumerate only the selected source directory, applying recursion and include/exclude patterns. Ignore app-managed temp/spool files and invalid symlinks/reparse points unless explicitly supported safely.
3. One running scan/upload at a time per job. A tick during an active run may be coalesced; do not launch concurrent uploads to the same destination.
4. Do not require changes to, or hooks from, the Camera Module production software.
5. If network unavailable, keep state and retry later; production file creation must not be blocked.

### 4.2 Safe snapshot of an actively written file

1. A file candidate should appear unchanged in size and last-write time over the stability window before considering a snapshot.
2. Copy the candidate into a dedicated local staging/spool directory using sharing modes compatible with the producer. Never upload directly from a file that is actively changing.
3. After local staging completes, re-stat the original (size, mtime, file identity if available); if changed, discard staged data and retry on a later cycle. Consider rehash/checks or robust snapshot semantics to cover writers updating content without metadata changes; document the platform limitations.
4. Compute SHA-256 of the staged immutable file. The source file remains untouched. Persist snapshot metadata and clean up only confirmed-unused local temp files.
5. If the producer locks the source exclusively or consistency cannot be established, skip/retry with visible status; never force access or falsely mark success.
6. Empty files: permit only according to explicit policy; do not overwrite a previously nonempty remote file with empty content automatically.

### 4.3 Change detection and checkpoints

- Persist each job's normalized source path, target path, local source identity, file size, mtime, SHA-256, confirmed remote revision, and last successful time in `sync_state.json` (versioned and atomically saved). Journal every remote publish/rollback step to a separate `operations.jsonl` BEFORE attempting a potentially destructive FTP rename. See Section 6.1 for safe file-based persistence.
- Cheap scan: size/mtime before calculating SHA-256; however if metadata changes unexpectedly or equivalence is uncertain, hash contents. Support `Force Verify` for a full content check.
- If staged SHA-256 matches the **last confirmed successful upload** AND the remote target is still verifiably the expected revision, SKIP; no FTP data transfer and no backup.
- Do not assume same size = same content. A file can change within the same size; provide periodic forced re-hash or detect using last-write time, and explicit verify functionality.
- For first run with an existing remote target, do not blindly replace. Detect/adopt only when a remote read-back or trustworthy server hash proves the target matches the local snapshot, or proves it is an exact prefix of an append-only source (if explicitly enabled); otherwise mark CONFLICT pending operator review.
- If source shrinks, is reset, rotated, overwritten or changes unexpectedly, protect both source and remote and surface a conflict (or treat a truly new rotated filename as a separate file). Never silently truncate a remote file.
- If the remote file changes outside the app or the manifest/checkpoint disagrees, flag CONFLICT; no automatic overwrite.

### 4.4 Remote path ownership and multi-PC safety

- Default remote mapping: `<user-entered-remote-root>/<station-id>/<source-relative-path>` when station separation is enabled.
- Allow turning off station separation only after a clear warning; exact destination preview required.
- Enforce unique remote destination per configured job on the same machine; reject overlapping automatic jobs that may race.
- **FTP does not provide general-purpose distributed transactions/CAS.** A simple lock file on FTP is not necessarily an atomic lock. Therefore do not claim safe concurrent multi-PC overwrites of one remote filename. Prefer physically isolated station namespaces. If the operator points two PCs at the same target, require a deliberate owner assignment or conflict protection; do not quietly choose a winner.
- Remote folder creation (`MKD`) must be limited to the configured root/station subtree, checked for path traversal and permission failures.

### 4.5 Safe FTP upload and publish

The application must use a real FTP/FTPS library (recommended libcurl) and implement explicit staged upload rather than writing into the final filename:

1. Establish authenticated TLS connection where available, with certificate and hostname verification ON. Passive mode configurable; provide meaningful status on firewalls/NAT and failures.
2. Query remote target metadata. If existing remote target is unexpected, conflict; if unchanged, skip.
3. Upload the local immutable snapshot to a **unique, same-directory remote temporary path** such as `.lsc.txt.uploading.<station-id>.<uuid>`.
4. Check transfer completion, bytes uploaded and remote `SIZE`. If High Integrity is enabled, obtain trustworthy remote content hash or download the staged file and compute SHA-256 to compare with the local snapshot (FTP hash commands may be unsupported; never pretend a `SIZE` check is cryptographic verification).
5. Publish **only after verification**. If the FTP server supports atomic replacement via rename and this behavior has been verified, use it. Otherwise implement a recoverable two-step sequence: rename existing final to unique rollback path, rename staged to final, and verify final. On any failure, restore old final when possible and flag the job for recovery/manual intervention.
6. **Do not claim unconditional atomicity on FTP.** FTP implementations differ: replacement rename may fail if destination exists; there may be a brief period without the final file in the two-step path. If the downstream reader requires zero-gap atomic replacement, expose incompatibility and require a compatible server-side mechanism.
7. Keep the former file only transiently as a rollback copy for an ordinary verified update; remove that transient copy after a fully confirmed commit when safe. A permanent timestamped backup is **not** created after every normal cycle.
8. For explicitly approved destructive replacement, archive the prior file by server rename to a backup name such as `lsc^yyyyMMdd-HHmmss.txt` (use 24-hour `HH`), with a collision-safe suffix where needed, then publish the new snapshot. Never overwrite an existing backup name.
9. On crash/restart, recover journaled in-progress operations: handle orphan `.uploading.*` and `.rollback.*` files without risking the only valid copy. Do not automatically delete unknown or foreign artifacts.
10. Update the atomic `sync_state.json` checkpoint **only after remote publish and required verification succeed**; mark the corresponding journal operation complete afterward. Use idempotency rules so a retry after a crash does not repeat a destructive operation.

### 4.6 What to do in ambiguous cases

| Scenario | Mandatory action |
|---|---|
| Source unchanged | Skip, no upload/backup |
| Source appended, remote known to be last published revision | Periodic full snapshot upload, staged and verified, publish with transient rollback |
| Source and remote have equal sizes but different hashes | Conflict, never assume identical |
| Remote larger than source / source truncated | Conflict, preserve both |
| Existing remote file unknown at initial setup | Verify/adopt if confidently identical; otherwise conflict |
| Upload disconnects halfway | Final remains valid if possible; retain safe state, clean or recover temp, retry with backoff |
| FTP server cannot rename | No unsafe direct-write fallback; show unsupported server capability, fail safely |
| FTP server lacks hash extension | Offer read-back SHA-256; size-only check clearly labeled lower assurance |
| Credentials invalid / TLS certificate invalid | Stop that attempt, show actionable error, never disable TLS verification silently |
| Multiple PCs point to one target | Prevent/flag collision; use unique station directory by default |
| Save password unchecked | Keep only in memory for the active session and prompt after restart |
| File actively changing or unreadable | Skip/retry later; do not upload an inconsistent snapshot |

---

## 5. FTP SECURITY AND SECRET HANDLING

- Support **FTPS Explicit TLS** as preferred, FTPS Implicit TLS if the server requires it, and optional legacy plaintext FTP **only with a prominent warning and explicit operator opt-in**. Do not call plain FTP secure.
- **SFTP is not FTP/FTPS**. Do not implement a fake SFTP mode. An actual SFTP adapter may be added as a later enhancement.
- Verify TLS certificate chain and hostname, no blanket `insecure` option in normal UI. Provide clear diagnosis for private-CA certificates and steps for installing/using trusted CA certificates.
- Username/password entered manually in GUI; masked password box; no echo in errors, screenshots/logs/telemetry; clear sensitive buffers where practical.
- If `Remember password` enabled, use **Windows Credential Manager (preferred)** or DPAPI in user scope; the INI config stores only a credential reference, never plaintext. If disabled, do not persist password.
- Do not store URL with inline password. Redact userinfo from copied URLs and exceptions.
- Validate/canonicalize paths and reject `..` traversal, illegal components, invalid destination paths, and suspicious remote-root escapes. Handle Unicode local paths; clearly report server UTF-8/path encoding limitations.
- Do not transmit data to any other service; no analytics or telemetry by default.

---

## 6. CONFIGURATION, STORAGE, LOGS

**Non-secret settings** stored at `%LOCALAPPDATA%\CameraFtpSync\config.ini` (editable via the GUI; never contains plaintext passwords).

**Sync state** stored in `%LOCALAPPDATA%\CameraFtpSync\sync_state.json`, with a crash-recovery operation journal at `%LOCALAPPDATA%\CameraFtpSync\operations.jsonl`. Use atomic file writes, schema versioning, locking and startup reconciliation. No database engine.

**Logs** in `%LOCALAPPDATA%\\CameraFtpSync\\logs\\`, daily files, 30-day or size-capped retention configurable. **Local snapshot staging** in `%LOCALAPPDATA%\\CameraFtpSync\\spool\\` with size limits/cleanup policy; warn on insufficient disk space. Avoid storing production data or plaintext passwords in logs.

Configuration must include settings version, job ID, source path, source masks/excludes, destination mode/host/port/path, username (nonsecret), credential reference, station ID, sync interval, stability window, verification mode, retry policy, backup policy, and auto-start. Use atomic config save (write temp, flush, replace) and validation before accepting changes. Credentials MUST be in Windows Credential Manager / user-scope DPAPI if the operator explicitly chooses Remember Password; never in INI/JSON/TXT.

**Initial MVP:** one connection profile and one or more sync jobs using that profile is acceptable; at minimum one job fully supported. If multiple jobs are implemented, their state, conflicts and scheduling must be isolated.

---

### 6.1 File-based persistence and crash consistency — mandatory (NO DATABASE)

Store user data under `%LOCALAPPDATA%\CameraFtpSync\` (single active app instance per Windows user profile). Use this directory layout:

```text
%LOCALAPPDATA%\CameraFtpSync\
  config.ini                 # non-secret editable application settings
  sync_state.json            # last confirmed per-file sync checkpoints
  operations.jsonl           # durable operation journal for incomplete FTP publishes
  logs\2026-11-12.log        # append-only human-readable logs with rotation
  spool\                      # immutable local file snapshots for in-flight transfers
```

- `config.ini` holds the local source folder, FTP host, port, mode, username (not password), remote directory, Station ID, interval (default 5 minutes), filters, safety settings, and an opaque credential reference if saved. A normal user may change all relevant fields via the GUI, not only by editing this file.
- `sync_state.json` is a bounded map indexed by **job ID + canonical local relative path + final remote destination**, not only by filename. Each entry records schema version, file identity where available, source size/mtime, staged SHA-256, last verified remote identifier/hash when available, UTC confirmation time, and last result. Use 64-bit file sizes and safe escaping of paths. Keep the file from growing without bound by pruning *only* definitively stale non-active entries according to a documented policy. Never treat absence of a state entry as proof that a remote file is safe to replace.
- To save `sync_state.json` or `config.ini`: serialize a complete new version to a same-volume temporary file, flush the file to disk, then replace the destination via a checked Windows atomic-replace/rename operation. Retain a recovery copy of the previous known-good state; validate parse, schema, and entry sanity on loading. On partial/corrupt state, quarantine the damaged file and require remote reconciliation rather than assuming that no files were previously uploaded.
- `operations.jsonl` records every publish transaction with a unique operation ID, job/file ID, original and temp remote paths, rollback path, expected staged hash/size, and durable phase (`planned`, `staged`, `verified`, `old_moved`, `new_published`, `confirmed`, `cleanup_pending`). Flush the journal before issuing server operations that could destroy/remove the only known valid remote revision. Append completion or recovery records rather than editing lines in place. Provide compaction only when all referenced operations are safely terminal; preserve incomplete operations.
- Use a process-wide mutex or equivalent single-writer guarantee for state/journal and prevent multiple concurrent app instances from manipulating the same job. Within the app, serialize state updates with an internal lock. Every FTP operation must be idempotent or reconcilable after a crash; local durable state alone **cannot** make remote FTP rename atomic.
- On startup, reconcile incomplete journal entries against actual remote temporary/final/rollback objects; if the remote state cannot be proven, stop on a visible conflict, leave potential good copies intact, and require explicit user resolution. Do not auto-delete orphaned files merely because their names look temporary.
- Daily `.log` files are human-readable TXT, with size/age retention and sensitive-data redaction. Logs are **not** authoritative sync state. `pending.json` is optional; retry work can be derived from the state + journal and a new scan.
- Resource limits: avoid reading whole multi-GB files or whole FTP downloads into RAM; stream hashing and upload/download. JSON state is for compact metadata only; if the source contains very many files, use a sharded JSON state layout, still without a DB, and benchmark update costs. Never persist production file payloads in JSON.

**Example `config.ini` (illustrative values; password intentionally absent):**

```ini
[Source]
Folder=D:\CameraData
Include=lsc*.txt
Recursive=false
StableSeconds=30

[FTP]
Host=ftp.company.local
Port=21
Security=ExplicitFTPS
RemoteRoot=/production/camera
Username=ftp_user
StationID=PC01
SeparateByStation=true
RememberPassword=false

[Sync]
IntervalMinutes=5
AutoStart=false
Verification=ReadBackSHA256
BackupOnNormalUpdate=false
RetryAttempts=5
```

**Example `sync_state.json` (metadata only, no password):**

```json
{
  "schema_version": 1,
  "jobs": {
    "job-PC01": {
      "files": {
        "lsc.txt": {
          "remote_path": "/production/camera/PC01/lsc.txt",
          "source_size": 102400,
          "source_sha256": "<64-hex-digit-hash>",
          "last_confirmed_utc": "2026-11-12T04:30:00Z",
          "status": "confirmed"
        }
      }
    }
  }
}
```

The hash above is an illustrative placeholder, not a valid hash value. Implement schema parsing with strict validation, error reporting, and future-version rejection. Because INI settings are easy to corrupt manually, validate all inputs when loading and before running.

---

## 7. ERROR HANDLING, RETRY, AND RECOVERY

- Classify errors: local source unavailable, file busy, disk full, DNS, network timeout, FTP login (530), TLS validation, FTP permission (550), quota, rename unsupported, remote changed, integrity mismatch, corrupt checkpoint, and cancellation.
- Retry **transient** errors using exponential backoff with jitter, with a cap. Authentication/certificate/configuration/conflict errors must not enter endless noisy retry loops without user intervention.
- Persist checkpoints only after full success. Preserve crash recovery journal for in-progress publish operations.
- Status must distinguish: `last scan succeeded` from `all eligible files synchronized`; do not display misleading green Success when some files were skipped due to errors/conflict.
- Pause/resume and PC sleep/wake behavior: trigger recheck on resume; do not create overlapping jobs.
- Avoid CPU-intensive polling and unnecessary full-file hashing, but prefer stronger validation where confidence is uncertain.
- Handle duplicate timestamps, daylight saving / clock changes, long filenames, huge files (>4 GB), low disk space, read-only files, and rapid append while syncing. Use 64-bit file sizes and UTC internally for checkpoints/logs.

---

## 8. BACKUP/RETENTION RULES

1. Normal successful periodic update: **no permanent historical backup by default**; a short-lived rollback staging file during publish is permitted and must be cleaned up only when safe.
2. Confirmed same content: neither upload nor backup.
3. Unexpected remote content / reset / data conflict: keep both files, warn and require an explicit operator choice; do not automatically overwrite.
4. Explicitly approved replacement: create timestamped backup in configured backup directory or naming convention, verify it exists and is safe, then publish replacement. Use unique suffix to avoid name collision. Example compatible naming: `lsc^20261112-100405.txt`, with documented 24-hour `HH`.
5. Backup retention can delete **only backups recorded as created and owned by this application**. Never sweep arbitrary files in the FTP directory. Default to conservative retention; allow the user to disable automatic deletion.
6. If FTP server has no space for staged file + rollback, fail safely with a clear disk-quota warning.

---

## 9. SUGGESTED C++/MFC PROJECT STRUCTURE

```text
CameraFtpSync/
  CameraFtpSync.sln
  src/
    Gui/                 # MFC dialogs, message handlers, input validators
    Core/
      Scheduler.*        # periodic jobs, pause/resume, no overlap
      FileScanner.*      # include/exclude masks and directory traversal
      SnapshotManager.*  # immutable local spool snapshot
      ChangeDetector.*   # metadata + SHA-256 comparison
      SyncEngine.*       # upload decision, conflict policy, state transitions
      RemotePublisher.*  # staged upload, verify, rename/rollback
      RecoveryManager.*  # resume interrupted transactions
      ConfigManager.*    # JSON, settings validation
      FileStateStore.*   # sync_state.json: versioned atomic checkpoints
      OperationJournal.* # operations.jsonl: crash-recovery publish journal
      CredentialStore.*  # Windows Credential Manager / DPAPI
      Logger.*
    Transport/
      IFtpClient.h       # mockable abstraction
      CurlFtpClient.*    # libcurl FTP/FTPS implementation
    Models/
  tests/
    Unit/
    Integration/
    Fixtures/
  docs/
    USER_GUIDE_VI.md
    ADMIN_DEPLOYMENT.md
    SECURITY_AND_LIMITATIONS.md
    TEST_REPORT.md
  README.md
  CMakeLists.txt / VS project files
```

Core code should be testable without creating MFC dialogs. No network operations on the GUI thread. Use RAII resource lifetime management, strong path validation, cancellation tokens where useful, thread-safe status updates, and no implicit use of global mutable shared state. `IFtpClient` must enable deterministic fault injection in tests.

Dependency notes: libcurl for FTP/FTPS; Windows INI APIs or a tested UTF-8 INI parser for non-secret configuration; a maintainable lightweight JSON serializer/parser for state and journal (a header-only library is fine); SHA-256 from Windows CNG; Windows Credential Manager / user-scope DPAPI for saved secrets. No database dependencies. Pin dependency versions and describe license/build/runtime requirements. Do not invent functions that the pinned library does not offer.

---

## 10. AUTOMATED ACCEPTANCE TESTS (REQUIRED)

Implement unit tests and FTP integration tests using an isolated temporary server/directory. A fake/mock FTP interface is required for deterministic failure paths.

| # | Test scenario | Expected result |
|---:|---|---|
| 01 | Manually set valid local folder and remote host/account/path | Settings saved and reflected in destination preview |
| 02 | Enter invalid source folder | Clear validation, cannot start unsafe job |
| 03 | Valid FTP credentials | Connection test shows success/capabilities |
| 04 | Invalid FTP password | Clear 530/auth error, no upload |
| 05 | Untrusted FTPS certificate | Reject safely, no TLS bypass |
| 06 | Initial source, empty remote folder | Upload to temp, verify, publish final |
| 07 | No changes across multiple cycles | No upload, no backup |
| 08 | Source appends after prior success | One full snapshot transfer on next safe cycle |
| 09 | Same-size source data modification | Hash-based detection; do not falsely SKIP |
| 10 | Source file actively growing during copy | Retry/skip, never publish mixed snapshot |
| 11 | Remote initially identical to source | Verify/adopt and skip upload |
| 12 | Remote initially unrelated | Conflict, preserve remote file |
| 13 | Remote larger than local | Conflict, preserve both |
| 14 | Local file truncated or rotated | Conflict/segregate, no silent remote truncate |
| 15 | Connection lost halfway through STOR | Final remains valid if possible, safe recovery |
| 16 | Remote SIZE differs after upload | Integrity failure, no publish |
| 17 | High Integrity read-back hash mismatch | Integrity failure, no publish |
| 18 | FTP rename of existing final fails | Safe abort / rollback, detailed error |
| 19 | Crash after moving final to rollback | Next launch recognizes and recovers journal safely |
| 20 | Retry after crash once publish succeeded | Idempotent reconciliation, no destructive duplicate |
| 21 | Click Sync Now during scheduled run | No overlapping writes |
| 22 | Pause then resume | No scheduled uploads during pause, next resume checks |
| 23 | FTP session returns 550 permission denied | Visible failure, never mark success |
| 24 | 1000 small files and a >4 GB file | Correct 64-bit progress and bounded memory usage |
| 25 | Password saved opt-in | Secret stored in Credential Manager/DPAPI, not settings/log |
| 26 | Password not saved | Prompts on next session, no persisted secret |
| 27 | Source and destination paths with Unicode | Works where server capabilities support it or fails clearly |
| 28 | Two stations use default namespaced destination | No cross-station overwrite |
| 29 | Two stations intentionally share one target | Collision identified; unsafe overwrites blocked |
| 30 | Manual replacement with timestamped backup | Old remote preserved; unique backup name; new committed |
| 31 | Auto-retention run | Deletes only app-owned eligible backups |
| 32 | PC suspends/resumes / clock changes | No duplicate concurrent jobs |
| 33 | Low staging-disk space | Safe stop with error; original unchanged |
| 34 | FTP URL includes embedded credentials | Credentials not saved into URL or printed in logs |
| 35 | Plain FTP selected | Explicit warning/opt-in required |
| 36 | Remote server lacks safe rename ability | Fail safely; no direct unsafe final overwrite |
| 37 | Restart after successful publish and before state checkpoint | Journal reconciliation identifies the published revision; no duplicate destructive upload |
| 38 | Truncated or invalid `sync_state.json` | Previous known-good copy used if valid; otherwise quarantine and reconcile, never blindly overwrite |
| 39 | Truncated `operations.jsonl` record / crash during journal append | Ignore/report incomplete tail safely; recover known complete transactions without losing old remote copy |
| 40 | Two running app processes share state folder | Only one writer proceeds; second exits or is read-only with clear message |
| 41 | `config.ini` contains a secret or a hostile relative remote path | Secret never logged; path rejected; clear actionable error |
| 42 | Package scan/dependency audit | Zero SQLite DLL, sqlite3 source, database connection strings, or database runtime requirements |

Add a manual smoke test checklist with screenshots for GUI and a simulated production sequence:
- Create `D:\\CameraData\\lsc.txt` (or a temporary equivalent).
- Configure FTP server and remote `/production/camera/PC01/`.
- Sync first file; append lines; repeat with 5-min (or shorter test-mode) periodic cycles.
- Verify no redundant uploads, no redundant permanent backups, correct final contents, and clean pause/resume/restart.
- Explicitly mark the scenarios that cannot be fully proven in the local test environment.

---

## 11. DEFINITION OF DONE / RELEASE GATE

Codex work is complete only when:

- [ ] A **standalone Windows desktop application**, not an integration into existing production software, is implemented.
- [ ] GUI accepts editable local source folder and FTP/FTPS URL/host, username, password, port, and destination directory.
- [ ] The operator can test the connection, configure 5/10-minute intervals, save settings, start/pause, and sync manually.
- [ ] Nothing is uploaded if local data has not changed; no uploads directly into a live final file.
- [ ] Source files are read-only to this app and transferred from stable snapshots.
- [ ] Existing unknown remote files are not silently overwritten.
- [ ] Network interruptions and publish/rename failures are recoverable as far as server capabilities allow.
- [ ] Passwords are stored securely only with explicit opt-in; no sensitive logs.
- [ ] Unit and integration tests cover normal flow, errors, conflict, retry and crash recovery.
- [ ] Release build and run instructions are available; app can start without the Camera production software.
- [ ] `config.ini`, `sync_state.json`, `operations.jsonl`, rotating TXT log and test coverage for malformed/recovered state; **no SQLite/database used**.
- [ ] Vietnamese usage guide, a sample configuration screenshot, limitations and rollback guide delivered.
- [ ] Codex reports what was actually built and tested, plus unresolved server-specific limitations.

**Release restriction:** If the actual FTP server does not support staged upload + safe publish and an appropriate verification method, the program must report this limitation and **not silently revert to unsafe direct overwrite**.

---

## 12. CODEX EXECUTION STEPS

**Phase A — Repository and environment:** detect Windows/MFC compiler, check project location, plan solution creation and dependencies.  
**Phase B — Working MVP:** editable configuration GUI, FTP connection test, directory scan, periodic schedule, changed-file upload with remote staging, logs.  
**Phase C — Production safety:** snapshot stability, SHA-256, file-backed checkpoint + operation journal, conflict/adoption rules, rollback recovery, TLS, credential protection, verification.  
**Phase D — Operational features:** tray, start with Windows opt-in, history, retention, reliability counters, status dashboard.  
**Phase E — Validation:** tests and fault injection, build Release, documentation, real-FTP test checklist.

Do not skip production-safety functionality but hide it behind a polished UI. If a time/technical constraint prevents a capability, leave an explicit fail-safe behavior and report it.

**Suggested run prompt (for Codex CLI):**

```text
Read CODEX_CAMERA_FTP_SYNC_NO_SQLITE.md. Create a new independent
Windows C++17/MFC application named CameraFtpSync. Implement the complete
manually configurable GUI and FTP/FTPS synchronization engine specified
in the document. Do not modify the camera production software. Use safe
snapshot uploads, remote temporary filenames, verification, recovery,
conflict detection, Windows Credential Manager / DPAPI, config.ini,
sync_state.json, operations.jsonl and TXT logs. Do NOT use SQLite or any database.
Build and test all feasible parts, and report precise results and any
unverified FTP server-specific behavior. Deliver source code, tests,
Visual Studio solution, and Vietnamese user instructions.
```

---

## 13. REAL-SERVER QUESTIONS (VALIDATE BEFORE PRODUCTION DEPLOYMENT)

These questions should be answered during onsite testing, **not guessed**. The software must remain fail-safe when information is missing:

1. Is the endpoint FTP, explicit FTPS, implicit FTPS, or SFTP? Which port/firewall and certificate trust chain?
2. Can the server create files, query SIZE, rename inside the same directory, and rename/replace an existing destination? Does it expose a trustworthy hash command?
3. Is a final filename continuously read by downstream processing, and is any no-file gap during two-step rename acceptable?
4. Is the original data writer strictly append-only? How does it rotate/reset files, and can it rewrite bytes without changing file size?
5. Will multiple PCs upload the same filename to the same remote folder? Can each use a unique Station ID subfolder?
6. Is a permanent backup required by quality or traceability policies, and if so what retention rules must be configured?
7. Do PC/server clocks differ significantly, and are Unicode filenames supported by the server?

**Baseline safety:** Until these are validated, use namespaced station folders, conservative conflict detection, secure transport, staged full-file uploads, and never force-overwrite unknown remote files.
