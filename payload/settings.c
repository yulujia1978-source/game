/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int turbo_valid(unsigned enabled,unsigned mask,unsigned half,unsigned shortcuts) {
    return enabled<=1 && !(mask&~TURBO_ALLOWED) && half==TURBO_HALF_US && !(shortcuts&~TURBO_SHORTCUT_ALL);
}
void turbo_defaults(ga_args_t *a) {a->enabled=1;a->mask=0;a->half_us=TURBO_HALF_US;a->shortcuts=TURBO_SHORTCUT_DOWN;}
int turbo_load(ga_args_t *a,const char *path) {
    a->enabled=0;a->mask=0;a->half_us=TURBO_HALF_US;a->shortcuts=0;
    FILE *f=fopen(path,"r");if (!f) return -1;
    char line[160],key[40],value[80];
    unsigned enabled=1,mask=0,half=TURBO_HALF_US,shortcuts=TURBO_SHORTCUT_DOWN,seen=0;int valid=1;
    while (fgets(line,sizeof(line),f)) {
        if (line[0]=='#' || line[0]=='\n' || line[0]=='\r') continue;
        if (sscanf(line,"%39[^=]=%79s",key,value)!=2) {valid=0;continue;}
        char *end;unsigned long n=strtoul(value,&end,0);
        if (end==value || *end || value[0]=='-' || n>0xffffffffUL) {valid=0;continue;}
        if (!strcmp(key,"enabled")) {enabled=(unsigned)n;seen|=1;}
        else if (!strcmp(key,"mask")) {mask=(unsigned)n;seen|=2;}
        else if (!strcmp(key,"half_us")) {half=(unsigned)n;seen|=4;}
        else if (!strcmp(key,"shortcuts")) {if (seen&8) valid=0;shortcuts=(unsigned)n;seen|=8;}
        else valid=0;
    }
    if (ferror(f)) valid=0;
    fclose(f);
    if (!valid || (seen&7)!=7 || !turbo_valid(enabled,mask,half,shortcuts)) return -1;
    a->enabled=enabled;a->mask=mask;a->half_us=half;a->shortcuts=shortcuts;a->settings_seq++;
    return 0;
}
int turbo_save(const char *path,unsigned enabled,unsigned mask,unsigned half,unsigned shortcuts) {
    if (!turbo_valid(enabled,mask,half,shortcuts)) return -1;
    char temp[256];if (snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp)) return -1;
    FILE *f=fopen(temp,"w");if (!f) return -1;
    int ok=fprintf(f,"enabled=%u\nmask=0x%04x\nhalf_us=%u\nshortcuts=%u\n",enabled,mask,half,shortcuts)>0;
    if (fflush(f) || fsync(fileno(f))) ok=0;
    if (fclose(f)) ok=0;
    if (ok && !rename(temp,path)) return 0;
    unlink(temp);return -1;
}
