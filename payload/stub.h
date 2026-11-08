/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "hook.h"
#include <stdint.h>
#define GA_PAD_SIZE 120u
#define GA_STALE_READS 600u
#define TURBO_PADS 8u
#define TURBO_ALLOWED 0x0000ff00u /* face buttons, L1/R1, L2/R2 only */
#define TURBO_CHORD 0x00000c40u /* L1 + R1 + D-pad DOWN */
#define TURBO_STICK_CHORD 0x00000006u
#define TURBO_SHORTCUT_DOWN 1u
#define TURBO_SHORTCUT_STICKS 2u
#define TURBO_SHORTCUT_ALL 3u
#define TURBO_HALF_US 25000u /* fixed 20 complete press/release cycles per second */
typedef struct {
    int32_t handle;
    uint32_t used, held, chord, muted, shortcuts_seen;
    uint64_t last_ts, since[16];
    uint64_t feedback_owner;
    uint32_t feedback_request, feedback_running, feedback_disabled;
    uint32_t game_motors, game_mode; /* atomically tracked game output, high bit = known */
} turbo_pad_t;
typedef struct {
    uint32_t magic, version;
    uint64_t orig[GA_FN_COUNT];
    uint64_t feedback_create, feedback_detach, feedback_sleep, feedback_entry;
    char feedback_name[16];
    /* Service-owned settings, contiguous for ga_inject_publish. */
    volatile uint32_t enabled, mask, half_us, shortcuts, settings_seq;
    /* Stub-owned state. Service only resets age as a heartbeat. */
    volatile uint32_t age;
    uint32_t lock;
    uint64_t calls, applied;
    uint64_t feedback_sent, feedback_failed;
    turbo_pad_t pads[TURBO_PADS];
    uint32_t hook_count;
    uint64_t hook_slot[GA_MAX_SLOTS], hook_orig[GA_MAX_SLOTS];
    uint32_t hook_prot[GA_MAX_SLOTS];
} ga_args_t;
_Static_assert(__builtin_offsetof(ga_args_t, pads) >
               __builtin_offsetof(ga_args_t, settings_seq), "state must not be published");
int32_t ga_stub_read_state(int32_t, uint8_t *, ga_args_t *);
int32_t ga_stub_read_state_ext(int32_t, uint8_t *, ga_args_t *);
int32_t ga_stub_read(int32_t, uint8_t *, int32_t, ga_args_t *);
int32_t ga_stub_read_ext(int32_t, uint8_t *, int32_t, ga_args_t *);
int32_t ga_stub_set_vibration(int32_t, const uint8_t *, ga_args_t *);
int32_t ga_stub_set_vibration_mode(int32_t, int32_t, ga_args_t *);
void *ga_stub_feedback(void *);
void ga_stub_end(void);
