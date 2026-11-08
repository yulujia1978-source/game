/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "target.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
static int valid_id(const char *s) {
    if (strlen(s)!=9) return 0;
    for (unsigned i=0;i<9;i++)
        if (i<4?(s[i]<'A'||s[i]>'Z'):(s[i]<'0'||s[i]>'9')) return 0;
    return strncmp(s,"NPX",3)!=0; /* Sony system applications */
}
int turbo_targets_load(turbo_targets_t *out,const char *path) {
    memset(out,0,sizeof(*out));
    FILE *f=fopen(path,"r");if (!f) return -1;
    turbo_targets_t parsed={0};char line[128];int ok=1;
    while (fgets(line,sizeof(line),f)) {
        char *p=line;while (*p==' '||*p=='\t') p++;
        if (*p=='#'||*p=='\n'||*p=='\r'||!*p) continue;
        p[strcspn(p," \t\r\n#")]=0;
        if (!valid_id(p)||parsed.count>=TURBO_EXTRA_TARGETS) {ok=0;break;}
        snprintf(parsed.ids[parsed.count++],GA_TITLE_ID_MAX,"%s",p);
    }
    if (ferror(f)) ok=0;
    fclose(f);if (!ok) return -1;
    *out=parsed;return 0;
}
static int emulator_name(const char *name) {
    static const char *const names[]={"retroarch","ppsspp","fbneo","switchps5",
        "suyu","ryujinx","yuzu","rpcs3","duckstation","pcsx2","pcsx-rearmed"};
    char base[GA_PROC_NAME_MAX];snprintf(base,sizeof(base),"%s",name);
    char *dot=strrchr(base,'.');
    if (dot && (!strcasecmp(dot,".elf")||!strcasecmp(dot,".bin"))) *dot=0;
    for (unsigned i=0;i<sizeof(names)/sizeof(*names);i++)
        if (!strcasecmp(base,names[i])) return 1;
    return 0;
}
turbo_target_kind_t turbo_target_kind(const ga_proc_t *p,const turbo_targets_t *extra) {
    if (!p||p->pid<=0) return TURBO_TARGET_NONE;
    /* Exclude shell/services even when someone puts their ID in targets.txt. */
    if (!strncasecmp(p->name,"Sce",3)||!strncasecmp(p->name,"Shell",5)||
        !strcasecmp(p->name,"payload.elf")) return TURBO_TARGET_NONE;
    if (p->title_id[0] && !valid_id(p->title_id)) return TURBO_TARGET_NONE;
    int eboot=!strcmp(p->name,"eboot.bin");
    if (eboot && !strncmp(p->title_id,"PPSA",4)) return TURBO_TARGET_PS5;
    if (eboot && !strncmp(p->title_id,"CUSA",4)) return TURBO_TARGET_PS4;
    if (emulator_name(p->name)) return TURBO_TARGET_EMULATOR;
    if (extra && p->title_id[0])
        for (unsigned i=0;i<extra->count && i<TURBO_EXTRA_TARGETS;i++)
            if (!strcmp(p->title_id,extra->ids[i])) return TURBO_TARGET_CUSTOM;
    return TURBO_TARGET_NONE;
}
unsigned turbo_target_select(const ga_proc_t *list,unsigned n,pid_t self,
                             const turbo_targets_t *extra,ga_proc_t *out) {
    memset(out,0,sizeof(*out));out->pid=-1;
    if (!list || n>=GA_PROC_MAX) return 0; /* truncated snapshot is ambiguous */
    unsigned found=0;
    for (unsigned i=0;i<n;i++) {
        if (list[i].pid==self||!turbo_target_kind(&list[i],extra)) continue;
        if (found && list[i].pid==out->pid) continue;
        if (found) {memset(out,0,sizeof(*out));out->pid=-1;return 2;}
        *out=list[i];found=1;
    }
    return found;
}
const char *turbo_target_label(turbo_target_kind_t kind) {
    switch (kind) {
        case TURBO_TARGET_PS5:return "PS5/PPSA";
        case TURBO_TARGET_PS4:return "PS4/CUSA";
        case TURBO_TARGET_EMULATOR:return "emulator";
        case TURBO_TARGET_CUSTOM:return "custom homebrew";
        default:return "none";
    }
}
