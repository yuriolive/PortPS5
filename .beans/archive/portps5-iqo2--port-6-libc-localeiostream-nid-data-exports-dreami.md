---
# portps5-iqo2
title: Port 6 libc locale/iostream NID data exports (Dreaming Sarah gap)
status: completed
type: task
priority: high
tags:
    - beads:portps5-53
created_at: 2026-09-30T22:53:50Z
updated_at: 2026-09-30T22:53:50Z
---

## Description

Link-level gap: 6/484 game NIDs missing, all libc #T#T libstdc++ ABI data. Upstream implements all 6 in core/libs/prx/libc/src/LocaleSupport.cpp:143-154 @75a8668 (first in 9ac0798). Port declarations only (no GuestLocale shim): Cv+zC4EjGMA ctype<char>::id, VmqsS6auJzo ctype<wchar_t>::id, E14mW8pVpoE num_put id, 1kZFcktOm+s num_put vtable[12], irGo1yaJ-vM collate<wchar_t>::id, H4fcpQOpc08 locale::_Id_cnt. Add GTest NID-presence coverage and spec touch per implement-prx-function.

## Acceptance Criteria

objdump shows all 6 in patched libc.prx; NID-presence GTest green; registry 484/484 resolve

Migrated from beads `portps5-53`.
