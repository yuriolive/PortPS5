# PortPS5 — Spec: Video / FMV

Status: draft v1 · 2026-09-27

## Scope

In-game video playback. It takes two paths:
- **Title-decoded video**, for example Bink 2 decoded by the title's own compute shaders and CPU code.
- **System-decoded video** through `libSceAvPlayer`.

This spec also owns A/V offset telemetry and the "FMV played" pass rule. Presentation and flip pacing belong to `libSceVideoOut`, and are described here only where FMV depends on them. Everything else in the GPU path is in [gpu-driver.md](gpu-driver.md).

PRD bars owned here: F4 (FMV plays; skipping is not a pass) and the ±80 ms A/V offset during FMV.

## Current state

References are relative to `core/`.

| Area | AnyPS5 `main` | PR #5 |
|---|---|---|
| Bink 2 (title-decoded) | No verified FMV reach. | Demon's Souls' intro cinematic plays through the title's own decoder. It depends on two fixes:<br>• the `s_*_saveexec` source-before-destination order (`shader/recompiler/Translation/src/ControlFlowInstructions.cpp:18-25`; the comment names the Bink 2 kernels; `APS5_SAVEEXEC_WRITE_FIRST` reverts it);<br>• "adjacent-generation" write-back for video planes packed back to back (`libs/prx/libSceAgcDriver/Graphics/src/Texture.cpp:838-849`, `:2609-2641`; kill switch `APS5_NO_ADJACENT_GENERATION`). |
| `libSceAvPlayer.native` | All 27 exports are `NotImplemented_nid_no_patch`, which throws `std::runtime_error` (`libs/prx/libc/src/General.cpp:85-87`). For example, `sceAvPlayerInit` is at `libs/prx/libSceAvPlayer.native/Export.cpp:87-91`. | Upstream implements a 27-export state machine simulating player lifecycle (Ready/Play/Pause/Stop) with a 1080p blank clip. PortPS5 ports this state machine with full System V ABI annotations and companion GoogleTest suite. |
| `libSceAvPlayer` (non-native) | 25 throw-stubs. | Forwards to `libSceAvPlayer.native` implementation under System V ABI. |
| Flip pacing | `libSceVideoOut` synthesises a 59.94 Hz vblank from `steady_clock` (`VideoOutDriver.cpp` `vblankLoop`). | Same (`libs/prx/libSceVideoOut/src/VideoOutDriver.cpp:504-521`). A flip waits for `lastFlipVblank + flipRate + 1` (`:350-362`). The swapchain present mode is hard-coded to FIFO (`libSceAgcDriver/Execution/src/VulkanDevice.cpp:847`, `:1471`). |
| A/V telemetry | None. | None. |

The architecture spec previously called AvPlayer coverage "zero". AvPlayer now provides a non-blocking offline state machine serving a blank frame stream and simulated event transitions so titles polling AvPlayer frame getters or event loops never crash or deadlock at boot. Real system media decoding (e.g., Media Foundation) remains scheduled for full FMV verification.

## Decision

