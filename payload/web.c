/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "web.h"
#include "settings.h"
#include "web_page.h"
#include "version.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
int turbo_web_form(const char *body,const char *token,unsigned *enabled,unsigned *mask,unsigned *half,unsigned *shortcuts) {
    if (strlen(body)>=256) return -1;
    char copy[256];snprintf(copy,sizeof(copy),"%s",body);
    unsigned seen=0;char *save=0;
    for (char *p=strtok_r(copy,"&",&save);p;p=strtok_r(0,"&",&save)) {
        char *eq=strchr(p,'=');if (!eq) return -1;*eq++=0;
        unsigned bit;
        if (!strcmp(p,"token")) {
            if (strcmp(eq,token) || (seen&8)) return -1;
            seen|=8;continue;
        }
        if (!strcmp(p,"enabled")) bit=1;
        else if (!strcmp(p,"mask")) bit=2;
        else if (!strcmp(p,"half_us")) bit=4;
        else if (!strcmp(p,"shortcuts")) bit=16;
        else return -1;
        if ((seen&bit) || !*eq || *eq=='-' || *eq=='+') return -1;
        char *end;unsigned long n=strtoul(eq,&end,10);
        if (*end || n>0xffffffffUL) return -1;
        if (bit==1) *enabled=(unsigned)n;
        else if (bit==2) *mask=(unsigned)n;
        else if (bit==4) *half=(unsigned)n;
        else *shortcuts=(unsigned)n;
        seen|=bit;
    }
    return seen==31 && turbo_valid(*enabled,*mask,*half,*shortcuts) ? 0 : -1;
}
static int web_open_failed(turbo_web_t *w,const char *stage,int error,int fd) {
    w->error_stage=stage;w->error=error;
    if (fd>=0) close(fd);
    return -1;
}
int turbo_web_open(turbo_web_t *w) {
    memset(w,0,sizeof(*w));w->fd=-1;
    unsigned char random[16];
    int rng=open("/dev/urandom",O_RDONLY);
    if (rng<0) return web_open_failed(w,"open random device",errno,-1);
    ssize_t got=read(rng,random,sizeof(random));int rng_error=errno;close(rng);
    if (got!=(ssize_t)sizeof(random)) return web_open_failed(w,"read random device",got<0?rng_error:EIO,-1);
    for (unsigned i=0;i<sizeof(random);i++) snprintf(w->token+i*2,3,"%02x",random[i]);
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if (fd<0) return web_open_failed(w,"create socket",errno,-1);
    int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_ANY);addr.sin_port=htons(TURBO_PORT);
    if (bind(fd,(struct sockaddr *)&addr,sizeof(addr))) return web_open_failed(w,"bind port",errno,fd);
    if (listen(fd,4)) return web_open_failed(w,"listen",errno,fd);
    if (fcntl(fd,F_SETFL,O_NONBLOCK)) return web_open_failed(w,"nonblocking mode",errno,fd);
    w->fd=fd;return fd;
}
static int send_all(int fd,const char *p,size_t n) {
    while (n) {
        ssize_t r=send(fd,p,n,0);if (r<=0) return -1;
        p+=r;n-=(size_t)r;
    }
    return 0;
}
static void reply(int fd,int code,const char *type,const char *body) {
    char h[512];size_t len=strlen(body);
    int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n\r\n",code,code==200?"OK":"Error",type,len);
    if (!send_all(fd,h,(size_t)n)) send_all(fd,body,len);
}
/* Bounded to 200ms total, so a stalled HTTP client cannot starve input
 * heartbeats. All socket operations after select use nonblocking I/O. */
