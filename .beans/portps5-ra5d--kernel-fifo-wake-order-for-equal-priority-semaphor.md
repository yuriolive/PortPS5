---
# portps5-ra5d
title: 'Kernel: FIFO wake order for equal-priority semaphore waiters'
status: todo
type: task
priority: low
created_at: 2026-09-30T23:51:57Z
updated_at: 2026-10-01T18:33:26Z
parent: portps5-7dk3
---


## Context

docs/spec/threading.md Open question 3 asks whether FIFO order matters beyond FIFO-attributed semaphores. The futex-based semaphore wakes through WaitOnAddress, which gives no equal-priority FIFO guarantee. PR #61 (merged) ported the pthread semaphore on the same primitive.

## Higher Goal

Decide and document whether equal-priority waiters must wake in FIFO order, and implement it if a gate title needs it.

## Acceptance Criteria

- [ ] Decision recorded in threading.md
- [ ] If required, an intrusive waiter queue with a test for ordering
- [ ] No behaviour change if not required

## Out of Scope

Broadcast requeue optimizations.

## Summary of Changes

TBD
