/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stub.h"
#include "pad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ga_pad_data_t sample;
static int result=0;
static int batch_count=1;
static int32_t read_state(int32_t h,uint8_t *d) {(void)h;memcpy(d,&sample,sizeof(sample));return result;}
static int32_t read_batch(int32_t h,uint8_t *d,int32_t n) {
    (void)h;
    for (int i=0;i<n && i<batch_count;i++) memcpy(d+i*GA_PAD_SIZE,&sample,sizeof(sample));
    return batch_count;
}
static ga_args_t a;
static void init(void) {
    memset(&a,0,sizeof(a));memset(&sample,0,sizeof(sample));
    a.magic=GA_MAGIC;a.version=GA_LAYOUT_VERSION;a.enabled=1;
    a.mask=GA_BTN_CROSS;a.half_us=50000;a.shortcuts=TURBO_SHORTCUT_DOWN;
    a.orig[GA_FN_READ_STATE]=(uintptr_t)read_state;
    a.orig[GA_FN_READ_STATE_EXT]=(uintptr_t)read_state;
    a.orig[GA_FN_READ]=(uintptr_t)read_batch;
    a.orig[GA_FN_READ_EXT]=(uintptr_t)read_batch;
    sample.connected=1;sample.timestamp_us=1000000;
    result=0;
}
static ga_pad_data_t run(int h,uint32_t buttons,uint64_t time) {
    ga_pad_data_t d;sample.buttons=buttons;sample.timestamp_us=time;
    assert(ga_stub_read_state(h,(uint8_t *)&d,&a)==result);return d;
}
int main(void) {
    init();uint32_t x=GA_BTN_CROSS,other=GA_BTN_CIRCLE;
    assert(run(1,x|other,1000000).buttons==(x|other));
    assert(run(1,x|other,1049999).buttons==(x|other));
    assert(run(1,x|other,1050000).buttons==other);
    assert(run(1,x|other,1050000).buttons==other); /* duplicate */
    assert(run(1,x|other,1100000).buttons==(x|other));
    assert(run(1,other,1110000).buttons==other);
    assert(run(1,x,1120000).buttons==x); /* re-press starts on */
    assert(run(1,x,1170000).buttons==0);
    assert(run(2,x,1170000).buttons==x); /* separate pad phase */
    assert(run(2,0,1180000).buttons==0);
    uint32_t sticks=GA_BTN_L3|GA_BTN_R3;
    assert(run(1,x|sticks,1180000).buttons==sticks); /* game chord survives off phase */
    assert(run(1,x|sticks,1181000).buttons==sticks); /* held chord never toggles */
    assert(run(1,x,1190000).buttons==0);
    assert(run(1,x|sticks,1220000).buttons==(x|sticks));
    assert(run(1,x|sticks,1270000).buttons==sticks); /* turbo stays active */
    assert(run(1,GA_BTN_L3,1280000).buttons==GA_BTN_L3);
    assert(run(1,GA_BTN_R3,1290000).buttons==GA_BTN_R3);
    assert(run(1,sticks,1300000).buttons==sticks);
    assert(run(1,x|sticks,1310000).buttons==(x|sticks));
    assert(run(1,x|sticks,1360000).buttons==sticks);
    a.enabled=0;assert(run(1,x,1371000).buttons==x);
    a.enabled=1;a.age=GA_STALE_READS;
    assert(run(1,x,1372000).buttons==x); /* watchdog pass-through */
    init();a.mask=GA_BTN_R2;sample.triggers.r2=200;
    run(1,GA_BTN_R2,1000000);
    ga_pad_data_t d=run(1,GA_BTN_R2|other,1050000);
    assert(d.buttons==other && d.triggers.r2==0);
    assert(d.triggers.l2==sample.triggers.l2);
    init();run(1,x,1000000);sample.connected=0;
    assert(run(1,x,1050000).buttons==x);
    sample.connected=1;assert(run(1,x,1060000).buttons==x);
    assert(run(1,x|GA_BTN_INTERCEPT,1110000).buttons==(x|GA_BTN_INTERCEPT));
    assert(run(1,x,1120000).buttons==x);
    init();run(1,x,1000000);assert(run(1,x,3100000).buttons==x); /* resume */
    init();result=-1;assert(run(1,x,1000000).buttons==x); /* failed read */
    init();a.half_us=0;assert(run(1,x,1000000).buttons==x);
    init();run(1,x,1000000);
    sample.buttons=x;sample.timestamp_us=1050000;batch_count=2;
    struct {ga_pad_data_t d[2];uint64_t guard;} b;
    b.guard=0x123456789abcdef0ULL;
    assert(ga_stub_read(1,(uint8_t *)b.d,2,&a)==2);
    assert(!b.d[0].buttons && !b.d[1].buttons && b.guard==0x123456789abcdef0ULL);
    /* Even an invalid API return may never make the hook overrun count. */
    batch_count=64;sample.timestamp_us=1060000;
    memset(&b.d[1],0x5a,sizeof(b.d[1]));
    assert(ga_stub_read(1,(uint8_t *)b.d,1,&a)==64);
    assert(b.guard==0x123456789abcdef0ULL);
    for (unsigned i=0;i<sizeof(b.d[1]);i++) assert(((uint8_t *)&b.d[1])[i]==0x5a);
    init();run(1,x,1000000);sample.buttons=x;sample.timestamp_us=1050000;
    assert(!ga_stub_read_state_ext(1,(uint8_t *)&d,&a) && !d.buttons);
    /* PS4's connected flag is a byte; padding must not fake a connection. */
    init();run(1,x,1000000);sample.connected=(int32_t)0xa5a5a500u;
    assert(run(1,x,1050000).buttons==x);
    /* Ext uses its own original callback, with the same bounded 120-byte stride. */
    init();a.orig[GA_FN_READ]=0;batch_count=2;
    run(1,x,1000000);sample.buttons=x;sample.timestamp_us=1050000;
    b.guard=0x123456789abcdef0ULL;
    assert(ga_stub_read_ext(1,(uint8_t *)b.d,2,&a)==2);
    assert(!b.d[0].buttons && !b.d[1].buttons && b.guard==0x123456789abcdef0ULL);
    batch_count=64;memset(&b.d[1],0x5a,sizeof(b.d[1]));
    assert(ga_stub_read_ext(1,(uint8_t *)b.d,1,&a)==64);
    for (unsigned i=0;i<sizeof(b.d[1]);i++) assert(((uint8_t *)&b.d[1])[i]==0x5a);
    batch_count=-1;assert(ga_stub_read_ext(1,(uint8_t *)b.d,1,&a)==-1);
    init();a.lock=1;assert(run(1,x,1000000).buttons==x); /* contended read */
    assert(a.lock==1);
    init();a.mask=x|GA_BTN_CIRCLE;
    run(1,x,1000000);
    assert(run(1,x|GA_BTN_CIRCLE,1050000).buttons==GA_BTN_CIRCLE);
    assert(run(1,x|GA_BTN_CIRCLE,1100000).buttons==x); /* per-button phase */
    init();a.mask=0;
    run(1,x,1000000);assert(run(1,x,1050000).buttons==x); /* all off */
    init();a.mask=0xffff; /* invalid mask still cannot turbo navigation */
    run(1,x|GA_BTN_UP,1000000);
    assert(run(1,x|GA_BTN_UP,1050000).buttons==GA_BTN_UP);
    /* L1+R1+D-pad DOWN toggles once, even when shoulders are selected. */
    assert(TURBO_CHORD==(GA_BTN_L1|GA_BTN_R1|GA_BTN_DOWN));
    init();a.mask=TURBO_ALLOWED;
    run(1,x,1000000);assert(run(1,x,1050000).buttons==0);
    assert(run(1,x|TURBO_CHORD|sticks,1060000).buttons==(x|sticks));
    assert(a.pads[0].muted);
    assert(run(1,x|TURBO_CHORD,1060000).buttons==x);
    assert(run(1,x|TURBO_CHORD,1070000).buttons==x);
    assert(a.pads[0].muted); /* duplicate/held samples do not retoggle */
    assert(run(1,x,1080000).buttons==x);
    assert(run(1,x,1150000).buttons==x);
    run(2,x,1150000);assert(run(2,x,1200000).buttons==0);
    assert(run(1,x|TURBO_CHORD,1210000).buttons==x);
    assert(!a.pads[0].muted);
    assert(run(1,x,1250000).buttons==0);
    /* L3+R3 passes through at all durations, including beyond two seconds. */
    init();run(1,x|sticks,1000000);
    assert(run(1,x|sticks,1050000).buttons==sticks);
    run(1,x|sticks,2000000);run(1,x|sticks,3000000);
    assert(run(1,x|sticks,3050000).buttons==sticks);
    assert(!a.pads[0].muted);
    /* Each member and each incomplete combination remain ordinary inputs. */
    init();
    uint32_t parts[]={GA_BTN_DOWN,GA_BTN_L1,GA_BTN_R1,
        GA_BTN_L1|GA_BTN_R1,GA_BTN_L1|GA_BTN_DOWN,GA_BTN_R1|GA_BTN_DOWN,
        GA_BTN_L1|GA_BTN_R1|GA_BTN_TOUCH};
    for (unsigned i=0;i<sizeof(parts)/sizeof(parts[0]);i++) {
        assert(run(1,parts[i],1000000+i*10000).buttons==parts[i]);
        assert(!a.pads[0].muted);
    }
    init();a.enabled=0;
    assert(run(1,x|TURBO_CHORD,1000000).buttons==(x|TURBO_CHORD));
    assert(!a.pads[0].muted); /* web master switch wins */
    /* Both unchecked: combinations are ordinary input, even beyond 2s. */
    init();a.shortcuts=0;
    uint32_t both=TURBO_CHORD|sticks;
    assert(run(1,both,1000000).buttons==both);
    run(1,both,2000000);run(1,both,3000000);
    assert(run(1,both,3050000).buttons==both && !a.pads[0].muted);
    /* Stick-only: DOWN shortcut passes; stick hold toggles at exactly 2s. */
    init();a.shortcuts=TURBO_SHORTCUT_STICKS;
    assert(run(1,TURBO_CHORD,1000000).buttons==TURBO_CHORD);
    assert(!a.pads[0].muted);
    run(1,sticks,1100000);run(1,sticks,2100000);
    run(1,sticks,3099999);assert(!a.pads[0].muted);
    assert(run(1,sticks,3100000).buttons==sticks && a.pads[0].muted);
    run(1,sticks,3100000);run(1,sticks,4100000);run(1,sticks,5100000);
    assert(a.pads[0].muted); /* no repeat until release */
    run(1,GA_BTN_L3,5200000);run(1,sticks,5300000);
    run(1,sticks,6300000);run(1,sticks,7300000);
    assert(!a.pads[0].muted);
    /* Both checked: either shortcut works; a config change clears mute. */
    init();a.shortcuts=TURBO_SHORTCUT_ALL;
    run(1,TURBO_CHORD,1000000);assert(a.pads[0].muted);
    run(1,0,1100000);run(1,sticks,1200000);
    run(1,sticks,2200000);run(1,sticks,3200000);
    assert(!a.pads[0].muted);
    run(1,TURBO_CHORD,3300000);assert(a.pads[0].muted);
    a.shortcuts=0;
    assert(run(1,both,3400000).buttons==both && !a.pads[0].muted);
    /* A full second of 20 Hz contains 20 press/release cycles at 25ms
     * per half-cycle, while the service continues its 100ms heartbeats. */
    init();a.half_us=TURBO_HALF_US;
    unsigned presses=0,releases=0;int previous=0;
    for (unsigned i=0;i<2000;i++) {
        if (!(i%200)) a.age=0;
        uint32_t buttons=run(1,x|other,1000000u+(uint64_t)i*500u).buttons;
        assert(buttons&other);
        int current=!!(buttons&x);
        if (current && !previous) presses++;
        if (!current && previous) releases++;
        previous=current;
    }
    assert(presses==20 && releases==20);
    assert(run(1,other,2000000).buttons==other);
    puts("PASS: 20 Hz cycles, phase, repeat samples, release, pad isolation, L3/R3 and game chord passthrough, watchdog, triggers, disconnect, intercept, resume, errors, batch bounds, Ext");
}