This follows the decision table in [README.md](README.md#subsystem-specs) §Video / FMV:
- Keep the path where the title runs its own decoder.
- Carry PR #5's Bink-plane write-back through M1–M2 only as an interim general mechanism, *adjacent block-generation advance*, with no switch and no title reference. Replace it with general block-generation tracking in M3.
- Audit AvPlayer in M1.
- Decode system formats (H.264/H.265) with Media Foundation.
- FMV playback is a gate requirement. Skipping a video is not a pass.

## Target design

**Title-decoded path.** No FMV-specific runtime code is needed. Correctness comes from three general mechanisms:
- recompiler saveexec order and LDS barriers ([shader-recompiler.md](shader-recompiler.md));
- block-generation tracking of GPU-written surfaces, which supersedes `AdjacentGenerationEnabled` ([gpu-driver.md](gpu-driver.md), [guest-memory.md](guest-memory.md));
- capture ordering that never satisfies a wait from an unexecuted label.

**Interim, M1–M2: adjacent block-generation advance.** Until general tracking lands in M3, PR #5's write-back (`Texture.cpp:838-849`, `:2609-2641`) is ported with its Bink and video-plane naming removed. It is stated as a general rule: a GPU write to a surface also advances the write generation of the adjacent block it shares with a surface packed back to back, so that surface is not read stale. It has no switch and no title reference, and it applies to every surface. M3 deletes it when general block-generation tracking replaces it.

The `APS5_NO_ADJACENT_GENERATION` and `APS5_SAVEEXEC_WRITE_FIRST` switches are deleted, per the kill-switch rule in [configuration.md](configuration.md).

**AvPlayer path** (implemented only if the M1 inventory shows a gate title imports it):

| Piece | Design |
|---|---|
| Demux + decode | Media Foundation `IMFSourceReader`. Video out as NV12, audio out as 48 kHz PCM. Hardware decode where available, software MFT otherwise. |
| Guest file access | Sources resolve through the libc path resolver (`/app0/...`), so no guest path reaches MF unresolved. |
| Frame delivery | `sceAvPlayerGetVideoDataEx` copies the next due frame into a guest buffer allocated through the title's allocator callbacks. A frame is due when its PTS ≤ the player clock. |
| Player clock | Audio-master. It follows the samples the title has taken with `sceAvPlayerGetAudioData`, corrected by `audio_latency_ms` from [audio.md](audio.md). With no audio stream, it follows `steady_clock`. |
| State | `IsActive` goes false only at end of stream or on `Stop`. A decode error is reported through the title's event callback, never as a silent end of stream. |
| Unsupported calls | Anything past the audited set calls `Unsupported()`, which logs and aborts, so an unaudited call fails loudly. No throw crosses the `APS5_VABI` boundary ([threading.md](threading.md)). |

**A/V offset telemetry** (both paths). The runtime cannot see which audio sample belongs to which video frame inside a title's own decoder. It measures the offset it *adds* on top of the title's own sync:

`av_offset_ms = audio_latency_ms − video_latency_ms`

- `video_latency_ms` is the time from `sceVideoOutSubmitFlip` to `vkQueuePresentKHR` return, plus the swapchain images queued × the refresh period.
- It is sampled once per second while an FMV window is open. The run's maximum absolute value is `av_offset_ms_max` in the results JSON.
- The FMV window opens at the first frame-check reference of an FMV and closes at its end reference. This is title-agnostic: the per-title references define it, not code.
- AvPlayer runs additionally report `pts_minus_clock_ms` per delivered frame.

**"FMV played" rule** (local regression and full run). An FMV passes when:
- its first-frame and end-frame references match at SSIM ≥ 0.99 ([verification.md](verification.md) §2.3);
- the presented frames between them are at least 90% of the reference count;
- `av_offset_ms_max` ≤ 80.

Reaching the post-FMV state without the end reference fails the check, so a skip cannot pass.

## Interfaces

| Peer spec | Contract |
|---|---|
| [gpu-driver.md](gpu-driver.md) | General block-generation tracking and submit-time image resolve. Exposes `video_latency_ms` from the Presenter. |
| [guest-memory.md](guest-memory.md) | Per-block write generations (64 KiB blocks, with sub-block refinement) that the texture cache consumes. |
| [shader-recompiler.md](shader-recompiler.md) | Saveexec order, atomic-zero and LDS-barrier fixes. The Bink kernels go in the local golden corpus. |
| [audio.md](audio.md) | Supplies `audio_latency_ms` and `frames_consumed`. |
| [threading.md](threading.md) | The AvPlayer decode thread is a host thread and never holds guest locks. |
| [configuration.md](configuration.md) | `[display] present_mode` affects `video_latency_ms`. There are no FMV `[workarounds]` keys. |
| [verification.md](verification.md) | Frame references, the FMV window and the `av_offset_ms_max` field. |

## Failure modes

| Failure | Symptom | Handling |
|---|---|---|
| Stale shared block between planes | Band at plane start, zero chroma | Prevented by general generation tracking. The M3 stress test is the regression guard. |
| Capture reads unwritten guest memory | Wedge ("guest memory is not readable") | Capture-ordering redesign ([gpu-driver.md](gpu-driver.md)). The M3 exit requires 0 of them. |
| AvPlayer call on an unaudited path | `Unsupported()` logs and aborts | Kept deliberately. The inventory closes the gap. |
| MF lacks a codec on the host | `IMFSourceReader` fails | Report through the title's error callback and log the codec GUID. This counts as FMV failed, not skipped. |
| Audio device absent during FMV | The clock falls back to wall time | The offset is still reported, and the run is flagged `audio_device=none`. |
| Frame pacing below the FMV rate | Offset grows | Reported. The perf pass in M5 addresses it. |

## Tests

- **Hosted CI:**
  - Unit tests of the offset calculator on synthetic timestamps.
  - AvPlayer state machine GoogleTest suite (`AvPlayerStateMachineTests`): verifies player initialization/close, source attachment, automatic transition to Play, Pause/Resume lifecycle, StreamInfo resolution metadata, seeking timestamp offsets (`JumpToTime`), looping and trick-speed controls, stream enable/disable toggling, and video frame delivery with guest texture allocator callbacks (`sceAvPlayerGetVideoDataEx`).
  - AvPlayer state machine against a project-made H.264 clip in MP4, generated in CI with a permissively licensed encoder. It contains no game data. It asserts PTS ordering, `IsActive` at end of stream, and `pts_minus_clock_ms` within ±40 ms. Runs on the Windows runner, because MF is available there.
- **Hosted `driver-lavapipe`:** two surfaces packed back to back that share a 64 KiB block. A GPU write to one must not stale the other. This test replaces the Bink-specific evidence.
- **Local regression:** the first-frame and end-frame references for every FMV of each gate title, frame count ≥ 90%, and `av_offset_ms_max` ≤ 80.
- **M3 stress:** 3 × 150 s of the Demon's Souls intro with 0 wedges (ROADMAP M3 exit).

## Milestones

| Milestone | Work |
|---|---|
| M1 | - [ ] Port the saveexec fix. Port the interim adjacent block-generation advance (no switch, no title reference). AvPlayer coverage audit and codec inventory per gate title. Offset telemetry skeleton (`video_latency_ms`). |
| M2 | - [ ] FMV references and the "FMV played" rule wired into `tools/regress` for the 2D titles, if they contain video. |
| M3 | - [ ] General block-generation tracking replaces the interim adjacent block-generation advance, which is deleted. Tomb Raider FMV passes. Stress-run exit. |
| M4 | - [ ] Bugsnax FMV on the general path. |
| M5 | - [ ] Demon's Souls Bink FMVs from first level to credits within the A/V bar. |
| M6 | - [ ] Release full runs. |

## Open questions

- Do any gate titles use AvPlayer, or only title-decoded Bink? This is answered by the M1 inventory. AvPlayer work is skipped if none do.
- Is linking Media Foundation (an OS component) compatible with GPL-2.0-only distribution? It is believed to fall under the system-library exception, but this is not verified. Record it next to PRD R1.
- The 90% frame-count threshold is a starting value. Tune it after the first M2 runs, without title-specific values.
- Will FIFO-only presentation keep `video_latency_ms` low enough on 60 Hz hosts? Revisit when `[display] present_mode` lands.
