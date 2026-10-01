---
# portps5-zzwm
title: 'docs: resync ROADMAP status and M1 epics with main'
status: completed
type: task
priority: normal
created_at: 2026-10-01T18:00:31Z
updated_at: 2026-10-01T18:00:51Z
---

## Context

`docs/ROADMAP.md` still listed PRs #62, #66 and #67 as in flight after they merged, and omitted the PRs that landed on 2026-10-01 (#73, #74, #76, #78, #82 to #87) and the open per-draw cost PRs (#71, #75, #77). The three M1 epic beans (`portps5-4ut1`, `portps5-w3s8`, `portps5-r8mh`) were typed `task`, had no children, and listed completed work (portps5-mij8, portps5-ekx3, portps5-pjy1, portps5-8l0d) as open. `portps5-f9a3` had every acceptance box ticked but stayed open. `portps5-kmb6` (a full run, the M2 exit) was titled M1, and `portps5-0mv7` carried a stale "60fps gate" in its title.

## Higher Goal

ROADMAP checkboxes and beans describe `main` exactly, so the next milestone seed and the performance-track PR start from a true baseline.

## Acceptance Criteria

- [x] ROADMAP status line lists the merged and open PRs as of 2026-10-01 (including #53, #68 and #88, merged after this PR opened); the `/savedata0` item is ticked
- [x] ROADMAP M1 telemetry item and verification.md 4.3 point at the open wiring bean (portps5-w1re)
- [x] portps5-f9a3 completed and archived; wiring tracked by portps5-w1re
- [x] The three epics typed `epic`, retitled, refreshed against main, and their children parented
- [x] portps5-kmb6 retitled M2; portps5-0mv7 retitled without the 60 fps figure
- [x] `beans check` passes; docs hardware-wording check prints nothing

## Out of Scope

New roadmap content (the performance track is a separate PR). Milestone-type beans for M0 to M6.

## Summary of Changes

- `docs/ROADMAP.md`: status line as of 2026-10-01; M1 telemetry item names PR #78 and portps5-w1re.
- `docs/spec/verification.md` 4.3: open wiring names portps5-w1re.
- Beans: portps5-4ut1, portps5-w3s8, portps5-r8mh are epics with refreshed bodies; children tiod, 9s7e, ux18, r7qk (driver), 421p, vzmm, r2ns, k7qd, u5fe (memory), c06p, dtwf, w1re, f9a3 (runtime). portps5-f9a3 completed and archived. kmb6 and 0mv7 retitled.
