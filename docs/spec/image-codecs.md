# PortPS5 — Spec: Image codecs

Status: draft v1 · 2026-09-30

## Scope

Host-side JPEG and PNG encode/decode, shared by the PS5 replacement libraries that need them:
- `core/Decoder/Jpeg` and `core/Decoder/Png`: pure host static libraries over vendored stb (`3rdparty/stb`, MIT or public domain);
- `libSceJpegEnc`: JPEG encoder library on the shared encoder;
- `libScePngDec`: PNG decoder library on the shared decoder;
- `libScePngEnc`: still a set of unimplemented exports (see Open questions).

Out of scope: JPEG decoding through a `libSceJpegDec` library (none exists in the tree), MJPEG and restart-interval encoding, 16-bit PNG output, hardware-exact quantiser tables.

PRD bar owned here: none directly. The codecs support titles that thumbnail or screenshot through these libraries (M1 offline-behaviour work, so no gate title blocks on a missing library).

## Current state

Ported from AnyPS5 `main` (commits `94c73192`, `5853fec9`, `44208261`, `16290424`, `98a5228c`, `aa56049f`, `aad41aa9`, `6ef4ae3c`, `f9f02e37`, `a0e2f880`), adapted for PortPS5 rules. Tree: PortPS5.

| Area | State |
|---|---|
| `Decoder::Jpeg::Encode` / `Decode` (`core/Decoder/Jpeg/src/Jpeg.cpp`) | Implemented over stb. No throws: invalid arguments and failures return `nullopt`. Encoder output is always a 3-component YCbCr JFIF stream, so grayscale input decodes as neutral RGB. |
| `Decoder::Png::ParseHeader` / `Decode` / `Encode` (`core/Decoder/Png/src/Png.cpp`) | Implemented over stb. `Decode` always returns 8-bit RGBA. `Encode` accepts 1 to 4 channels. No throws. |
| `libSceJpegEnc` (`core/libs/prx/libSceJpegEnc/Export.cpp`) | `QueryMemorySize`, `Create`, `Delete`, `Encode` implemented for R8G8B8A8, B8G8R8A8, Y8U8Y8V8 and Y8 input. The handle header lives in guest work memory. MJPEG mode and restart intervals abort through `Unsupported()`. |
| `libScePngDec` (`core/libs/prx/libScePngDec/Export.cpp`) | `QueryMemorySize`, `Create`, `Delete`, `ParseHeader`, `Decode` implemented for R8G8B8A8 and B8G8R8A8 output. 16-bit output (`attribute == 1` with a 16-bit source) aborts through `Unsupported()`. |
| Build | `3rdparty/stb` is a pinned submodule (`2c980bb5`). stb implementations are compiled with `*_STATIC` into each decoder library, and both libraries use hidden visibility and PIC. |

## Decision

- One shared codec layer under `core/Decoder/` instead of stb copies per PRX, so JpegEnc, PngDec and any later PngEnc/JpegDec use one tested implementation.
- stb is licence-compatible with GPL-2.0-only (MIT or public domain, choose either; see `3rdparty/stb/LICENSE`). It is pinned as a submodule, not vendored piecemeal.
- The shared layer does not throw. Library wrappers map `nullopt` to SCE codes, or to `Unsupported()` when validation has already passed and only host allocation or encoder failure remains.
- Unsupported modes abort loudly instead of emitting wrong output (`.agents/rules/no-title-hacks.md`: no silent skips).

## Target design

```
libSceJpegEnc.prx --> decoder_jpeg (static, stb_image_write + stb_image, JPEG only)
libScePngDec.prx  --> decoder_png  (static, stb_image + stb_image_write, PNG only)
```

Interfaces are plain C++20 spans in and `std::optional<std::vector<uint8_t>>` out. All size arithmetic is done in `uint64_t` before narrowing; stb's own dimension cap (2^24) applies to decoding. `scePngDecDecode` bounds the required output (`(height-1)*pitch + rowBytes`) against the guest's `image_mem_size` before decoding, so a forged header cannot force a large allocation.

## Interfaces

| Export | Returns |
|---|---|
| `sceJpegEncQueryMemorySize(param)` | `0x800`, or `0x80650101` (null), `0x80650102` (bad size), `0x80650103` (bad attr) |
| `sceJpegEncCreate(param, mem, size, &h)` | `0`, or the codes above, plus `0x80650101` for null memory/handle and `0x80650102` for memory below `0x800` |
| `sceJpegEncDelete(h)` | `0`, or `0x80650104` for null, misaligned, forged or deleted handles |
| `sceJpegEncEncode(h, param, info)` | `0`, `0x80650104`, `0x80650101` (null or misaligned pointers), `0x80650102` (zero size, output too small), `0x80650103` (bad geometry or format combination) |
| `scePngDecQueryMemorySize(param)` | `0x20`, or `0x80690003` (null/bad attribute), `0x80690002` (`max_image_width` 0 or above 1000001) |
| `scePngDecCreate(param, mem, size, &h)` | `0`, the codes above, `0x80690001` (null memory/handle), `0x80690005` (memory below `0x20`) |
| `scePngDecDelete(h)` | `0`, or `0x80690004` |
| `scePngDecParseHeader(param, info)` | `0`, `0x80690003` (null param), `0x80690001`, `0x80690002`, `0x80690010` (not a PNG) |
| `scePngDecDecode(h, param, info)` | `(width<<16) OR height` when both fit in 15 bits, else `0`; or `0x80690004`, `0x80690003`, `0x80690001`, `0x80690002`, `0x80690010`, `0x80690012` (decode failure) |

