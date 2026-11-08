/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "upgrade.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
typedef struct {unsigned n[3];} version_t;
static int parse_version(const char *s,version_t *v) {
    for (unsigned i=0;i<3;i++) {
        unsigned n=0,digits=0;
        while (*s>='0' && *s<='9') {
            if (++digits>5) return -1;
            n=n*10u+(unsigned)(*s++-'0');
        }
        if (!digits) return -1;
        v->n[i]=n;
        if (i<2 && *s++!='.') return -1;
    }
    return *s?-1:0;
}
static int compare(version_t a,version_t b) {
    for (unsigned i=0;i<3;i++) if (a.n[i]!=b.n[i]) return a.n[i]>b.n[i]?1:-1;
    return 0;
}
static int path_join(char *out,size_t n,const char *dir,const char *leaf) {
    int len=snprintf(out,n,"%s/%s",dir,leaf);return len<0||(size_t)len>=n?-1:0;
}
static unsigned long long monotonic_ms(void) {
    struct timespec t;if (clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (unsigned long long)t.tv_sec*1000u+(unsigned long long)t.tv_nsec/1000000u;
}
static int instance_error(turbo_instance_t *s,const char *message) {
    snprintf(s->error,sizeof(s->error),"%s",message);turbo_instance_close(s);return -1;
}
void turbo_instance_close(turbo_instance_t *s) {
    if (s->service_fd>=0) {
        if (s->service_owned) ftruncate(s->service_fd,0);
        close(s->service_fd);s->service_fd=-1;s->service_owned=0;
    }
    if (s->upgrade_fd>=0) {close(s->upgrade_fd);s->upgrade_fd=-1;}
}
void turbo_instance_ready(turbo_instance_t *s) {
    if (s->upgrade_fd>=0) {close(s->upgrade_fd);s->upgrade_fd=-1;}
}
int turbo_instance_acquire(turbo_instance_t *s,const char *dir,const char *version,unsigned timeout_ms,uint64_t (*birth)(pid_t)) {
    memset(s,0,sizeof(*s));s->service_fd=s->upgrade_fd=-1;
    version_t requested;if (parse_version(version,&requested)) return instance_error(s,"FAILED: invalid release version");
    uint64_t own_birth=birth?birth(getpid()):0;
    char path[768],stop_path[768];
    if (path_join(path,sizeof(path),dir,"upgrade.lock") || path_join(stop_path,sizeof(stop_path),dir,"STOP"))
        return instance_error(s,"FAILED: upgrade directory path too long");
    s->upgrade_fd=open(path,O_CREAT|O_RDWR|O_NOFOLLOW,0600);
    if (s->upgrade_fd<0) return instance_error(s,"FAILED: cannot open upgrade lock");
    if (flock(s->upgrade_fd,LOCK_EX|LOCK_NB)) return instance_error(s,"BLOCKED: another upgrade is in progress; no stop request sent");
    path_join(path,sizeof(path),dir,"service.lock");
    s->service_fd=open(path,O_CREAT|O_RDWR|O_NOFOLLOW,0600);
    if (s->service_fd<0) return instance_error(s,"FAILED: cannot open service lock");
    if (flock(s->service_fd,LOCK_EX|LOCK_NB)) {
        if (errno!=EWOULDBLOCK && errno!=EAGAIN) return instance_error(s,"FAILED: service lock operation failed");
        char record[160]={0},active[32];int pid=0;unsigned long long recorded_birth=0;
        ssize_t n=pread(s->service_fd,record,sizeof(record)-1,0);
        if (n<0) return instance_error(s,"FAILED: cannot verify existing service record; no stop request sent");
        if (n>0) {
            version_t current;
            if (sscanf(record,"PS5Turbo version=%31s pid=%d birth=%llu",active,&pid,&recorded_birth)!=3 || parse_version(active,&current) || pid<=0)
                return instance_error(s,"FAILED: unrecognized existing service record; no stop request sent");
            uint64_t actual_birth=birth?birth((pid_t)pid):0;
            /* Birth information is optional on firmware that omits it. In
             * that case use the cooperative lock/STOP path, never a PID. */
            if (recorded_birth && actual_birth==recorded_birth && compare(current,requested)>=0)
                return instance_error(s,"BLOCKED: same or newer Turbo version already running; no stop request sent");
        }
        int marker=open(stop_path,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);
        int owned=marker>=0;
        if (!owned && errno!=EEXIST) return instance_error(s,"FAILED: cannot request previous Turbo shutdown");
        struct stat ours={0};if (owned) fstat(marker,&ours);
        unsigned long long start=monotonic_ms();int acquired=0;
        unsigned elapsed=0;
        for (;;) {
            if (!flock(s->service_fd,LOCK_EX|LOCK_NB)) {acquired=1;break;}
            if (errno!=EWOULDBLOCK && errno!=EAGAIN) break;
            unsigned long long now=monotonic_ms();
            if (elapsed>=timeout_ms || (start && now>=start && now-start>=timeout_ms)) break;
            usleep(50000);elapsed+=50;
        }
        /* Remove our pending marker after a timeout, but never a replacement
         * STOP independently created by the user or another process. */
        if (owned) {
            struct stat live;
            if (!lstat(stop_path,&live) && live.st_dev==ours.st_dev && live.st_ino==ours.st_ino) unlink(stop_path);
            close(marker);
        }
        if (!acquired) return instance_error(s,"FAILED: previous Turbo did not stop within timeout; new instance not started and old files retained");
        s->replaced=1;
    }
    s->service_owned=1;
    char record[160];int n=snprintf(record,sizeof(record),"PS5Turbo version=%s pid=%d birth=%llu\n",version,(int)getpid(),(unsigned long long)own_birth);
    if (ftruncate(s->service_fd,0) || lseek(s->service_fd,0,SEEK_SET)<0 ||
        write(s->service_fd,record,(size_t)n)!=n || fsync(s->service_fd))
        return instance_error(s,"FAILED: cannot record running version");
    return 0;
}
/* Read regular files only, with strict bounds and no symlink following. */
static unsigned char *read_regular(const char *path,size_t limit,size_t *size,struct stat *identity) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW);if (fd<0) return 0;
    struct stat st;
    if (fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(unsigned long long)st.st_size>limit) {close(fd);return 0;}
    unsigned char *b=malloc((size_t)st.st_size+1);if (!b) {close(fd);return 0;}
    size_t done=0;while (done<(size_t)st.st_size) {
        ssize_t n=read(fd,b+done,(size_t)st.st_size-done);
        if (n<=0) {free(b);close(fd);return 0;}done+=(size_t)n;
    }
    close(fd);b[done]=0;*size=done;if (identity) *identity=st;return b;
}
static int elf_version(const char *path,version_t *version,struct stat *identity) {
    size_t n=0;unsigned char *b=read_regular(path,2u*1024u*1024u,&n,identity);if (!b) return -1;
    int found=0;
    if (n>=64 && !memcmp(b,"\177ELF\2\1",6) && b[18]==62 && b[19]==0) {
        const char *markers[]={"PS5 TURBO · ","PS5 Turbo "};
        for (unsigned k=0;k<2;k++) {
            size_t len=strlen(markers[k]);
            for (size_t i=0;i+len<n;i++) if (!memcmp(b+i,markers[k],len)) {
                char text[32];size_t j=0;
                while (i+len+j<n && j<sizeof(text)-1 &&
                    ((b[i+len+j]>='0' && b[i+len+j]<='9')||b[i+len+j]=='.')) {text[j]=(char)b[i+len+j];j++;}
                text[j]=0;version_t v;
                if (!parse_version(text,&v)) {
                    if (found && compare(*version,v)) {free(b);return -1;}
                    *version=v;found=1;
                }
            }
        }
    }
    free(b);return found?0:-1;
}
static int json_string_is(const char *json,const char *key,const char *value) {
    char quoted[80];snprintf(quoted,sizeof(quoted),"\"%s\"",key);
    const char *p=strstr(json,quoted);if (!p) return 0;
    p+=strlen(quoted);while (isspace((unsigned char)*p)) p++;
    if (*p++!=':') return 0;while (isspace((unsigned char)*p)) p++;
    size_t len=strlen(value);return *p=='"' && !strncmp(p+1,value,len) && p[1+len]=='"';
}
int turbo_cleanup_versions(const char *slot,const char *release_version) {
    version_t current;if (parse_version(release_version,&current)) return 0;
    struct stat dir;if (lstat(slot,&dir)||!S_ISDIR(dir.st_mode)) return 0;
    char filename[96],path[768];snprintf(filename,sizeof(filename),"ps5-turbo-v%s.elf",release_version);
    if (path_join(path,sizeof(path),slot,"meta.json")) return 0;
    size_t n;unsigned char *meta=read_regular(path,16384,&n,0);if (!meta) return 0;
    int committed=json_string_is((const char *)meta,"filename",filename) && json_string_is((const char *)meta,"kind","local");
    free(meta);if (!committed || path_join(path,sizeof(path),slot,filename)) return 0;
    version_t installed;
    if (elf_version(path,&installed,0)||compare(installed,current)) return 0;
    DIR *d=opendir(slot);if (!d) return 0;
    struct dirent *entry;int removed=0;
    while ((entry=readdir(d))) {
        const char *name=entry->d_name;
        if (!strcmp(name,filename)) continue;
        int canonical=!strcmp(name,"ps5-turbo.elf");
        version_t named={0};
        if (!canonical) {
            if (strncmp(name,"ps5-turbo-",10)) continue;
            const char *start=name+10;if (*start=='v') start++;
            size_t len=strlen(start);
            if (len<=4 || strcmp(start+len-4,".elf") || len-4>=32) continue;
            char text[32];memcpy(text,start,len-4);text[len-4]=0;
            if (parse_version(text,&named)||compare(named,current)>=0) continue;
        }
        if (path_join(path,sizeof(path),slot,name)) continue;
        version_t embedded;struct stat before,after;
        if (elf_version(path,&embedded,&before)||compare(embedded,current)>=0 ||
            (!canonical && compare(embedded,named))) continue;
        if (!lstat(path,&after) && S_ISREG(after.st_mode) && after.st_dev==before.st_dev && after.st_ino==before.st_ino && !unlink(path)) removed++;
    }
    closedir(d);return removed;
}
