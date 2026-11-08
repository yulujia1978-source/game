/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "stub.h"
#ifndef TURBO_PORT
#define TURBO_PORT 6081
#endif
typedef struct {int fd;char token[33];char process[20];int error;const char *error_stage;} turbo_web_t;
int turbo_web_open(turbo_web_t *w);
void turbo_web_poll(turbo_web_t *w,ga_args_t *cfg,int attached,const char *title);
int turbo_web_form(const char *body,const char *token,unsigned *enabled,unsigned *mask,unsigned *half,unsigned *shortcuts);
