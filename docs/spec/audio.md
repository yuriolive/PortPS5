# PortPS5 — Spec: Audio

Status: draft v1 · 2026-09-27

## Scope

Guest audio output and decode: `libSceAudioOut` (AudioOut v1 ports and AudioOut2 contexts), `libSceAjm` / `libSceAjm.native` (ATRAC9 and other AJM codecs), `libSceNgs2`, `libSceAudio3d`, `libSceAudiodec`. Out of scope: audio input (`libSceAudioIn`), voice chat, pad speaker output. FMV audio is covered here only as far as it reaches AudioOut; FMV timing is in [video-fmv.md](video-fmv.md).

PRD bars owned here: F3 (ATRAC9 plus the title's mixing path; at most 1 underrun per 10 minutes) and the audio half of the ±80 ms FMV A/V offset.

## Current state

File references are relative to `core/libs/prx/`.

| Area | PortPS5 (forked from AnyPS5 `main@e06dbff`) | AnyPS5 `main@75a8668` (incl. merged PR #5) |
|---|---|---|
| AudioOut v1 | Ports feed the single host mixer (`AudioMixer`, one SDL F32 48 kHz stereo device) through per-port SPSC rings: 40 ms target cushion, 100 ms overrun ceiling, non-48 kHz ports resampled with `SDL_AudioStream`, bounded 200 ms blocking push that counts `audio.stall`. A null buffer drains the port's ring. Unknown formats return the invalid-argument code; host audio failures abort through `Unsupported()`. All exports are `APS5_VABI` + `noexcept`. | Same design. A null buffer now waits for the queue to drain (`AudioOut.cpp:187-205`). |
| AudioOut2 | 8 context + 4 port exports (`AudioOut2Context.cpp`, `AudioOut2Port.cpp`). Each context owns one mixer source instead of an SDL device. Queue level comes from the source ring above the 40 ms cushion and reports full when less than one grain fits under the 100 ms ceiling (with or without a device); no-device runs use the wall-clock fallback. Master gain is a continuous soft limiter applied once, in the mixer callback (no fixed 0.5 gain, no hard clamp). Context attributes are still ignored. Tracing is `debug.trace = ["audio"]` (no env switch). Telemetry: process `underruns`, `overrunDrops`, `stalls`, plus `AudioOut2LatencyMs` (ring fill + device period, or modelled grains with no device). | Implemented. One SDL F32 stereo 48 kHz device per context (`AudioOut2Context.cpp:92-113`). Queue level comes from SDL queued bytes minus a 40 ms silence cushion (`:30`, `:82-90`). Overrun guard at 250 ms drops grains (`:32`, `:133-136`). Blocking push gives up after 200 ms (`:34`, `:233`). Fixed master gain 0.5 plus a hard clamp (`:40`, `:127-130`). Context attributes are ignored (`:268-276`). |
| AudioOut2 ports | Port attribute id 0 = PCM pointer, id 1 = per-channel gain; ids 5, 8, 9 logged once per id when tracing, then ignored. Channel count from `data_format` bits 8-11 via `AudioOut2DecodeChannels`; unknown formats leave the port unrendered. 7.1 folds to stereo with C/surround at -3 dB and LFE folded in at -10 dB. `sampling_freq` is stored; the mix runs at 48 kHz and non-48 kHz output is resampled by the mixer source. | Port attribute id 0 = PCM pointer, id 1 = per-channel gain; ids 5, 8, 9 ignored (`AudioOut2Port.cpp:16-20`, `:148-170`). Channel count comes from `data_format` bits 8-11 (`:22-26`). 7.1 folds to stereo at -3 dB, and LFE is dropped (`:38-62`). `sampling_freq` is stored but never used; the mix assumes 48 kHz (`:116`). Object ports are mixed as plain mono or stereo, with no position. |
| AJM | ATRAC9 only (codec 1) via LibAtrac9 (`libSceAjm.native/src/Ajm.cpp`, static `atrac9` in its `CMakeLists.txt`). Other codecs log once and report `AJM_RESULT_CODEC_ERROR` per job. `sceAjmBatchStart` decodes synchronously; `sceAjmBatchWait` accepts and ignores its timeout. Tracing is `debug.trace = ["ajm"]`. All exports are `APS5_VABI` + `noexcept`. | ATRAC9 only (`CODEC_AT9 = 1`, `libSceAjm.native/src/Ajm.cpp:32`), via LibAtrac9 (`libSceAjm.native/CMakeLists.txt:3-18`). Other codecs log once and report `AJM_RESULT_CODEC_ERROR` per job (`Ajm.cpp:458-463`, `:402-406`). `sceAjmBatchStart` decodes synchronously on the calling thread. `sceAjmBatchWait` ignores its timeout (`:540-565`). |
| NGS2, Audio3d, Audiodec | Throw-stubs: NGS2.native 27, Audio3d 7, Audiodec.native 6. Identical in both trees. | Same. |
| Tracing | `debug.trace = ["audio", "ajm"]` (see [configuration.md](configuration.md)). No env switches remain on this path. | `APS5_TRACE_AUDIOOUT2` (`AudioOut2Context.cpp:44-47`) and `APS5_TRACE_AJM` (`Ajm.cpp:34-37`). |

Verified gaps closed in M2:
- Single host mixer (`AudioMixer`) consolidates v1 ports and AudioOut2 contexts onto one SDL device clock (F32 48 kHz WASAPI shared mode).
- Callback-driven architecture pulling from SPSC lock-free ring buffers, tracking monotonic `frames_consumed` output clock. Source retirement closes callback admission and waits for the selected consumer before clearing or reusing a ring. Fixed callback buffers process oversized requests in chunks.
- AudioOut v1 snapshots port settings and retains source ownership across each output; per-source producer serialization spans the queued-frame wait and push, with the port table mutex released during pacing. Closing a handle defers source reuse until its in-flight outputs finish.
- Host-device initialization failures are logged and remain retryable. AudioOut2 queue levels use the source ring when the mixer has a device; otherwise they use modelled grains.
- Real-time underrun accounting in callback when active unpaused sources experience a shortfall; overrun drop ceiling enforced at 100 ms ring fill.
- Dynamic resampler via `SDL_AudioStream` for non-48 kHz ports.
- 8-channel bed downmix folds LFE into front pair at -10 dB (gain ~0.316228) and C/RL/RR/SL/SR at -3 dB (gain ~0.707107).
- Soft limiter replaces fixed 0.5 gain, preventing hard clipping while retaining linear pass-through for normal amplitudes.

## Decision

This follows the decision table in [README.md](README.md#subsystem-specs) §Audio:
- **Adopt** AnyPS5 main's AudioOut2 (merged PR #5) and ATRAC9.
- Add codecs only as the M1 inventory requires them.
- Implement object-port panning.
- Use WASAPI shared mode behind SDL, with no exclusive mode in 1.0.

## Target design

1. **One host mixer per process.** A single SDL device (F32, 48 kHz, stereo or the device's native layout) runs in callback mode and pulls from a lock-free ring. AudioOut v1 ports and AudioOut2 contexts become sources on that one mixer, so there is one device clock. SDL is asked for its WASAPI backend in shared mode.
2. **Output clock.** The mixer keeps `frames_consumed`, advanced in the callback. The AudioOut2 queue level and v1 blocking waits come from the ring fill, measured against that clock. The queue level reports full unless another whole grain fits under the 100 ms ring ceiling, so a free slot is never reported when the push would be dropped. The wall-clock fallback model (`Drain`, `AudioOut2Context.cpp:71-79`) stays only for the no-device case.
3. **Underrun accounting.** An underrun is one callback that finds fewer frames in the ring than it needs, while at least one source is open and not paused. It is counted in the callback and emitted to telemetry as `audio.underrun` with a timestamp. Overrun drops (the current 250 ms guard) are counted separately as `audio.overrun_drop`.
4. **Latency budget.** The ring target is 40 ms (the current cushion), with a ceiling of 100 ms. The measured output latency is published as `audio_latency_ms`: ring fill plus the device period SDL reports. [video-fmv.md](video-fmv.md) uses it to compute the A/V offset.
5. **Mix quality.**
   - Resample ports whose `sampling_freq` is not 48 kHz (SDL_AudioStream), instead of silently assuming 48 kHz.
   - Fold LFE into the front pair at -10 dB rather than dropping it.
   - Replace the fixed 0.5 gain with a soft limiter. *Inference:* 0.5 was tuned on one title's peak (a comment at `AudioOut2Context.cpp:37-39` cites a 2.1 peak), which makes it a title-derived constant that the no-title-code policy disallows.
6. **Object ports.** Decode the position attributes (ids 5, 8, 9 are candidates; their meaning is unverified) and apply constant-power stereo panning. Until they are decoded, an unknown attribute id is logged once per id.
7. **Codecs.** An AJM codec is added only when the M1 import inventory shows a gate title needs it. An unknown codec keeps today's behaviour: log once, then return a codec error per job. It never silently produces zeros.
8. **NGS2, Audio3d, Audiodec.** Their stubs call `Unsupported()`, which logs and aborts, until the inventory names a gate title that imports them. No throw crosses the `APS5_VABI` boundary ([threading.md](threading.md)). Each one is then implemented as a source on the host mixer.

## Interfaces

| Peer spec | Contract |
|---|---|
| [video-fmv.md](video-fmv.md) | Audio publishes `frames_consumed` and `audio_latency_ms`. Video computes the A/V offset from them. |
| [threading.md](threading.md) | Blocking pushes and `sceAjmBatchWait` sleep on futex-based waits, not `sleep_for` polling (`AudioOut2Context.cpp:234-236`). The callback thread never takes a guest lock. |
| [guest-memory.md](guest-memory.md) | Port PCM pointers are guest memory read at mix time. The mixer copies each grain at push time, so guest rewrites cannot tear a callback. |
| [configuration.md](configuration.md) | `debug.trace = ["audio", "ajm"]` replaces the two `APS5_TRACE_*` variables. There are no audio `[workarounds]` keys. |
| [build-toolchain.md](build-toolchain.md) | LibAtrac9 stays a static submodule. The SDL build must keep `SDL_AUDIO` with the WASAPI backend enabled. |
| [verification.md](verification.md) | Telemetry fields `audio_underruns` and `audio_overrun_drops` per run. The results JSON needs an `audio_underruns` field, which is not yet in the schema. |

## Failure modes

| Failure | Detection | Handling |
|---|---|---|
| No audio device, or the device is lost | SDL open fails or the device is removed | Fall back to the wall-clock model, so the title keeps pacing. Log once and retry opening every 2 s. |
| Underrun (guest mixer starved) | Callback count | Output silence, count the event, and never block the guest. |
| Guest pushes faster than real time | Ring above 100 ms | Drop the grain and count it. This was the cause of the earlier 4-5× over-speed. |
| Unsupported codec or port format | `InstanceCreate` / `PortCreate` | Log once, return a codec error or skip the port. Never produce fabricated PCM. |
| Unknown AudioOut2 attribute | `SetAttributes` | Log once per id, then ignore. |
| Blocking push on a stuck device | 200 ms timeout | Return, and count it as a stall input to the watchdog (`audio.stall`, `AudioMixer::GetStalls`, distinct from `audio.overrun_drop`). |

## Tests

- **GoogleTest Unit Suites** (`ctest -L unit`, hosted `unit` job):
  - Downmix matrix, LFE fold, and soft-limiter headroom on synthetic multi-channel buffers.
  - `data_format` channel decoding and interleaving.
  - Resampler length, phase interpolation, and drift compensation.
  - Source retirement versus callback selection, chunked mixing with silent tails, device-open retry, and wall-clock reset synchronization.
  - AudioOut v1 pacing versus concurrent close/reopen and multiple producers; AudioOut2 source-ring queue reporting with a host device.
  - AJM job parsing with synthetic command batches (no game assets).
  - ATRAC9 header and frame decoding of project-generated bitstreams.
  - Death tests (`EXPECT_DEATH`): verify that malformed AJM batches trigger an immediate abort via `Unsupported()` rather than corrupting audio ring buffers.
- **Ported Ecosystem Test Suites:**
  - [x] **KytyPS5 `AudioOut2PortTests`:** port lifecycle stress (slot reuse, table growth, concurrent create), destroy-releases-ports, unknown-format skip, late-read grains, and depth-1 queue backpressure (`core/libs/tests/AudioOut2KytyPorts.cpp`; adapted: single mixer with no per-port devices, 1/2/8 channels only, late PCM read at push time, unbounded port table, non-blocking opens).
  - [x] **SharpEMU audio suites:** v1 batch fan-out to every port and S16 conversion endpoints/volume (`AudioOut2Tests.V1BatchSubmitsEveryPort`, `V1S16StereoConversionAndVolume` in `core/libs/tests/AudioOut2.cpp`); AJM concurrent instance creation (`AudioAjmTests.ConcurrentInstanceCreates` in `core/libs/tests/AudioAjm.cpp`). Register-marshalling trampoline tests, generation-specific registries, ACM codecs, and NaN sanitization have no equivalent surface here and are not ported.
- **Driverless mixer test:** a dummy SDL audio driver consumes at a fixed rate. It asserts 0 underruns over 10 simulated minutes at a steady push rate, and that exactly N underruns are counted when N gaps are injected.
- **Local regression:** every gate run reports `audio_underruns`. The pass is ≤ 1 per 10 minutes, pro-rated over the run.

## Milestones

| Milestone | Audio work |
|---|---|
| M1 | - [x] Port AudioOut2 and ATRAC9 from AnyPS5 main (merged PR #5). Build the codec, NGS2 and Audio3d inventory per gate title. Add underrun and latency telemetry. Remove `APS5_TRACE_AUDIOOUT2` and `APS5_TRACE_AJM`. |
| M2 | - [x] Single host mixer with the resampler and the soft limiter. - [ ] TMNT proves the mixing path. Underrun bar enforced in the full run (needs a local title run with published results; unit tests alone do not close this). |
| M3 | - [ ] Tomb Raider FMV audio within the A/V bar, jointly with [video-fmv.md](video-fmv.md). |
| M4 | - [ ] Any codec or NGS2 surface that the inventory flags for Bugsnax. |
| M5 | - [ ] Object-port panning, validated on Demon's Souls. |
| M6 | - [ ] Release full runs meet F3. |

## Open questions

- Which AJM codecs besides ATRAC9 do the gate titles import? The M1 inventory (`core/libs/prx/libSceAudioOut/IMPORT_INVENTORY.md`, from export-table inspection only, no game data) records current capability and what each local dump run must confirm.
- The meaning of AudioOut2 attribute ids 5, 8 and 9 (object position is suspected, not verified).
- Should the mixer output 5.1/7.1 when the host device supports it? 1.0 assumes stereo.
- `sceAjmBatchStart` decodes on the guest thread. Does this cost measurable frame time in Demon's Souls? Profile it in M5 before adding a worker.
