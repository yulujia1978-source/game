/* Exercise the real PS5 removal path with process memory backed by host RAM. */
#include "inject.h"
#include "kstuff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

const char *const ga_fn_names[GA_FN_COUNT] = {0};
static int fail_write;
void ga_log(const char *fmt, ...) { (void)fmt; }
int kernel_proc_copyout(pid_t pid, intptr_t addr, void *dst, size_t len) {
    (void)pid; memcpy(dst,(void *)addr,len); return 0;
}
int kernel_proc_copyin(pid_t pid, const void *src, intptr_t addr, size_t len) {
    (void)pid; if (fail_write) return -1;
    memcpy((void *)addr,src,len); return 0;
}
int ga_kstuff_available(void) { return 1; }
int ga_kstuff_mmap(pid_t pid,size_t len,intptr_t *addr) {
    (void)pid;(void)len;(void)addr;return -1;
}
int ga_kstuff_munmap(pid_t pid,intptr_t addr,size_t len) {
    (void)pid;(void)addr;(void)len;assert(0);return -1;
}
int ga_kstuff_mprotect(pid_t pid,intptr_t addr,size_t len,int prot) {
    (void)pid;(void)addr;(void)len;(void)prot;return 0;
}
static void run(int mode) {
    ga_args_t a={0}; uint64_t slot=0x1010;
    a.magic=GA_MAGIC; a.version=GA_LAYOUT_VERSION; a.enabled=1;
    a.hook_count=1; a.hook_slot[0]=(uintptr_t)&slot;
    a.hook_orig[0]=0x5555; a.hook_prot[0]=PROT_READ;
    ga_inject_t in={.pid=1,.code=0x1000,.code_len=0x100,
                    .args=(intptr_t)&a,.installed=1};
    if (mode==1) slot=0x5555; /* already restored */
    if (mode==2) slot=0x9999; /* owned by another plugin */
    fail_write=mode==3;
    int rc=ga_inject_remove(&in);
    assert(rc==((mode<2)?0:-1));
    assert(slot==((mode<2)?0x5555:(mode==2?0x9999:0x1010)));
    assert(!in.installed);
    if (!fail_write) assert(!a.enabled);
}
int main(void) {
    for (int mode=0;mode<4;mode++) run(mode);
    puts("inject removal regression tests passed");
}
