/* SPDX-License-Identifier: GPL-3.0-or-later
 * Self-contained position-independent block copied into a PS5 game.
 * No libc calls, float constants, or references outside this block.
 */
#include "stub.h"
#ifdef __APPLE__
#define STUB __attribute__((noinline, used))
#else
#define STUB __attribute__((section(".text.gastub"), noinline, used))
#endif
#define INLINE __attribute__((always_inline)) inline

static INLINE uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static INLINE uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p+4)<<32);
}
static INLINE void put32(uint8_t *p, uint32_t v) {
    p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24;
}
static INLINE int usable(const ga_args_t *a) {
    return a && a->magic==GA_MAGIC && a->version==GA_LAYOUT_VERSION;
}
/* Caller holds the shared nonblocking lock. */
static INLINE turbo_pad_t *pad_for(ga_args_t *a,int32_t handle) {
    for (unsigned i=0;i<TURBO_PADS;i++)
        if (a->pads[i].used && a->pads[i].handle==handle) return &a->pads[i];
    for (unsigned i=0;i<TURBO_PADS;i++) if (!a->pads[i].used) {
        a->pads[i].used=1;a->pads[i].handle=handle;return &a->pads[i];
    }
    return 0;
}
static INLINE void feedback_error(ga_args_t *a) {
    __atomic_add_fetch(&a->feedback_failed,1u,__ATOMIC_RELAXED);
}
static INLINE void queue_feedback(ga_args_t *a,turbo_pad_t *p) {
    if (!a->feedback_create || !a->feedback_detach || !a->feedback_sleep ||
        !a->feedback_entry || !a->orig[GA_FN_SET_VIBRATION] ||
        __atomic_load_n(&p->feedback_disabled,__ATOMIC_ACQUIRE)) {
        feedback_error(a);return;
    }
    if (!p->feedback_owner) p->feedback_owner=(uint64_t)(uintptr_t)a;
    uint32_t request=__atomic_load_n(&p->feedback_request,__ATOMIC_RELAXED);
    request=((request+2u)&~1u)|(p->muted?1u:0u);
    __atomic_store_n(&p->feedback_request,request,__ATOMIC_RELEASE);
    if (__atomic_exchange_n(&p->feedback_running,1u,__ATOMIC_ACQ_REL)) return;
    typedef int32_t (*create_fn)(uint64_t *,const void *,void *(*)(void *),void *,const char *);
    typedef int32_t (*detach_fn)(uint64_t);
    uint64_t thread=0;
    if (((create_fn)(uintptr_t)a->feedback_create)(&thread,0,
        (void *(*)(void *))(uintptr_t)a->feedback_entry,p,a->feedback_name)) {
        __atomic_store_n(&p->feedback_running,0u,__ATOMIC_RELEASE);feedback_error(a);return;
    }
    if (((detach_fn)(uintptr_t)a->feedback_detach)(thread)) {
        /* Do not keep allocating joinable threads if this ABI cannot detach. */
        __atomic_store_n(&p->feedback_disabled,1u,__ATOMIC_RELEASE);feedback_error(a);
    }
}
static INLINE void transform(ga_args_t *a, uint8_t *d, int32_t handle) {
    uint32_t age=__atomic_add_fetch(&a->age,1u,__ATOMIC_RELAXED);
    if (!a->enabled || age>GA_STALE_READS) return;
    /* Concurrent callers pass through instead of blocking a game thread. */
    if (__atomic_exchange_n(&a->lock,1u,__ATOMIC_ACQUIRE)) return;
    turbo_pad_t *p=pad_for(a,handle);
    if (!p) goto done;
    uint32_t shortcuts=a->shortcuts&TURBO_SHORTCUT_ALL;
    if (p->shortcuts_seen!=shortcuts) {
        p->shortcuts_seen=shortcuts;p->chord=0;p->muted=0;
    }
    uint32_t buttons=u32(d), mask=a->mask&TURBO_ALLOWED;
    uint64_t ts=u64(d+0x50);
    if (!d[0x4c] || (buttons&0x80000000u) || !ts) {
        p->held=0;p->last_ts=0;p->chord=0;
        goto done;
    }
    uint32_t half=a->half_us;
    if (half<10000u || half>1000000u) goto done;
    if (p->last_ts && ts<p->last_ts) goto done; /* old buffered sample */
    if (p->last_ts && ts-p->last_ts>2000000u) {p->held=0;p->chord=0;}
    uint32_t chord=(shortcuts&TURBO_SHORTCUT_DOWN) &&
                   (buttons&TURBO_CHORD)==TURBO_CHORD;
    uint32_t state=chord?1u:0u;
    int toggle=chord && !(p->chord&1u);
    if ((shortcuts&TURBO_SHORTCUT_STICKS) &&
        (buttons&TURBO_STICK_CHORD)==TURBO_STICK_CHORD) {
        state|=2u;
        if (!(p->chord&2u)) p->since[0]=ts;
        else if (p->chord&4u) state|=4u;
        else if (ts-p->since[0]>=2000000u) {toggle=1;state|=4u;}
    }
    if (toggle) {p->muted=!p->muted;queue_feedback(a,p);}
    p->chord=state;
    uint32_t held=buttons&mask;
    uint32_t output=buttons;
    /* Disabled shortcuts pass through. The long-press chord always reaches
     * the game so it does not swallow short awakening presses. */
    if (chord) output&=~TURBO_CHORD;
    for (unsigned b=1;b<16;b++) {
        uint32_t bit=1u<<b;
        if (!(held&bit)) continue;
        if (!(p->held&bit)) p->since[b]=ts;
        if (!p->muted && !chord && (((ts-p->since[b])/half)&1u)) output&=~bit;
    }
    p->held=held;p->last_ts=ts;
    /* Analog trigger values must follow the released digital trigger. */
    if (!p->muted && !chord) {
        if ((held&0x100u) && !(output&0x100u)) d[8]=0;
        if ((held&0x200u) && !(output&0x200u)) d[9]=0;
    }
    put32(d,output);
    if (output!=buttons) __atomic_add_fetch(&a->applied,1u,__ATOMIC_RELAXED);
done:
    __atomic_store_n(&a->lock,0u,__ATOMIC_RELEASE);
}
STUB int32_t ga_stub_read_state(int32_t h,uint8_t *d,ga_args_t *a) {
    typedef int32_t (*fn)(int32_t,uint8_t *);
    int32_t r=((fn)(uintptr_t)a->orig[GA_FN_READ_STATE])(h,d);
    __atomic_add_fetch(&a->calls,1u,__ATOMIC_RELAXED);
    if (!r && d && usable(a)) transform(a,d,h);
    return r;
}
STUB int32_t ga_stub_read_state_ext(int32_t h,uint8_t *d,ga_args_t *a) {
    typedef int32_t (*fn)(int32_t,uint8_t *);
    int32_t r=((fn)(uintptr_t)a->orig[GA_FN_READ_STATE_EXT])(h,d);
    __atomic_add_fetch(&a->calls,1u,__ATOMIC_RELAXED);
    if (!r && d && usable(a)) transform(a,d,h);
    return r;
}
STUB int32_t ga_stub_read(int32_t h,uint8_t *d,int32_t count,ga_args_t *a) {
    typedef int32_t (*fn)(int32_t,uint8_t *,int32_t);
    int32_t r=((fn)(uintptr_t)a->orig[GA_FN_READ])(h,d,count);
    __atomic_add_fetch(&a->calls,1u,__ATOMIC_RELAXED);
    if (r>0 && count>0 && d && usable(a))
        for (int32_t i=0;i<r && i<count && i<64;i++) transform(a,d+(uint32_t)i*GA_PAD_SIZE,h);
    return r;
}
STUB int32_t ga_stub_read_ext(int32_t h,uint8_t *d,int32_t count,ga_args_t *a) {
    typedef int32_t (*fn)(int32_t,uint8_t *,int32_t);
    int32_t r=((fn)(uintptr_t)a->orig[GA_FN_READ_EXT])(h,d,count);
    __atomic_add_fetch(&a->calls,1u,__ATOMIC_RELAXED);
    if (r>0 && count>0 && d && usable(a))
        for (int32_t i=0;i<r && i<count && i<64;i++) transform(a,d+(uint32_t)i*GA_PAD_SIZE,h);
    return r;
}
STUB int32_t ga_stub_set_vibration(int32_t h,const uint8_t *motors,ga_args_t *a) {
    typedef int32_t (*fn)(int32_t,const uint8_t *);
    int32_t r=((fn)(uintptr_t)a->orig[GA_FN_SET_VIBRATION])(h,motors);
    if (!r && motors && usable(a) && !__atomic_exchange_n(&a->lock,1u,__ATOMIC_ACQUIRE)) {
        turbo_pad_t *p=pad_for(a,h);
        if (p) __atomic_store_n(&p->game_motors,0x80000000u|motors[0]|((uint32_t)motors[1]<<8),__ATOMIC_RELEASE);
        __atomic_store_n(&a->lock,0u,__ATOMIC_RELEASE);
    }
    return r;
}
STUB int32_t ga_stub_set_vibration_mode(int32_t h,int32_t mode,ga_args_t *a) {
    typedef int32_t (*fn)(int32_t,int32_t);
    int32_t r=((fn)(uintptr_t)a->orig[GA_FN_SET_VIBRATION_MODE])(h,mode);
    if (!r && mode>=0 && mode<=2 && usable(a) && !__atomic_exchange_n(&a->lock,1u,__ATOMIC_ACQUIRE)) {
        turbo_pad_t *p=pad_for(a,h);
        if (p) __atomic_store_n(&p->game_mode,0x80000000u|(uint32_t)mode,__ATOMIC_RELEASE);
        __atomic_store_n(&a->lock,0u,__ATOMIC_RELEASE);
    }
    return r;
}
/* Short-lived detached thread: stop pulses even if the game stops reading
 * input. No sleeping, waiting or spinning occurs in a game's read callback. */