All exports are `APS5_VABI` and `noexcept`.

## Failure modes

| Condition | Behaviour |
|---|---|
| Invalid arguments | SCE error code (table above). |
| Forged PNG dimensions | `ParseHeader` succeeds (structurally valid); `scePngDecDecode` returns `INVALID_SIZE` from the 64-bit bound check, or `DECODE_ERROR` if stb rejects the data. |
| JPEG `encode_mode == 1` (MJPEG), `restart_interval > 0` | `Unsupported()` (logs, aborts). |
| PNG 16-bit output requested | `Unsupported()`. |
| Host allocation failure while repacking or encoding | `Unsupported()` (host failure, not a console error). |
| Output buffer too small for the JPEG | `INVALID_SIZE`; nothing is written to the output. |

## Tests

All tests are GoogleTest through `portps5_add_gtest`, run in hosted CI (no GPU), and use images synthesised in the test.

- [x] `decoder_jpeg_tests` (`core/Decoder/Jpeg/tests/Jpeg.cpp`): RGB and grayscale round trip, quality/size monotonicity, invalid arguments, maximal dimensions with a tiny buffer (overflow), malformed input.
- [x] `decoder_png_tests` (`core/Decoder/Png/tests/Png.cpp`): lossless RGBA round trip, every channel count expanded to RGBA, `ParseHeader` fields and rejection cases, forged 2^31-1 dimensions, truncated input, invalid `Encode` arguments including stride overflow.
- [x] `guest_jpeg_enc_tests` (`core/libs/tests/GuestJpegEnc.cpp`): lifecycle and handle misuse (null, misaligned, forged, double delete), every argument error class, `height*pitch` overflow, RGBA/BGRA/Y8 encode, padded pitch, small output buffer canary, death tests for MJPEG and restart intervals.
- [x] `guest_png_dec_tests` (`core/libs/tests/GuestPngDec.cpp`): lifecycle, `ParseHeader` fields (colour spaces, tRNS flag) and errors, RGBA/BGRA decode, alpha fill versus source alpha, pitch handling, forged huge dimensions with a maximal pitch, argument validation order, death test for 16-bit output.

## Milestones

- [x] M1: shared JPEG/PNG codec layer, `libSceJpegEnc`, `libScePngDec` ([ROADMAP](../ROADMAP.md) Milestone 1).
- [ ] `libScePngEnc` on `Decoder::Png::Encode` (shared encoder exists; the library wrapper is not ported).
- [ ] 16-bit PNG output, MJPEG and restart-interval JPEG encoding, if a gate title needs them.

## Open questions

- **Guest pointer validation.** No guest-memory range-validation API is exported to PRXs. These libraries validate null, alignment and size arithmetic but read guest buffers with raw pointers, like the rest of the tree. Replace with the guest-memory API when it lands ([guest-memory.md](guest-memory.md)).
- **`scePngDecCreate` `max_image_width`** is stored but never enforced against the image width; the real library's behaviour is unverified.
- **`scePngDecDecode` return value** (`width<<16|height`, or `0` when a dimension exceeds 32767) is ported from AnyPS5 and not independently verified.
- **`sceJpegEncEncode`**: the `compression_ratio` to stb quality mapping is a linear approximation; `output_info->height` echoes the input height; a negative `restart_interval` is treated as no restart. None is verified against hardware.
- **Grayscale JPEG** (`Y8` input) is emitted as a 3-component stream because stb has no 1-component mode.
- **`libScePngEnc`**: AnyPS5 implemented it via `stb_image_write.h` and then dropped it again (`dc6baf62`, `08bec63e`, then `020579d4`); upstream `main` has stubs only. Whether a PNG encoder library is needed by a gate title is unknown.
- **PNG behaviour classes (checked against PngSuite, 2017-07-19, locally; no files committed).** All 162 valid suite images decode (interlaced, palette with and without tRNS, gray, 16-bit reduced to the high byte) and all corrupt `x*` files are rejected. stb itself ignored bad CRCs (`xcs*`, `xhd*`), so `Decoder::Png::Decode` now verifies critical-chunk CRCs and requires IEND; ancillary CRC errors are ignored. `ParseHeader` does not check CRCs, so a bad-IHDR-CRC file reports a header but fails `scePngDecDecode` with `DECODE_ERROR`. Whether the console library rejects bad CRCs is unverified.
- **JPEG behaviour classes (libjpeg-turbo `testimages`, locally; no files committed).** The two 8-bit Huffman files (`testorig.jpg`, `testimgint.jpg`) decode; 12-bit and arithmetic-coded files return `nullopt` (stb does not support them). Truncation sweeps and byte-flip corruption never crash or hang; forged 65535 x 65535 and zero-size frames are rejected without a large allocation.
- **stb as a decoder of guest-supplied data.** stb is not hardened against hostile input. Inputs are the user's own assets, but fuzzing the decode paths is worthwhile before 1.0.
