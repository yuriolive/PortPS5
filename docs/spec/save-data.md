# PortPS5 — Spec: Save data

Status: draft v1 · 2026-09-27

## Scope

This spec covers save storage (`libSceSaveData(.native)`), save and system dialogs (`libSceSaveDataDialog(.native)`, `libSceCommonDialog`, `libSceMsgDialog(.native)`), user and system services (`libSceUserService`, `libSceSystemService`), and offline NP/PSN/trophy behaviour (`libSceNp*`, `libSceNpTrophy2`, `libSceNpUniversalDataSystem`).

**This spec owns offline NP, PSN and trophy behaviour (PRD F8, delivered in ROADMAP M1).** [configuration.md](configuration.md) owns none of it.

PRD bars owned here: F2 (save, quit, relaunch and continue) and F8 (online calls return offline behaviour and never block progression).

## Current state

**PortPS5 offline sockets.** `libSceNet` rejects IPv4/IPv6 bind-family mismatches with `NET_EAFNOSUPPORT`; explicit port collisions require matching family and socket type. Socket abort remains set until close so both current and later blocking waiters receive `NET_ECONNABORTED`. IPv6 conversion accepts one-to-four-digit hexadecimal groups and at most one `::` replacing at least one group; prefixes, signs, whitespace, excess digits and trailing single colons are invalid. Invalid conversion leaves the output buffer unchanged.

References are relative to `core/libs/prx/`. PR #5 line numbers are given unless noted otherwise.

**Save storage (`libSceSaveData.native/Export.cpp`).**

| Behaviour | `main` | PR #5 |
|---|---|---|
| Root | `_sd`, relative to the process CWD (`:12`) | Same (`:18`, `:23-25`). Not per title, not per user. |
| Mount point | `"/" + real_path` (`:207`) | `/_sm/<slot>`, aliased through `AddPathAlias_nid_no_patch` (`:286-287`; `libc/src/General.cpp:39`) |
| Mount slots | 16 (`SaveData.hpp:26`) | Same. Busy and exists checks at `:265-276`. An unknown `mount_mode` throws (`:253-255`). |
| Backup, event result, memory blobs | Throw | Backup queues an OK event at once (`:77-92`). Memory blobs are persisted as `_sd/_memory_<slot>.bin` with a truncating `ofstream` (`:35-43`, `:333-383`). |
| Commit, Prepare | No-ops | No-ops (`:94-97`, `:297-301`). Writes go straight to the files. |
| Params, icons, quota | — | `GetParam` returns zeros (`:182-195`). `DirNameSearch` zeroes params (`:144-146`). `SetParam` and `SaveIcon` are dropped (`:303-331`). `GetMountInfo` reports 32768 free blocks (`:169-180`; `SaveData.hpp:24`). |
| Other | — | `sceSaveDataTransferringMount` throws (`:402-407`). |

**Dialogs.** In `libSceSaveDataDialog.native/Export.cpp`, `Open` sets `FINISHED` immediately (`:51-74`), and `GetResult` always returns OK, the OK button and the first dir name passed in (`:35-49`). `libSceCommonDialog`'s `IsUsed` returns false. Every export in `libSceMsgDialog/Export.cpp:8-63` throws, and PR #5's `libSceMsgDialog.native` exports only a marker variable. `docs/TechnicalDebt.md:14-15` lists the two silent dialog stubs.

**User and system services.** `libSceUserService/Export.cpp:85-100` has one constant initial user, and `GetUserName` and `GetUserNumber` throw (`:104`, `:112`). `libSceSystemService/Export.cpp:50-65` `ParamGetInt` returns fixed values (English US, 24-hour clock, Cross as enter), and `ParamGetString` throws (`:67-72`).

**NP and trophies.**

| Library | `main` | PR #5 |
|---|---|---|
| NpManager | 25 exports, all throw | Signed-out semantics: `GetState` → `SIGNED_OUT` (`:102-106`); `PollAsync` finishes with `SIGNED_OUT` (`:116-120`); `CheckPremium` and `GetAccountId`/`GetOnlineId` → signed out (`:41-45`, `:70-80`, `:96-100`). 13 of 29 still throw, including `CheckNpAvailability`, `GetNpId`, `HasSignedUp` and every `Register*Callback`. |
| NpAuth | 8 exports, all throw | Same |
| NpWebApi2 | 18 exports, all throw | 24 exports, none throw |
| NpCppWebApi | — | 157 exports, 51 throw |
| Trophy2 | Fixed-constant game, group and trophy info (`libSceNpTrophy2/src/GameInfo.cpp:11-37`). Icon getters throw "icon file not found" (`GameInfo.cpp:39-47`, `GroupInfo.cpp:73-81`, `TrophyInfo.cpp:70-78`). | Same |
| UniversalDataSystem | `PostEvent` accepts the event and drops it (`src/Event.cpp:36-37`) | Same |

