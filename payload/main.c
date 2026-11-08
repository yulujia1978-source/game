/* SPDX-License-Identifier: GPL-3.0-or-later
 * PS5 Turbo: experimental PPSA process input transform.
 */
#include "hook.h"
#include "inject.h"
#include "kstuff.h"
#include "log.h"
#include "proc.h"
#include "stub.h"
#include "settings.h"
#include "web.h"
#include "version.h"
#include "target.h"
#include "upgrade.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#define DIR TURBO_DIR
static volatile sig_atomic_t quit;
static void stop(int sig) { quit=sig;ga_hook_cancel=1; }
/* Separate from log.txt: a second launch must explain its failure without
 * truncating the log of the instance that still holds the lock. */
static void startup_status(const char *message) {
    FILE *f=fopen(DIR "/startup.txt","w");
    if (!f) return;
    fprintf(f,"PS5 Turbo %s pid %d: %s\n",TURBO_VERSION,(int)getpid(),message);
    fclose(f);
}

static int attach(pid_t pid,ga_scan_cursor_t *cursor,ga_inject_t *inj) {
    ga_target_t t;ga_slot_t slots[GA_MAX_SLOTS];ga_orphan_t orphan;
    if (ga_hook_inspect(pid,&t)) return 1;
    memset(&orphan,0,sizeof(orphan));
    int n=ga_hook_find_slots(&t,slots,GA_MAX_SLOTS,&orphan);
    int reads=0;
    for (int i=0;i<n;i++) if (slots[i].kind>=GA_FN_READ_STATE && slots[i].kind<=GA_FN_READ_EXT) reads++;
    if ((n<=0 || !reads) && !orphan.args)
        n=ga_hook_scan_slots(&t,slots,GA_MAX_SLOTS,&orphan,cursor);
    if (quit) return -1;
    if (orphan.args) {
        ga_log("previous Turbo hooks remain; restart this game before reloading");
        return -1;
    }
    if (n<=0) return 1;
    return ga_inject_install(&t,slots,(unsigned)n,inj);
}
int main(void) {
    mkdir(DIR,0777);
    startup_status("starting");
    signal(SIGPIPE,SIG_IGN);
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
    /* Check dependencies before asking a working older instance to stop. */
    if (ga_proc_self_check()) {
        startup_status("FAILED: process access unavailable; reload jailbreak environment");return 1;
    }
    if (!ga_kstuff_available()) {
        startup_status("FAILED: kstuff bridge unavailable; load compatible kstuff before starting Turbo");return 1;
    }
    turbo_instance_t instance;
    if (turbo_instance_acquire(&instance,DIR,TURBO_VERSION,15000,ga_proc_birth)) {
        ga_log("startup: %s",instance.error);startup_status(instance.error);return 1;
    }
    ga_log_open();
    ga_log("PS5 Turbo " TURBO_VERSION " experimental update; v0.1 tested by user on 13.60");
    if (instance.replaced) ga_log("upgrade: previous Turbo stopped and service lock transferred");
    /* An explicit new launch wins over a marker left by a stopped instance.
     * During normal running, STOP still requests an immediate clean exit. */
    if (!access(DIR "/STOP",F_OK)) {
        if (unlink(DIR "/STOP")) {
            ga_log("startup: cannot remove leftover STOP: %s",strerror(errno));
            startup_status("FAILED: cannot remove leftover STOP marker");
            ga_log_close();turbo_instance_close(&instance);return 1;
        }
        ga_log("startup: leftover STOP removed; continuing explicit launch");
    }
    ga_args_t cfg;memset(&cfg,0,sizeof(cfg));turbo_defaults(&cfg);
    turbo_web_t web;
    int portal=turbo_web_open(&web);
    if (portal<0) {
        char report[240];snprintf(report,sizeof(report),"WEB FAILED: %s: errno %d (%s), port %d",web.error_stage,web.error,strerror(web.error),TURBO_PORT);
        ga_log("%s",report);startup_status(report);
        ga_log_close();turbo_instance_close(&instance);return 1;
    }
    /* Every successfully started service is a new settings session. Replace
     * the old file too, so periodic reload cannot restore the last session. */
    if (turbo_save(TURBO_SETTINGS,cfg.enabled,cfg.mask,cfg.half_us,cfg.shortcuts)) {
        startup_status("FAILED: cannot reset settings file");
        ga_log("cannot reset settings; exiting");close(portal);ga_log_close();turbo_instance_close(&instance);return 1;
    }
    ga_log("settings reset for new session: all buttons OFF, 20 Hz, D-pad shortcut only");
    ga_log("settings page: http://<PS5-IP>:%d",TURBO_PORT);
    startup_status("RUNNING: settings web server listening on port 6081");
    turbo_instance_ready(&instance);
    int removed=turbo_cleanup_versions("/data/pldmgr/payloads/local/ps5-turbo",TURBO_VERSION);
    if (removed) ga_log("upgrade: removed %d verified older Turbo ELF file(s) from manager slot",removed);
    ga_inject_t inj;memset(&inj,0,sizeof(inj));
    ga_scan_cursor_t cursor;memset(&cursor,0,sizeof(cursor));
    turbo_targets_t extra={0};
    ga_proc_t processes[GA_PROC_MAX];
    pid_t game=-1,waiting_for_exit=-1,unrecognized=-1;char title[GA_TITLE_ID_MAX]="";
    unsigned tick=0,attempts=0;int attached=0,failed=0;
    ga_log("waiting for one PS5 / PS4 game or supported emulator process");
    while (!quit) {
        if (!access(DIR "/STOP",F_OK)) {unlink(DIR "/STOP");break;}
        if (!(tick%10u)) {
            int ok=turbo_load(&cfg,TURBO_SETTINGS);
            if (ok) ga_log("settings missing/invalid: turbo disabled");
            turbo_targets_load(&extra,DIR "/targets.txt");
        }
        if (portal>=0) turbo_web_poll(&web,&cfg,attached,title);
        /* A dying process can still be listed after its mappings disappear.
         * Keep HTTP/settings alive, but never install over uncertain hooks. */
        if (waiting_for_exit>0) {
            if (ga_proc_alive(waiting_for_exit)) goto next_tick;
            ga_inject_forget(waiting_for_exit);
            memset(&inj,0,sizeof(inj));memset(&cursor,0,sizeof(cursor));
            game=-1;waiting_for_exit=-1;failed=0;attempts=0;title[0]=0;web.process[0]=0;
            ga_log("previous game exited; resuming game detection");
        }
        unsigned n=ga_proc_list(processes,GA_PROC_MAX);
        ga_proc_t candidate;
        unsigned matches=turbo_target_select(processes,n,getpid(),&extra,&candidate);
        if (!matches && n<GA_PROC_MAX) {
            for (unsigned i=0;i<n;i++) {
                ga_proc_t *p=&processes[i];
                if (p->pid==unrecognized || strcmp(p->name,"eboot.bin") ||
                    !p->title_id[0] || !strncmp(p->title_id,"NPX",3)) continue;
                ga_log("unrecognized app %s pid %d; for a homebrew emulator add its title ID to targets.txt",p->title_id,(int)p->pid);
                unrecognized=p->pid;break;
            }
        }
        pid_t selected=matches==1?candidate.pid:-1;
        const char *selected_title=matches==1?candidate.title_id:"";
        if (selected!=game) {
            if (game>0) {
                if (ga_proc_alive(game)) {
                    if (attached && ga_inject_remove(&inj)) {
                        ga_log("restore incomplete; web stays online, waiting for pid %d to exit; close that game",(int)game);
                        waiting_for_exit=game;attached=0;
                        goto next_tick;
                    }
                } else ga_inject_forget(game);
            }
            memset(&inj,0,sizeof(inj));memset(&cursor,0,sizeof(cursor));
            attached=0;failed=0;attempts=0;game=selected;
            snprintf(title,sizeof(title),"%s",selected_title);
            snprintf(web.process,sizeof(web.process),"%s",game>0?candidate.name:"");
            if (game>0) ga_log("%s %s (%s) pid %d detected",
                turbo_target_label(turbo_target_kind(&candidate,&extra)),
                title,candidate.name,(int)game);
        }
        /* Limit retries; a title without a supported input path stays plain. */
        if (game>0 && !attached && !failed && !(tick%10u)) {
            int r=attach(game,&cursor,&inj);
            if (!r) {attached=1;ga_log("attached to %s; web-selected shortcuts toggle turbo",title);}
            else if (r<0 || ++attempts>=60) {
                failed=1;ga_log("attach failed/timed out; close game and inspect log");
            }
        }
        if (attached && ga_inject_publish(&inj,&cfg)) {
            ga_log("heartbeat failed; web stays online, waiting for pid %d to exit",(int)game);
            if (ga_proc_alive(game) && ga_inject_remove(&inj))
                ga_log("restore incomplete; close the previous game");
            waiting_for_exit=game;attached=0;
            goto next_tick;
        }
        if (attached && !(tick%50u)) {
            ga_args_t live;
            if (!ga_inject_read(&inj,&live))
                ga_log("%s reads=%llu changed=%llu feedback=%llu errors=%llu",title,
                    (unsigned long long)live.calls,(unsigned long long)live.applied,
                    (unsigned long long)live.feedback_sent,(unsigned long long)live.feedback_failed);
        }
next_tick:
        tick++;usleep(100000);
    }
    if (inj.installed && ga_proc_alive(inj.pid)) {
        if (ga_inject_remove(&inj)) ga_log("restore incomplete; close the game");
    }
    if (portal>=0) close(portal);
    startup_status("STOPPED: service stopped; start this ELF to bring the webpage online");
    ga_log("service stopped");ga_log_close();turbo_instance_close(&instance);return 0;
}
