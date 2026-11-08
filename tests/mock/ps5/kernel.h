/* Host-only substitutes for the PS5 process-memory API. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
int kernel_proc_copyout(pid_t, intptr_t, void *, size_t);
int kernel_proc_copyin(pid_t, const void *, intptr_t, size_t);
