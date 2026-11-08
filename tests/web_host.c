/* SPDX-License-Identifier: GPL-3.0-or-later — host-only HTTP integration fixture */
#include "web.h"
#include "settings.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static volatile sig_atomic_t done;
static void stop(int n) {(void)n;done=1;}
int main(void) {
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
    mkdir(TURBO_DIR,0700);
    if (access(TURBO_SETTINGS,F_OK))
        assert(!turbo_save(TURBO_SETTINGS,1,0,25000,TURBO_SHORTCUT_DOWN));
    ga_args_t cfg={0};assert(!turbo_load(&cfg,TURBO_SETTINGS));
    turbo_web_t w;assert(turbo_web_open(&w)>=0);
    struct sockaddr_in a;socklen_t n=sizeof(a);assert(!getsockname(w.fd,(struct sockaddr *)&a,&n));
    printf("%d\n",ntohs(a.sin_port));fflush(stdout);
    while (!done) {turbo_web_poll(&w,&cfg,1,"PPSA12345");usleep(10000);}
    close(w.fd);return 0;
}
