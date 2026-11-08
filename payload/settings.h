/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "stub.h"
#ifndef TURBO_DIR
#define TURBO_DIR "/data/ps5-turbo"
#endif
/* Separate filename deliberately resets the v0.1 X default on upgrade. */
#define TURBO_SETTINGS TURBO_DIR "/settings-v2.txt"
void turbo_defaults(ga_args_t *a);
int turbo_valid(unsigned enabled, unsigned mask, unsigned half_us, unsigned shortcuts);
int turbo_load(ga_args_t *a, const char *path);
int turbo_save(const char *path, unsigned enabled, unsigned mask, unsigned half_us, unsigned shortcuts);
