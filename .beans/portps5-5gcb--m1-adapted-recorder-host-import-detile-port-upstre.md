---
# portps5-5gcb
title: 'M1: Adapted Recorder + host-import + detile port (upstream PR #5 core)'
status: todo
type: task
priority: normal
tags:
    - beads:portps5-6
created_at: 2026-09-30T22:53:51Z
updated_at: 2026-09-30T22:53:51Z
---

## Description

Trial-ported on PR #28 and reverted: 32 APS5_* env switches trip policy, throws cross ABI, unwired without driver. Return adapted with the M1 driver port: typed [debug] config, return codes/abort path, file headers, GTest under driver-lavapipe. See docs/spec/gpu-driver.md Open questions.

## Acceptance Criteria

Recorder + ShaderReplay build, policy green, M1 gpu-driver box checked

Migrated from beads `portps5-6`.
