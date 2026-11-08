/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "proc.h"
#define TURBO_EXTRA_TARGETS 32u
/* Explicit opt-in for nonstandard homebrew title IDs. Never includes system IDs. */
typedef struct { unsigned count; char ids[TURBO_EXTRA_TARGETS][GA_TITLE_ID_MAX]; } turbo_targets_t;
typedef enum { TURBO_TARGET_NONE, TURBO_TARGET_PS5, TURBO_TARGET_PS4, TURBO_TARGET_EMULATOR, TURBO_TARGET_CUSTOM } turbo_target_kind_t;
int turbo_targets_load(turbo_targets_t *, const char *path);
turbo_target_kind_t turbo_target_kind(const ga_proc_t *, const turbo_targets_t *);
/* Return 0/1/2: none, exactly one, ambiguous. Self and duplicate PIDs excluded. */
unsigned turbo_target_select(const ga_proc_t *, unsigned, pid_t self,
                             const turbo_targets_t *, ga_proc_t *);
const char *turbo_target_label(turbo_target_kind_t);
