/* Real process/lock tests for cooperative replacement and bounded cleanup. */
#include "upgrade.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static uint64_t birth(pid_t pid) {return kill(pid,0)?0:42;}
static uint64_t unknown_birth(pid_t pid) {(void)pid;return 0;}
static void path(char *out,const char *dir,const char *leaf) {assert(snprintf(out,768,"%s/%s",dir,leaf)<768);}
static void text_file(const char *dir,const char *leaf,const char *text) {
    char p[768];path(p,dir,leaf);FILE *f=fopen(p,"w");assert(f);fputs(text,f);assert(!fclose(f));
}
static int exists(const char *dir,const char *leaf) {char p[768];path(p,dir,leaf);return !access(p,F_OK);}
static void remove_file(const char *dir,const char *leaf) {char p[768];path(p,dir,leaf);unlink(p);}
static void clean(const char *dir) {remove_file(dir,"STOP");remove_file(dir,"service.lock");remove_file(dir,"upgrade.lock");assert(!rmdir(dir));}
typedef struct {pid_t pid;int control;} old_t;
static old_t old_service(const char *dir,const char *version,int ignore_stop) {
    int ready[2],control[2];assert(!pipe(ready)&&!pipe(control));
    pid_t child=fork();assert(child>=0);
    if (!child) {
        close(ready[0]);close(control[1]);
        char p[768];path(p,dir,"service.lock");int fd=open(p,O_CREAT|O_RDWR,0600);
        if (fd<0 || flock(fd,LOCK_EX|LOCK_NB)) _exit(2);
        if (version) {char record[160];int n=snprintf(record,sizeof(record),"PS5Turbo version=%s pid=%d birth=42\n",version,(int)getpid());if (write(fd,record,(size_t)n)!=n) _exit(3);}
        if (write(ready[1],"r",1)!=1) _exit(4);close(ready[1]);
        fcntl(control[0],F_SETFL,O_NONBLOCK);path(p,dir,"STOP");
        for (unsigned i=0;i<2000;i++) {
            char c;if (read(control[0],&c,1)==1) break;
            if (!ignore_stop && !access(p,F_OK)) {unlink(p);break;}
            usleep(5000);
        }
        close(control[0]);close(fd);_exit(0);
    }
    close(ready[1]);close(control[0]);char c;assert(read(ready[0],&c,1)==1);close(ready[0]);
    old_t result={child,control[1]};return result;
}
static void stop_old(old_t old,int already_stopped) {
    if (!already_stopped) assert(write(old.control,"q",1)==1);
    close(old.control);int status;assert(waitpid(old.pid,&status,0)==old.pid && WIFEXITED(status)&&!WEXITSTATUS(status));
}
static void takeover_cases(void) {
    char a[]="/tmp/turbo-upgrade-a-XXXXXX";assert(mkdtemp(a));
    turbo_instance_t active,duplicate;
    assert(!turbo_instance_acquire(&active,a,"0.4.1",1000,birth) && !active.replaced);
    turbo_instance_ready(&active);
    assert(turbo_instance_acquire(&duplicate,a,"0.4.1",100,birth) && strstr(duplicate.error,"already running"));
    assert(turbo_instance_acquire(&duplicate,a,"0.4.0",100,birth) && !exists(a,"STOP"));
    turbo_instance_close(&active);clean(a);
    for (unsigned i=0;i<2;i++) {
        char b[]="/tmp/turbo-upgrade-b-XXXXXX";assert(mkdtemp(b));
        old_t old=old_service(b,i?"0.4.0":0,0);
        assert(!turbo_instance_acquire(&active,b,"0.4.1",1500,birth) && active.replaced);
        assert(!exists(b,"STOP"));
        /* While the winner is starting, another launcher sends no STOP. */
        assert(turbo_instance_acquire(&duplicate,b,"0.4.2",100,birth) && strstr(duplicate.error,"upgrade is in progress"));
        assert(!exists(b,"STOP"));
        turbo_instance_ready(&active);turbo_instance_close(&active);stop_old(old,1);clean(b);
    }
    char c[]="/tmp/turbo-upgrade-c-XXXXXX";assert(mkdtemp(c));
    old_t newer=old_service(c,"0.5.0",1);
    assert(turbo_instance_acquire(&active,c,"0.4.1",100,birth) && !exists(c,"STOP"));
    stop_old(newer,0);clean(c);
    char stale[]="/tmp/turbo-upgrade-stale-XXXXXX";assert(mkdtemp(stale));
    old_t legacy=old_service(stale,0,0);
    char stale_record[160];snprintf(stale_record,sizeof(stale_record),"PS5Turbo version=9.0.0 pid=%d birth=41\n",(int)legacy.pid);
    text_file(stale,"service.lock",stale_record); /* PID reused after a reboot */
    assert(!turbo_instance_acquire(&active,stale,"0.4.1",1500,birth) && active.replaced);
    turbo_instance_close(&active);stop_old(legacy,1);clean(stale);
    char unknown[]="/tmp/turbo-upgrade-unknown-XXXXXX";assert(mkdtemp(unknown));
    old_t unrecorded=old_service(unknown,0,0);
    assert(!turbo_instance_acquire(&active,unknown,"0.4.1",1500,unknown_birth) && active.replaced);
    turbo_instance_close(&active);stop_old(unrecorded,1);clean(unknown);
    for (unsigned keep_stop=0;keep_stop<2;keep_stop++) {
        char d[]="/tmp/turbo-upgrade-d-XXXXXX";assert(mkdtemp(d));
        old_t stuck=old_service(d,0,1);text_file(d,"settings.txt","keep this configuration");
        if (keep_stop) text_file(d,"STOP","user stop request");
        assert(turbo_instance_acquire(&active,d,"0.4.1",100,birth) && strstr(active.error,"timeout"));
        assert(exists(d,"settings.txt") && exists(d,"STOP")==!!keep_stop && !kill(stuck.pid,0));
        stop_old(stuck,0);remove_file(d,"settings.txt");clean(d);
    }
}
static void fake_elf(const char *dir,const char *name,const char *brand_version) {
    char p[768];path(p,dir,name);FILE *f=fopen(p,"wb");assert(f);
    unsigned char header[64]={0};memcpy(header,"\177ELF\2\1",6);header[18]=62;
    assert(fwrite(header,1,sizeof(header),f)==sizeof(header));
    fprintf(f,"PS5 TURBO · %s<",brand_version);assert(!fclose(f));
}
static void cleanup_cases(void) {
    char dir[]="/tmp/turbo-clean-XXXXXX";assert(mkdtemp(dir));
    fake_elf(dir,"ps5-turbo-v0.4.1.elf","0.4.1");
    fake_elf(dir,"ps5-turbo-v0.4.0.elf","0.4.0");
    fake_elf(dir,"ps5-turbo-0.3.1.elf","0.3.1");
    fake_elf(dir,"ps5-turbo.elf","0.3.1");
    fake_elf(dir,"ps5-turbo-v0.5.0.elf","0.5.0");
    fake_elf(dir,"other-v0.1.0.elf","0.1.0");
    text_file(dir,"ps5-turbo-v0.2.0.elf","not a Turbo ELF");
    fake_elf(dir,"ps5-turbo-v0.2.1.elf","0.5.0"); /* mismatched identity */
    text_file(dir,"meta.json","{\"kind\":\"local\",\"filename\":\"ps5-turbo-v0.4.0.elf\"}");
    assert(!turbo_cleanup_versions(dir,"0.4.1") && exists(dir,"ps5-turbo-v0.4.0.elf"));
    text_file(dir,"meta.json","{\"kind\":\"local\",\"filename\":\"ps5-turbo-v0.4.1.elf\"}");
    char link[768],target[768];path(link,dir,"ps5-turbo-v0.1.0.elf");path(target,dir,"other-v0.1.0.elf");assert(!symlink(target,link));
    assert(turbo_cleanup_versions(dir,"0.4.1")==3);
    assert(exists(dir,"ps5-turbo-v0.4.1.elf") && exists(dir,"ps5-turbo-v0.5.0.elf"));
    assert(exists(dir,"other-v0.1.0.elf") && exists(dir,"ps5-turbo-v0.2.0.elf") && exists(dir,"ps5-turbo-v0.2.1.elf"));
    assert(!access(link,F_OK)); /* symlink and its target retained */
    assert(!turbo_cleanup_versions(dir,"0.4.1"));
    const char *left[]={"ps5-turbo-v0.4.1.elf","ps5-turbo-v0.5.0.elf","other-v0.1.0.elf","ps5-turbo-v0.2.0.elf","ps5-turbo-v0.2.1.elf","ps5-turbo-v0.1.0.elf","meta.json"};
    for (unsigned i=0;i<sizeof(left)/sizeof(*left);i++) remove_file(dir,left[i]);
    assert(!rmdir(dir));
}
int main(void) {
    takeover_cases();cleanup_cases();
    puts("PASS: legacy/current takeover, duplicate and downgrade prevention, simultaneous launcher exclusion, timeout preserving old service, verified cleanup with metadata/identity/symlink guards");
}
