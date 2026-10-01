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

## Not listed

Titles missing from the table have not been reported. The other gate titles (TMNT: Shredder's Revenge, Tomb Raider I-III Remastered, Bugsnax, Demon's Souls) are tracked in the [ROADMAP](ROADMAP.md).

## Reporting a result

Send metrics only: title, status, fps, hardware tier. Never attach dumps, screenshots, video frames or logs containing game data ([legal boundary](../.agents/rules/legal-boundary.md)).