STUB void *ga_stub_feedback(void *context) {
    turbo_pad_t *p=context;
    ga_args_t *a=(ga_args_t *)(uintptr_t)p->feedback_owner;
    typedef int32_t (*vib_fn)(int32_t,const uint8_t *);
    typedef int32_t (*mode_fn)(int32_t,int32_t);
    typedef int32_t (*sleep_fn)(uint32_t);
    vib_fn vibrate=(vib_fn)(uintptr_t)a->orig[GA_FN_SET_VIBRATION];
    mode_fn mode=(mode_fn)(uintptr_t)a->orig[GA_FN_SET_VIBRATION_MODE];
    sleep_fn nap=(sleep_fn)(uintptr_t)a->feedback_sleep;
    for (;;) {
        uint32_t request=__atomic_load_n(&p->feedback_request,__ATOMIC_ACQUIRE);
        if (a->enabled) {
            uint8_t motors[2];
            uint32_t previous_mode=__atomic_load_n(&p->game_mode,__ATOMIC_ACQUIRE);
            /* Only change a mode we can restore. An unknown native haptics
             * mode must not be permanently replaced just for a UI cue. */
            if (mode && (previous_mode&0x80000000u) && mode(p->handle,2)) feedback_error(a);
            unsigned pulses=(request&1u)?1u:2u;
            for (unsigned i=0;i<pulses && a->enabled;i++) {
                motors[0]=96;motors[1]=64;
                if (vibrate(p->handle,motors)) feedback_error(a);
                if (nap((request&1u)?180000u:80000u)) feedback_error(a);
                motors[0]=0;motors[1]=0;
                if (vibrate(p->handle,motors)) feedback_error(a);
                if (i+1<pulses && nap(60000u)) feedback_error(a);
            }
            uint32_t saved_mode=__atomic_load_n(&p->game_mode,__ATOMIC_ACQUIRE);
            if (mode && (saved_mode&0x80000000u) && mode(p->handle,(int32_t)(saved_mode&0x7fffffffu))) feedback_error(a);
            uint32_t saved=__atomic_load_n(&p->game_motors,__ATOMIC_ACQUIRE);
            if (saved&0x80000000u) {
                motors[0]=(uint8_t)saved;motors[1]=(uint8_t)(saved>>8);
                if (vibrate(p->handle,motors)) feedback_error(a);
            }
            __atomic_add_fetch(&a->feedback_sent,1u,__ATOMIC_RELAXED);
        }
        /* Serialize the final request check with the read callback. A request
         * cannot be lost between declaring the worker idle and returning. */
        while (__atomic_exchange_n(&a->lock,1u,__ATOMIC_ACQUIRE))
            if (nap(1000u)) feedback_error(a);
        if (__atomic_load_n(&p->feedback_request,__ATOMIC_ACQUIRE)==request || !a->enabled) {
            __atomic_store_n(&p->feedback_running,0u,__ATOMIC_RELEASE);
            __atomic_store_n(&a->lock,0u,__ATOMIC_RELEASE);
            return 0;
        }
        __atomic_store_n(&a->lock,0u,__ATOMIC_RELEASE);
    }
}
STUB void ga_stub_end(void) {}
