#ifndef ROBOT_WAMR_PLATFORM_INTERNAL_H
#define ROBOT_WAMR_PLATFORM_INTERNAL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
typedef struct { StaticSemaphore_t storage; SemaphoreHandle_t handle; } korp_mutex;
typedef TaskHandle_t korp_tid;
typedef int korp_thread;
typedef int korp_cond;
typedef int korp_sem;
typedef int korp_rwlock;
typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef int os_timespec;
static inline os_file_handle os_get_invalid_handle(void) { return -1; }
#define BH_APPLET_PRESERVED_STACK_SIZE 512
static inline size_t os_getpagesize(void) { return 4096; }
#endif
