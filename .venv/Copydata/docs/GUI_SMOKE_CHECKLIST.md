# GUI and onsite smoke test — pending

Run only against an isolated test source and FTP subtree. Do not use active production data for failure injection.

- [ ] Build x64 Release in VS2022 with MFC, launch the desktop executable without camera software installed.
- [ ] Capture `screenshots/01-settings.png`: source path, connection, station, security, masks, custom 5/10-minute interval, destination preview; password masked and no inline credentials.
- [ ] Browse/change local source and host/account/port/path without touching code or INI. Invalid folder/path/numbers must block Start and show an actionable message.
- [ ] Capture `02-connection.png`: login/LIST success and explicit write/rename/delete probe result. Verify test artifacts are removed, or cleanup failure is reported.
- [ ] Use a temporary `lsc.txt`; Start, wait for stability and scan. Capture `03-first-sync.png`: uploaded file and matching server bytes.
- [ ] Run unchanged cycles. Verify no STOR and no permanent backup; RETR for ownership verification is expected in this implementation.
- [ ] Append lines; wait for next 5/10-minute scan. Verify full staged transfer, final match and transient rollback cleanup.
- [ ] Capture `04-conflict.png`: same-size rewrite, truncation, unknown remote and externally edited remote all conflict. Neither source nor existing remote is deleted/overwritten.
- [ ] Select exact relative filename and explicitly authorize replacement. Capture `05-backup.png`; old bytes must exist under a collision-safe timestamp/ID name. Retention must leave foreign files untouched.
- [ ] Pause/Resume/Sync Now during upload. Confirm no overlapping writes and no scheduled run during pause. Physical sleep/wake and wall-clock changes must not create overlap.
- [ ] Close X, verify tray remains and synchronization continues while logged in. Reopen from tray. Exit must cancel safely and stop; logout is not service mode.
- [ ] Opt in/out of saved password; restart. Check Credential Manager, absence of secret in INI/state/logs and prompt/input after opt-out. Verify startup disabled by default and current-user opt-in/out only.
- [ ] Reject an untrusted or wrong-host FTPS certificate. Test explicit and implicit TLS using actual server trust chain; no bypass/downgrade.
- [ ] Interrupt STOR, crash after each journal phase, restart before checkpoint completion. Compare final/temp/rollback hashes; unresolved artifacts must block further publishes and remain recoverable.
- [ ] Test 550, unsupported SIZE/rename, quota/low staging space, exclusive source lock and Unicode paths. Verify actionable status and no false green success.
- [ ] Test two processes: second state writer exits. Test two physically isolated Station IDs; never run two PCs against the same live filename.
- [ ] Verify actual downstream reader tolerates two-step gap. If not, leave gap consent off and reject deployment.
- [ ] Audit signed ZIP and dependency inventory; verify database DLLs/drivers/connection strings absent. Install on a clean standard-user VM with only the VC++ redistributable.

No screenshots in this folder are claimed as captured evidence until an operator executes this checklist. Record OS, DPI, server/version, protocol, certificate issuer, account permissions and date alongside captures.
