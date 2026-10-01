# Supported games

Titles reported as running on PortPS5, with the status as tested by a maintainer on their own decrypted dump. How to run one: [USER_GUIDE.md](USER_GUIDE.md). Measured, release-grade results are published as results JSON ([spec/verification.md](spec/verification.md) §4); this page is the human-readable summary.

Hardware is described by tier, not by model ([PRD §4.4](PRD.md)): the **reference tier** is a 12-core desktop CPU and a mid-to-high-end Vulkan 1.3 GPU with 12 GB VRAM and 32 GB RAM, on Windows 11.

## Status levels

| Status | Meaning |
|---|---|
| Playable | Boots and can be played start to finish of the tested section, no blocking issues |
| In game | Reaches gameplay with known issues |
| Boots | Starts and renders, not yet usable |
| Does not boot | Fails at conversion or start-up |

## Titles

| Title | Status | Performance | Tested on |
|---|---|---|---|
| Dreaming Sarah | Playable | 60 fps | Reference tier |

Statuses here are maintainer reports from a local run. A title counts as passing the 1.0 full-run protocol only once its results JSON is published; for Dreaming Sarah that is still pending (bean `portps5-kmb6`).

## Not listed

Titles missing from the table have not been reported. The other gate titles (TMNT: Shredder's Revenge, Tomb Raider I-III Remastered, Bugsnax, Demon's Souls) are tracked in the [ROADMAP](ROADMAP.md).

## Reporting a result

Submit a measured run as [results JSON](spec/verification.md) (`portps5.results/1`, §4), written by `tools/regress.py` and opened as a PR adding it under `compat/results/<titleId>/` (the [compat-result](../.agents/skills/compat-result/SKILL.md) skill walks through it). The table above is a human summary of title, status, fps and hardware tier; a submission carries the full schema, including run metadata, hashes and the pass/fail verdict. Never attach dumps, screenshots, video frames or logs containing game data ([legal boundary](../.agents/rules/legal-boundary.md)).