## Decision

This follows the decision table in [README.md](README.md#subsystem-specs) §Save data: adopt PR #5's `libSceSaveData.native`, replace the silent dialog stubs with scripted and logged results, store saves under `%LOCALAPPDATA%/PortPS5/saves/<titleId>/`, and make a save round-trip per gate title part of local regression.

On top of that, this spec takes the offline NP decision: signed out, deterministic, never throwing on a gate path.

## Target design

**Storage layout.** Saves live at `%LOCALAPPDATA%/PortPS5/saves/<titleId>/<dirName>/`, where `titleId` comes from `/app0/sce_sys/param.json` (`libkernel/AppMetadata/src/AppMetadata.cpp:22-37`). Memory blobs live at `<titleId>/_memory/<slot>.bin`. PortPS5 metadata lives in `<dirName>/.portps5/`: `param.bin` (the last `SetParam`) and `icon0.png`. 1.0 has one user, so there is no user level in the path.

**Crash safety.** An RDWR mount snapshots the directory to `<dirName>.portps5-prev/` before the first write, and a clean `Umount2` deletes the snapshot. The copy is staged in `.<dirName>.portps5-prev.tmp/` and renamed to the snapshot path only after copying succeeds; a failed copy or rename rejects the mount and removes the staging copy. A snapshot left over at start-up means a session died mid-save, so it is restored and the event is logged. Memory blobs are written to a temporary file and swapped in with `ReplaceFileW`, never truncated in place.

**Validation and synchronization.** Delete requires a terminated, nonempty directory name and rejects `.`, `..`, `_memory`, and names containing `/`, `\`, or `:`. Slot lookups and use, including metadata I/O, are serialized with mount and unmount. Dialog scans skip internal and snapshot directories, skip entries with metadata errors, and stop on iterator errors without throwing. Only a running native dialog owns a common-dialog registration.

**Fidelity.** `SetParam`, `GetParam` and `DirNameSearch` round-trip the stored param, and icons are stored and returned. `GetMountInfo` reports free blocks from the real free disk space, capped at `SAVE_DATA_BLOCKS_MAX`. `TransferringMount` calls `Unsupported()`, which logs and aborts, until a gate title needs it. No throw crosses the `APS5_VABI` boundary ([threading.md](threading.md)).

**Migration.** At first start, if `./_sd` exists next to the executable and the target directory is empty, it is copied to the target directory and a log line is written. The source is never deleted.

**Scripted dialogs.** Every dialog `Open` emits a structured log event: `dialog.open {lib, mode, type, result}`. Dialogs complete on the next `UpdateStatus` call, not inside `Open`, so titles that poll see the `RUNNING → FINISHED` sequence.

| Dialog | Scripted result |
|---|---|
| SaveData: confirm, overwrite or save | OK / Yes. |
| SaveData: list (load) | The newest existing dir by modification time, or Cancel if there are none. It never invents a dir name. |
| SaveData: error or no-space notice | OK, logged at warning level. |
| MsgDialog: user message | The default button (Yes/OK). Progress bars finish immediately. |
| MsgDialog: system message | OK. |
| CommonDialog | `IsUsed` reflects whether any dialog is open. |

**User and system services.** One user, id 1, named "Player" by default. `GetUserName` returns that name, and `ParamGetString` returns the user name or an empty string. Language comes from `ParamGetInt` (fixed English US in 1.0). Unknown params return 0 and are logged once.

**Offline NP and PSN.** Every NP entry point on a gate path returns a signed-out or offline result: state `SIGNED_OUT`, reachability unreachable, the signed-out error from availability checks, async requests finished with `SIGNED_OUT` on the first poll, and a network-unavailable error from WebApi calls. `Register*Callback` stores the callback and never calls it, because no state change ever happens. No call blocks and no call throws. An export first reached by a gate title is added to this list in the M1 inventory.

**Trophies.** Trophy info calls return the title's own trophy metadata when the dump contains it, and the current constants otherwise. A missing icon returns a not-found error code instead of throwing. UDS events that look like trophy unlocks are appended to `<titleId>/_trophies.log` for diagnostics. *Inference:* PS5 trophy unlocks travel as UDS events, because Trophy2 exposes no unlock export. Nothing is uploaded, and progression never waits on a trophy result.

## Interfaces

| Peer spec | Contract |
|---|---|
| [guest-memory.md](guest-memory.md) | The libc path-alias table maps `/_sm/<slot>` to the host dir. The mount point stays at 16 bytes or fewer. |
| [threading.md](threading.md) | Dialog status and save state are guarded by a module mutex. No save call holds a guest lock across file I/O. |
| [input.md](input.md) | The user ids that pad slots bind to. |
| [configuration.md](configuration.md) | No behavioural save or NP keys. `debug.trace = ["savedata", "dialog", "np"]` controls logging. Configuration files never hold save data. |
| [verification.md](verification.md) | Save round-trip (§2.5), checkpoint saves (§2.2) and the `save_roundtrip` field. Saves never leave the maintainer machine. |
| [build-toolchain.md](build-toolchain.md) | `ReplaceFileW` and `SHGetKnownFolderPath` (LocalAppData). No new dependencies. |

## Failure modes

| Failure | Handling |
|---|---|
| Crash or kill during a save | The snapshot is restored at the next start and logged. The previous save survives. |
| Disk full | Return the save-data no-space error. Never report success for a failed write. |
| Title opens a dialog kind with no script | Log at error level and return Cancel, which is visible in the logs rather than a silent OK. |
| NP export not yet covered | It calls `Unsupported()`, which logs and aborts (loudly). The M1 inventory and the `policy` checks keep gate paths covered. |
| Unknown `mount_mode` bits | `Unsupported()` logs and aborts, so the gap is visible. |
| Old AnyPS5 `_sd` layout present | Migrated once by copying. The source is kept. |

## Tests

- `OfflineNetStackTests` covers bind-family mismatches, port sharing across families/types, malformed and valid IPv6 groups, and concurrent plus post-abort accept calls using synthetic socket state.

- **GoogleTest Unit Suites** (`ctest -L unit`, hosted `unit` job):
  - Isolated temporary directory fixtures guaranteeing test hermeticity across runs.
  - Mount modes (create, create2, rdonly, rdwr) with their exists, not-found and busy errors.
  - Write, unmount, remount, and read consistency.
  - Snapshot restore and atomic file swapping after a simulated process kill.
  - Memory blob swap and quota enforcement (`SCE_SAVE_DATA_ERROR_NO_SPACE`).
  - Wildcard pattern matching (`dir_name_match` with `%` and `_`).
  - Param and icon metadata binary round-trip.
  - Dialog state sequence transitions and non-blocking return values.
  - Death tests (`EXPECT_DEATH`): verify that unknown or corrupt `mount_mode` bits trigger an abort through `Unsupported()`.
- **Ported Ecosystem Test Suites:**
  - **SharpEMU `KernelSandboxEscapeTests`:** path traversal attack containment (e.g. `../` sequences, symlink traversal escaping container roots, Windows absolute path injection), default-deny write permission checks, and guest mount namespace isolation.
  - **Atomic Persistence Suites:** power-cut / process-termination simulation verifying that partial writes never corrupt previous valid snapshots.
- **Local regression:** the save round-trip per gate title (save, quit, relaunch, load), plus checkpoint loads from stored saves.
- **Full run:** every run ends with the round-trip, and `save_roundtrip = pass` is required.

## Milestones

| Milestone | Work |
|---|---|
| M1 | - [x] Offline NP, trophies, user service and system dialogs non-blocking at boot for all five titles. NP, dialog and trophy import inventory. `MsgDialog` stops throwing. |
| M2 | - [ ] Per-title storage, crash safety, param and icon fidelity, scripted save dialogs, `_sd` migration. Round-trip in `tools/regress`. Dreaming Sarah and TMNT pass. |
| M3 | - [ ] Tomb Raider save/load (the PRD gate for save/load), including the multi-slot list dialog. |
| M4–M5 | - [ ] Bugsnax and Demon's Souls saves, including any backup events or memory-blob paths they use. |
| M6 | - [ ] The user guide documents the save location and migration. |

## Open questions

- Multi-user saves: add a `<userId>` level before 1.0 or after? Not gated.
- Should trophy metadata be parsed from the dump for display only, or skipped entirely?
- Do any gate titles call `TransferringMount` or backup restore? This is answered by the M1 inventory.
