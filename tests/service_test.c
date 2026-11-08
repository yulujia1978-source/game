/* Run the actual service loop against deterministic game-exit races. */
#define main turbo_service_main
#define usleep test_usleep
#include "../payload/main.c"
#undef main
#undef usleep
#include <assert.h>

volatile int ga_hook_cancel;
static unsigned step,scenario,polls,installs,removes,forgets,publishes;
static int online;
int test_usleep(useconds_t usec) {
    (void)usec;
    if (++step==(scenario==3?3u:13u)) raise(SIGTERM);
    assert(step<20);return 0;
}
void ga_log_open(void) {}
void ga_log_close(void) {}
void ga_log(const char *fmt,...) { (void)fmt; }
int ga_proc_self_check(void) { return scenario==7?-1:0; }
int ga_kstuff_available(void) { return scenario==6?0:1; }
unsigned ga_proc_list(ga_proc_t *out,unsigned max) {
    assert(max>0);memset(out,0,sizeof(*out));
    out->pid=step==0 || (scenario==1 && step<5)?101:202;
    snprintf(out->name,sizeof(out->name),"%s",scenario==4?"retroarch.elf":"eboot.bin");
    if (scenario!=4) snprintf(out->title_id,sizeof(out->title_id),"%s%05d",out->pid==101?"PPSA":"CUSA",out->pid);
    return 1;
}
int ga_proc_alive(pid_t pid) { return pid==202 || step<5; }
uint64_t ga_proc_birth(pid_t pid) { (void)pid;return 42; }
void ga_inject_forget(pid_t pid) { assert(pid==101);forgets++; }
int ga_hook_inspect(pid_t pid,ga_target_t *t) {
    memset(t,0,sizeof(*t));t->pid=pid;return 0;
}
int ga_hook_find_slots(const ga_target_t *t,ga_slot_t *s,unsigned max,ga_orphan_t *o) {
    (void)t;(void)max;(void)o;memset(s,0,sizeof(*s));return 1;
}
int ga_hook_scan_slots(const ga_target_t *t,ga_slot_t *s,unsigned max,ga_orphan_t *o,ga_scan_cursor_t *c) {
    (void)t;(void)s;(void)max;(void)o;(void)c;assert(0);return -1;
}
int ga_inject_install(const ga_target_t *t,const ga_slot_t *s,unsigned n,ga_inject_t *in) {
    (void)s;assert(n==1);
    if (t->pid==202 && scenario!=2) assert(step>=5);
    installs++;in->pid=t->pid;in->installed=1;return 0;
}
int ga_inject_remove(ga_inject_t *in) {
    removes++;
    if (in->pid==101 && scenario!=2) return -1;
    in->installed=0;return 0;
}
int ga_inject_publish(const ga_inject_t *in,const ga_args_t *cfg) {
    assert(in->installed);assert(cfg->mask==0x800);
    publishes++;return scenario==1 && in->pid==101 && step==1?-1:0;
}
int ga_inject_read(const ga_inject_t *in,ga_args_t *a) {
    (void)in;memset(a,0,sizeof(*a));return 0;
}
int turbo_web_open(turbo_web_t *w) {
    if (scenario==10) {memset(w,0,sizeof(*w));w->fd=-1;w->error=EADDRINUSE;w->error_stage="bind port";return -1;}
    w->fd=dup(STDOUT_FILENO);assert(w->fd>=0);online=1;return w->fd;
}
void turbo_web_poll(turbo_web_t *w,ga_args_t *cfg,int attached,const char *title) {
    (void)w;(void)title;assert(online);polls++;
    if (polls==1) {
        assert(cfg->enabled==1 && !cfg->mask && cfg->half_us==25000);
        assert(cfg->shortcuts==TURBO_SHORTCUT_DOWN);
        ga_args_t saved={0};assert(!turbo_load(&saved,TURBO_SETTINGS));
        assert(saved.enabled==1 && !saved.mask && saved.half_us==25000);
        assert(saved.shortcuts==TURBO_SHORTCUT_DOWN);
    }
    if (scenario!=2 && step>=2 && step<5) assert(!attached);
    /* Saving and loading settings must remain possible while waiting. */
    assert(!turbo_save(TURBO_SETTINGS,1,0x800,25000,TURBO_SHORTCUT_DOWN));
    assert(!turbo_load(cfg,TURBO_SETTINGS));assert(cfg->mask==0x800);
}
int main(int argc,char **argv) {
    assert(argc==2);scenario=(unsigned)atoi(argv[1]);assert(scenario<=10);
    mkdir(DIR,0700);
    assert(!turbo_save(TURBO_SETTINGS,0,TURBO_ALLOWED,25000,TURBO_SHORTCUT_ALL));
    if (scenario==9) {
        FILE *f=fopen(TURBO_SETTINGS,"w");assert(f);fputs("broken old settings\n",f);fclose(f);
    }
    unlink(DIR "/STOP");
    if (scenario==5) {
        mkdir(DIR,0700);FILE *f=fopen(DIR "/STOP","w");assert(f);fclose(f);
    }
    int held_lock=-1;
    if (scenario==8) {
        mkdir(DIR,0700);held_lock=open(DIR "/service.lock",O_CREAT|O_RDWR,0600);
        assert(held_lock>=0 && !flock(held_lock,LOCK_EX|LOCK_NB));
        char owner[128];int n=snprintf(owner,sizeof(owner),"PS5Turbo version=%s pid=1 birth=42\n",TURBO_VERSION);
        assert(write(held_lock,owner,(size_t)n)==n);
    }
    int rc=turbo_service_main();
    if ((scenario>=6 && scenario<=8) || scenario==10) {
        assert(rc==1 && !polls && !installs);
        FILE *f=fopen(DIR "/startup.txt","r");assert(f);
        char report[512];assert(fgets(report,sizeof(report),f));fclose(f);
        const char *reason=scenario==6?"kstuff bridge unavailable":scenario==7?"process access unavailable":scenario==10?"WEB FAILED":"already running";
        assert(strstr(report,reason));
        ga_args_t saved={0};assert(!turbo_load(&saved,TURBO_SETTINGS));
        assert(saved.mask==TURBO_ALLOWED && saved.half_us==25000);
        if (held_lock>=0) close(held_lock);
        unlink(TURBO_SETTINGS);unlink(DIR "/service.lock");unlink(DIR "/upgrade.lock");unlink(DIR "/startup.txt");rmdir(DIR);
        printf("PASS: startup failure %u explains %s\n",scenario,reason);return 0;
    }
    assert(!rc);
    assert(polls==(scenario==3?3u:13u));
    assert(installs==(scenario==3?1u:2u));
    assert(publishes>0);assert(removes>0);
    if (scenario!=2 && scenario!=3) assert(forgets==1);
    assert(access(DIR "/STOP",F_OK));
    unlink(TURBO_SETTINGS);unlink(DIR "/service.lock");unlink(DIR "/upgrade.lock");unlink(DIR "/startup.txt");rmdir(DIR);
    printf("PASS: service game-switch scenario %u, settings stayed available\n",scenario);
}
