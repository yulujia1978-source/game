/* Exercise error reporting with a real, deliberately occupied local port. */
static unsigned test_port;
#define TURBO_PORT test_port
#include "../payload/web.c"
#include <assert.h>
int main(void) {
    int blocker=socket(AF_INET,SOCK_STREAM,0);assert(blocker>=0);
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    assert(!bind(blocker,(struct sockaddr *)&addr,sizeof(addr)) && !listen(blocker,1));
    socklen_t len=sizeof(addr);assert(!getsockname(blocker,(struct sockaddr *)&addr,&len));
    test_port=ntohs(addr.sin_port);
    turbo_web_t w;
    assert(turbo_web_open(&w)<0 && w.fd==-1);
    assert(w.error==EADDRINUSE && !strcmp(w.error_stage,"bind port"));
    close(blocker);
    assert(turbo_web_open(&w)>=0 && !w.error);close(w.fd);
    puts("PASS: occupied web port reports bind stage and errno; startup works after port release");
}
