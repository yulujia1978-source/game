/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "settings.h"
#include "web.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    ga_args_t a;memset(&a,0,sizeof(a));turbo_defaults(&a);
    assert(a.enabled==1 && !a.mask && a.half_us==25000);
    assert(turbo_valid(1,0,25000,TURBO_SHORTCUT_DOWN));assert(turbo_valid(1,0xff00,25000,TURBO_SHORTCUT_DOWN));
    assert(!turbo_valid(1,0xffff,25000,TURBO_SHORTCUT_DOWN));assert(!turbo_valid(2,0,25000,TURBO_SHORTCUT_DOWN));
    char path[]="/tmp/ps5-turbo-settings-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    assert(!turbo_save(path,1,0,25000,TURBO_SHORTCUT_DOWN));assert(!turbo_load(&a,path));assert(!a.mask);
    unsigned bits[]={0x4000,0x2000,0x8000,0x1000,0x400,0x800,0x100,0x200};
    for (unsigned i=0;i<8;i++) {
        assert(!turbo_save(path,1,bits[i],25000,TURBO_SHORTCUT_DOWN));assert(!turbo_load(&a,path));
        assert(a.mask==bits[i] && a.half_us==25000);
    }
    assert(!turbo_save(path,0,0xff00,25000,TURBO_SHORTCUT_DOWN));assert(!turbo_load(&a,path));
    assert(a.enabled==0 && a.mask==0xff00);
    assert(turbo_save(path,1,2,25000,TURBO_SHORTCUT_DOWN));assert(!turbo_load(&a,path));assert(a.mask==0xff00);
    for (unsigned shortcuts=0;shortcuts<=TURBO_SHORTCUT_ALL;shortcuts++) {
        assert(!turbo_save(path,1,0x800,25000,shortcuts));
        assert(!turbo_load(&a,path) && a.shortcuts==shortcuts);
    }
    assert(turbo_save(path,1,0x800,25000,4));
    assert(!turbo_load(&a,path) && a.shortcuts==3);
    FILE *legacy=fopen(path,"w");assert(legacy);
    fputs("enabled=1\nmask=0x800\nhalf_us=25000\n",legacy);fclose(legacy);
    assert(!turbo_load(&a,path) && a.shortcuts==TURBO_SHORTCUT_DOWN);
    legacy=fopen(path,"a");assert(legacy);fputs("shortcuts=4\n",legacy);fclose(legacy);
    assert(turbo_load(&a,path) && !a.enabled && !a.shortcuts);
    FILE *f=fopen(path,"w");assert(f);fputs("enabled=1\nmask=0x4000\nhalf_us=0\n",f);fclose(f);
    assert(turbo_load(&a,path) && !a.enabled && !a.mask);
    f=fopen(path,"w");fputs("enabled=1\n",f);fclose(f);
    assert(turbo_load(&a,path) && !a.enabled && !a.mask);
    unlink(path);assert(turbo_load(&a,path) && !a.enabled && !a.mask);
    unsigned en=0,mask=0,half=0,shortcuts=0;const char *token="abc123";
    assert(!turbo_web_form("enabled=1&mask=0&half_us=25000&shortcuts=1&token=abc123",token,&en,&mask,&half,&shortcuts));
    assert(en==1 && !mask && half==25000);
    assert(!turbo_web_form("token=abc123&shortcuts=1&mask=65280&enabled=0&half_us=25000",token,&en,&mask,&half,&shortcuts));
    for (unsigned i=0;i<4;i++) {
        char form[160];snprintf(form,sizeof(form),"token=abc123&shortcuts=%u&mask=0&enabled=1&half_us=25000",i);
        assert(!turbo_web_form(form,token,&en,&mask,&half,&shortcuts) && shortcuts==i);
    }
    const char *bad[]={"enabled=1&mask=16384&half_us=25000&shortcuts=1&token=wrong",
    "enabled=1&mask=16384&half_us=25000", "enabled=1&mask=2&half_us=25000&shortcuts=1&token=abc123",
    "enabled=1&mask=0&half_us=0&shortcuts=1&token=abc123", "enabled=1&mask=0&half_us=25000&shortcuts=1&token=abc123&shortcuts=1&mask=0",
    "enabled=1&mask=-1&half_us=25000&shortcuts=1&token=abc123", "enabled=1&mask=0x4000&half_us=25000&shortcuts=1&token=abc123",
    "enabled=1&mask=999999999999999999999&half_us=25000&shortcuts=1&token=abc123",
    "enabled=1&mask=0&half_us=25000&token=abc123",
    "enabled=1&mask=0&half_us=25000&shortcuts=4&token=abc123",
    "enabled=1&mask=0&half_us=25000&shortcuts=1&shortcuts=2&token=abc123"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(turbo_web_form(bad[i],token,&en,&mask,&half,&shortcuts));
    puts("PASS: default-off, eight buttons, save/reload, invalid config fail-closed, form token and input validation");
}
