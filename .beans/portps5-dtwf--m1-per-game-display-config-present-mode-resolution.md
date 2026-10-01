---
# portps5-dtwf
title: 'M1: Per-game display config (present_mode, resolution_scale)'
status: todo
type: task
priority: normal
created_at: 2026-09-30T23:49:35Z
updated_at: 2026-09-30T23:49:35Z
---

## Context

Config accepts display.present_mode and display.resolution_scale (Config.cpp:438-473) but the driver ignores them: swapchain creation hard-codes VK_PRESENT_MODE_FIFO_KHR (Execution/src/VulkanDevice.cpp:501 and :578) and there is no resolution-scale control. Specs: docs/spec/configuration.md, docs/spec/gpu-driver.md.

## Higher Goal

PRD F6: resolution scale and present mode are per-game settings, with a documented fallback to fifo when the surface lacks a mode.

## Acceptance Criteria

- [ ] Swapchain present mode chosen from display.present_mode with a logged fallback to fifo
- [ ] Render scale applied from display.resolution_scale, decision recorded on whether it enters pipeline-cache keys
- [ ] GoogleTest for mode selection on a fake surface capability set
- [ ] configuration.md and gpu-driver.md updated

## Out of Scope

A user-override config directory, fullscreen and window_percent behaviour changes.

## Summary of Changes

TBD
