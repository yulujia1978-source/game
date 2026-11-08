/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
typedef struct {
    int service_fd,upgrade_fd,replaced,service_owned;
    char error[240];
} turbo_instance_t;
/* Request cooperative STOP and wait for the existing service lock; never kill
 * a PID or unlink the lock file. The upgrade lock serializes new launchers. */
int turbo_instance_acquire(turbo_instance_t *,const char *dir,const char *version,unsigned timeout_ms,uint64_t (*birth)(pid_t));
void turbo_instance_ready(turbo_instance_t *);
void turbo_instance_close(turbo_instance_t *);
/* Remove only verified older Turbo ELFs in a local manager slot whose metadata
 * already points to this verified new release. No recursion or metadata edits. */
int turbo_cleanup_versions(const char *slot,const char *version);
