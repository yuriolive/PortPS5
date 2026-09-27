# Legal boundary

PortPS5 only ever processes dumps that a user decrypted from a console and games they own. The project stays clean-room with respect to Sony.

## Never commit, upload, paste or attach

- Keys, firmware, decryption code or tools that break console encryption.
- Sony libraries (`.sprx`), SDK headers, SDK docs, or leaked source of any kind. Don't read them either.
- Game data: dumps, executables, shader bytecode taken from games, textures, audio, saves, recorded input, screenshots or video frames, and converted executables.

This applies to the repo, CI artifacts, issues, PR descriptions and logs pasted into any of them.

## Allowed sources

- Public hardware documentation: the AMD RDNA2 ISA and addrlib.
- Open-source projects under compatible licences: AnyPS5, shadPS4, FreeBSD, Mesa and similar.
- Behaviour observed by running the user's own dumps locally.

## Derived data that may be published

- Metrics, hashes and pass/fail results in the results JSON (`docs/spec/verification.md` §4).
- Synthetic or hand-assembled test shaders written for this project.

## Licence

The project is GPL-2.0-only. Before copying code from another project, check that its licence is GPL-2.0-compatible and keep its copyright header. Apache-2.0 code is a known conflict (PRD risk R1); don't add new Apache-2.0 dependencies that get linked into shipped binaries.
