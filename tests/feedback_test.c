/* Real detached worker tests: completion must not need another pad read. */
#include "stub.h"
#include "pad.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static ga_args_t a;
static ga_pad_data_t input;
static pthread_mutex_t events_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {int handle;unsigned left,right;} events[128];
static unsigned event_count,mode_count,thread_count;
static int fail_create,fail_vibrate,real_time;
static unsigned completed;
typedef struct {void *(*entry)(void *);void *arg;} launch_t;
static void *launch(void *ptr) {
    launch_t c=*(launch_t *)ptr;free(ptr);
    void *result=c.entry(c.arg);
    __atomic_add_fetch(&completed,1u,__ATOMIC_RELEASE);return result;
}
static int32_t read_pad(int32_t h,uint8_t *out) {(void)h;memcpy(out,&input,sizeof(input));return 0;}
static int32_t vibrate(int32_t h,const uint8_t *motors) {
    pthread_mutex_lock(&events_lock);assert(event_count<128);
    events[event_count].handle=h;events[event_count].left=motors[0];events[event_count].right=motors[1];event_count++;
    pthread_mutex_unlock(&events_lock);return fail_vibrate?-1:0;
}
static int32_t set_mode(int32_t h,int32_t mode) {
    (void)h;(void)mode;pthread_mutex_lock(&events_lock);mode_count++;pthread_mutex_unlock(&events_lock);return 0;
}
static int32_t create(uint64_t *id,const void *attr,void *(*entry)(void *),void *arg,const char *name) {
    (void)attr;(void)name;thread_count++;
    if (fail_create) return -1;
    launch_t *context=malloc(sizeof(*context));assert(context);context->entry=entry;context->arg=arg;
    pthread_t thread;int rc=pthread_create(&thread,0,launch,context);if (rc) free(context);
    _Static_assert(sizeof(thread)<=sizeof(*id),"thread handle width");
    if (!rc) memcpy(id,&thread,sizeof(thread));return rc;
}
static int32_t detach(uint64_t id) {pthread_t thread;memcpy(&thread,&id,sizeof(thread));return pthread_detach(thread);}
static int32_t nap(uint32_t micros) {assert(micros==80000||micros==180000||micros==60000||micros==1000);return usleep(real_time?micros:1000);}
static void reset(void) {
    memset(&a,0,sizeof(a));memset(&input,0,sizeof(input));
    a.magic=GA_MAGIC;a.version=GA_LAYOUT_VERSION;a.enabled=1;a.mask=GA_BTN_CROSS;
    a.half_us=TURBO_HALF_US;a.shortcuts=TURBO_SHORTCUT_DOWN;
    a.orig[GA_FN_READ_STATE]=(uintptr_t)read_pad;
    a.orig[GA_FN_SET_VIBRATION]=(uintptr_t)vibrate;
    a.orig[GA_FN_SET_VIBRATION_MODE]=(uintptr_t)set_mode;
    a.feedback_create=(uintptr_t)create;a.feedback_detach=(uintptr_t)detach;
    a.feedback_sleep=(uintptr_t)nap;a.feedback_entry=(uintptr_t)ga_stub_feedback;
    input.connected=1;event_count=mode_count=thread_count=0;__atomic_store_n(&completed,0u,__ATOMIC_RELEASE);fail_create=fail_vibrate=real_time=0;
}
static void read_buttons(int handle,uint32_t buttons,uint64_t ts) {
    input.buttons=buttons;input.timestamp_us=ts;ga_pad_data_t out;
    assert(!ga_stub_read_state(handle,(uint8_t *)&out,&a));
}
static void finish(void) {
    for (unsigned i=0;i<1000;i++) {
        unsigned busy=0;
        for (unsigned j=0;j<TURBO_PADS;j++) busy|=__atomic_load_n(&a.pads[j].feedback_running,__ATOMIC_ACQUIRE);
        if (!busy && __atomic_load_n(&completed,__ATOMIC_ACQUIRE)==thread_count) return;
        usleep(1000);
    }
    assert(!"feedback worker failed to finish within one second");
}
int main(void) {
    reset();real_time=1;
    read_buttons(7,TURBO_CHORD,1000000); /* OFF: one pulse */
    read_buttons(7,TURBO_CHORD,1001000); /* held: no repeated event */
    finish(); /* no further game reads: the worker must still stop */
    assert(thread_count==1 && a.feedback_sent==1 && !a.feedback_failed);
    assert(!mode_count); /* unknown game mode is never overwritten */
    assert(event_count==2 && events[0].left && !events[1].left && events[0].handle==7 && events[1].handle==7);
    read_buttons(7,0,1100000);read_buttons(7,TURBO_CHORD,1200000); /* ON: two pulses */
    finish();assert(a.feedback_sent==2 && event_count==6);
    assert(events[2].left && !events[3].left && events[4].left && !events[5].left);
    reset();read_buttons(1,TURBO_CHORD,1000000);read_buttons(2,TURBO_CHORD,1000000);
    finish();assert(a.feedback_sent==2 && event_count==4); /* separate controllers */
    reset();fail_create=1;read_buttons(1,TURBO_CHORD,1000000);
    assert(a.pads[0].muted && !a.pads[0].feedback_running && a.feedback_failed==1);
    reset();a.feedback_create=0;read_buttons(1,TURBO_CHORD,1000000);
    assert(a.pads[0].muted && !thread_count && a.feedback_failed==1);
    reset();fail_vibrate=1;read_buttons(1,TURBO_CHORD,1000000);finish();
    assert(a.feedback_failed==2 && event_count==2 && !events[1].left);
    reset();uint8_t game[2]={21,34};
    assert(!ga_stub_set_vibration_mode(1,1,&a));assert(!ga_stub_set_vibration(1,game,&a));
    event_count=mode_count=0;read_buttons(1,TURBO_CHORD,1000000);finish();
    assert(event_count==3 && events[2].left==21 && events[2].right==34 && mode_count==2);
    reset(); /* rapid toggles queue on the same worker; latest state wins */
    read_buttons(1,TURBO_CHORD,1000000);read_buttons(1,0,1001000);read_buttons(1,TURBO_CHORD,1002000);
    finish();assert(!a.pads[0].muted && !a.pads[0].feedback_running && a.feedback_sent>=1);
    assert(event_count>=4 && events[event_count-4].left && !events[event_count-3].left &&
           events[event_count-2].left && !events[event_count-1].left);
    reset();a.enabled=0;read_buttons(1,TURBO_CHORD,1000000);assert(!thread_count && !event_count);
    puts("PASS: OFF one pulse, ON two pulses, held chord, independent pads, stop without reads, API failures, game output restoration, rapid toggles");
}
