# Audio codec / NGS2 / Audio3d import inventory (M1)

Method: inspection of the NID export tables in this repository only. No game
data, dumps, shader bytecode, saves, or recorded input were accessed; no
import manifest ships in the repo, so per-title proof still needs a local dump
run on a maintainer machine (boot the title, log the AJM codec ids passed to
`sceAjmInstanceCreate` and the first NGS2/Audio3d/Audiodec import hit, record
only ids and pass/fail in the results JSON). Until then the "needed by" column
below is the best supported inference, labelled as such.

## What the code supports today

| Area | Implementation | Behaviour on the unknown |
|---|---|---|
| ATRAC9 (AJM codec 1) | `libSceAjm.native/src/Ajm.cpp` via LibAtrac9 | n/a (decoded) |
| Any other AJM codec id | none | Log once at instance creation, then a codec error per job; never silent zeros |
| `libSceNgs2` + `libSceNgs2.native` (27 + 27 exports: rack/system/voice/pan/geometry) | throw-stubs (abort through the logging abort path) | abort with a log line naming the entry |
| `libSceAudio3d` (7 exports: init, open, port advance/push/queue/attribute) | throw-stubs | same |
| `libSceAudiodec` + `libSceAudiodec.native` (6 + 6 exports: init/create/decode/clear/delete) | throw-stubs | same |
| AudioOut2 contexts/ports | `libSceAudioOut` (this slice) | unknown attribute ids logged once per id, then ignored |

## Per gate title (names; ids live in the PRD pin table, not here)

| Gate title | Engine tier | Audio need (inference; confirm from a local dump) |
|---|---|---|
| Dreaming Sarah | 2D custom | Simplest path: AudioOut v1 ports plus ATRAC9 for music. Unlikely to need NGS2/Audio3d. |
| TMNT: Shredder's Revenge | 2D action | Mixing path plus ATRAC9; proves the M2 single-mixer work. NGS2/Audio3d unlikely. |
| Tomb Raider I-III Remastered | Simple 3D | ATRAC9 plus the FMV A/V offset path; save/load dialogues are silent stubs. Watch for Audiodec imports during FMV. |
| Bugsnax | UE4 | First title that may import NGS2 (UE4 audio graph) or non-AT9 AJM codecs; the M4 milestone implements whatever its dump shows. |
| Demon's Souls (AAA custom) | AAA custom, Bink FMV | ATRAC9 proven for the intro path by the upstream work this slice ports; object ports observed (mono/stereo/7.1 bed). Object-port panning is M5 work. Any NGS2/Audio3d import found in its dump goes to the M5 list. |

## Rule for adding codecs

An AJM codec is added only when a local dump run shows a gate title
requesting its id. Unknown ids keep today's behaviour: log once, codec error
per job, never fabricated PCM (audio spec Target design).
