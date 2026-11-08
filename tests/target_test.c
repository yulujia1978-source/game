/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "target.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static ga_proc_t proc(pid_t pid,const char *name,const char *title) {
    ga_proc_t p={.pid=pid};snprintf(p.name,sizeof(p.name),"%s",name);
    snprintf(p.title_id,sizeof(p.title_id),"%s",title);return p;
}
int main(void) {
    turbo_targets_t extra={0};ga_proc_t out;
    ga_proc_t ps5=proc(101,"eboot.bin","PPSA12345");
    ga_proc_t ps4=proc(102,"eboot.bin","CUSA12345");
    ga_proc_t emu=proc(103,"retroarch.elf","");
    assert(turbo_target_kind(&ps5,&extra)==TURBO_TARGET_PS5);
    assert(turbo_target_kind(&ps4,&extra)==TURBO_TARGET_PS4);
    const char *names[]={"RetroArch","ppsspp.elf","fbneo.bin","SwitchPS5","pcsx2","duckstation.elf"};
    for (unsigned i=0;i<sizeof(names)/sizeof(*names);i++) {
        ga_proc_t p=proc(103,names[i],"");
        assert(turbo_target_kind(&p,&extra)==TURBO_TARGET_EMULATOR);
    }
    ga_proc_t other=proc(104,"eboot.bin","BREW12345");
    assert(!turbo_target_kind(&other,&extra));
    strcpy(extra.ids[extra.count++],"BREW12345");
    assert(turbo_target_kind(&other,&extra)==TURBO_TARGET_CUSTOM);
    const ga_proc_t denied[]={proc(1,"SceShellUI","PPSA12345"),proc(2,"eboot.bin","NPXS12345"),
        proc(3,"payload.elf","PPSA12345"),proc(4,"eboot.bin","CUSA1234X"),
        proc(5,"unrelated.elf","PPSA12345"),proc(6,"retroarch.elf","NPXS12345")};
    for (unsigned i=0;i<sizeof(denied)/sizeof(*denied);i++) assert(!turbo_target_kind(&denied[i],&extra));
    ga_proc_t list[]={ps5,ps5,denied[0]};
    assert(turbo_target_select(list,3,999,&extra,&out)==1 && out.pid==101);
    assert(!turbo_target_select(list,3,101,&extra,&out));
    list[1]=ps4;assert(turbo_target_select(list,3,999,&extra,&out)==2 && out.pid==-1);
    list[0]=emu;list[1]=denied[0];
    assert(turbo_target_select(list,2,999,&extra,&out)==1 && out.pid==103);
    assert(!turbo_target_select(list,GA_PROC_MAX,999,&extra,&out));
    char path[]="/tmp/turbo-targets-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    FILE *f=fopen(path,"w");assert(f);fputs("# emulator app IDs\nBREW12345\n EMUL00001 # test\n",f);fclose(f);
    assert(!turbo_targets_load(&extra,path) && extra.count==2);
    f=fopen(path,"a");assert(f);fputs("NPXS12345\n",f);fclose(f);
    assert(turbo_targets_load(&extra,path) && !extra.count); /* fail closed */
    unlink(path);assert(turbo_targets_load(&extra,path) && !extra.count);
    puts("PASS: PS5, PS4, emulator aliases, explicit IDs, system exclusion, ambiguity and self exclusion");
}
