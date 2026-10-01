---
# portps5-jvsq
title: 'libScePngDec: enforce max_image_width on decode'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:51:39Z
updated_at: 2026-10-01T18:33:27Z
parent: portps5-7dk3
---


## Context

scePngDecCreate validates max_image_width's range (core/libs/prx/libScePngDec/Export.cpp:85-86) but the stored limit is not enforced against the decoded image, and the w<<16|h return encoding is unverified on hardware. Spec: docs/spec/image-codecs.md Open questions.

## Higher Goal

Decode honours the creation-time limits the guest asked for.

## Acceptance Criteria

- [ ] Decode rejects images wider than max_image_width with the SCE size error
- [ ] GoogleTest for the boundary width
- [ ] image-codecs.md Open question closed or updated

## Out of Scope

libScePngEnc.

## Summary of Changes

TBD