void turbo_web_poll(turbo_web_t *w,ga_args_t *cfg,int attached,const char *title) {
    int fd=accept(w->fd,0,0);if (fd<0) return;
    if (fcntl(fd,F_SETFL,O_NONBLOCK)<0) {close(fd);return;}
#ifdef SO_NOSIGPIPE
    int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
#endif
    char request[4096];size_t used=0;int length=-1;size_t header=0;
    struct timeval deadline,now;gettimeofday(&deadline,0);deadline.tv_usec+=200000;
    if (deadline.tv_usec>=1000000) {deadline.tv_sec++;deadline.tv_usec-=1000000;}
    for (;;) {
        gettimeofday(&now,0);
        long remain=(deadline.tv_sec-now.tv_sec)*1000000L+deadline.tv_usec-now.tv_usec;
        if (remain<=0) goto bad;
        fd_set reads;FD_ZERO(&reads);FD_SET(fd,&reads);
        struct timeval wait={remain/1000000,remain%1000000};
        if (select(fd+1,&reads,0,0,&wait)<=0) goto bad;
        ssize_t n=recv(fd,request+used,sizeof(request)-used-1,0);
        if (n<=0) goto bad;
        if (memchr(request+used,0,(size_t)n)) goto bad;
        used+=(size_t)n;request[used]=0;
        if (!header) {
            char *end=strstr(request,"\r\n\r\n");
            if (end) {
                header=(size_t)(end-request)+4;
                char headers[4096];memcpy(headers,request,header);headers[header]=0;
                char *save=0;char *line=strtok_r(headers,"\r\n",&save);(void)line;
                while ((line=strtok_r(0,"\r\n",&save))) {
                    if (!strncasecmp(line,"Transfer-Encoding:",18)) goto bad;
                    if (!strncasecmp(line,"Content-Length:",15)) {
                        if (length!=-1) goto bad;
                        char *p=line+15;while (*p==' ') p++;
                        char *tail;long v=strtol(p,&tail,10);
                        if (tail==p || *tail || v<0 || v>255) goto bad;
                        length=(int)v;
                    }
                }
                if (length<0) length=0;
            }
        }
        if (header && used>=header+(unsigned)length) break;
        if (used==sizeof(request)-1) goto bad;
    }
    char method[12],path[80],version[16];
    if (sscanf(request,"%11s %79s %15s",method,path,version)!=3) goto bad;
    if (!strcmp(method,"GET") && !strcmp(path,"/")) reply(fd,200,"text/html; charset=utf-8",turbo_page);
    else if (!strcmp(method,"GET") && !strcmp(path,"/api/state")) {
        char safe[10]="",process[20]="",body[320];
        /* Only title-id characters go into JSON, even if process data is bad. */
        unsigned i;for (i=0;i<9 && title[i];i++) {
            char c=title[i];if (!((c>='A'&&c<='Z')||(c>='0'&&c<='9'))) break;
            safe[i]=c;
        }
        safe[i]=0;
        for (i=0;i<sizeof(process)-1 && w->process[i];i++) {
            char c=w->process[i];
            if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) break;
            process[i]=c;
        }
        process[i]=0;
        snprintf(body,sizeof(body),"{\"version\":\"" TURBO_VERSION "\",\"enabled\":%u,\"mask\":%u,\"half_us\":%u,\"shortcuts\":%u,\"attached\":%s,\"title\":\"%s\",\"process\":\"%s\",\"token\":\"%s\"}",cfg->enabled,cfg->mask,cfg->half_us,cfg->shortcuts,attached?"true":"false",safe,process,w->token);
        reply(fd,200,"application/json",body);
    } else if (!strcmp(method,"POST") && !strcmp(path,"/api/settings")) {
        if (used!=header+(unsigned)length) goto bad;
        unsigned enabled=0,mask=0,half=0,shortcuts=0;
        if (turbo_web_form(request+header,w->token,&enabled,&mask,&half,&shortcuts)) {
            reply(fd,400,"text/plain; charset=utf-8","设置无效，请刷新页面重试");
        } else if (turbo_save(TURBO_SETTINGS,enabled,mask,half,shortcuts)) {
            reply(fd,500,"text/plain; charset=utf-8","保存失败，请查看主机存储和日志");
        } else {
            cfg->enabled=enabled;cfg->mask=mask;cfg->half_us=half;cfg->shortcuts=shortcuts;cfg->settings_seq++;
            reply(fd,200,"application/json","{\"ok\":true}");
        }
    } else reply(fd,404,"text/plain","Not found");
    close(fd);return;
bad:
    reply(fd,400,"text/plain","Bad request or timeout");close(fd);
}
