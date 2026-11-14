/* Board port: a single owning task, no guest threads, WASI, AOT or system heap. */
#include "platform_api_vmcore.h"
#include "wasm_export.h"
extern uint8_t *robot_native_stack_bottom;
int bh_platform_init(void) { return 0; }
void bh_platform_destroy(void) { }
void *os_malloc(unsigned n) { (void)n; return NULL; }
void *os_realloc(void *p, unsigned n) { (void)p; (void)n; return NULL; }
void os_free(void *p) { (void)p; }
int os_printf(const char *f, ...) { (void)f; return 0; }
int os_vprintf(const char *f, va_list a) { (void)f; (void)a; return 0; }
uint8 *os_thread_get_stack_boundary(void) { return robot_native_stack_bottom; }
korp_tid os_self_thread(void) { return xTaskGetCurrentTaskHandle(); }
uint64 os_time_get_boot_us(void) { return (uint64)xTaskGetTickCount() * 1000000U / configTICK_RATE_HZ; }
int os_mutex_init(korp_mutex *m) { m->handle=xSemaphoreCreateMutexStatic(&m->storage); return m->handle ? 0 : -1; }
int os_mutex_destroy(korp_mutex *m) { vSemaphoreDelete(m->handle); return 0; }
int os_mutex_lock(korp_mutex *m) { return xSemaphoreTake(m->handle,portMAX_DELAY)==pdTRUE ? 0 : -1; }
int os_mutex_unlock(korp_mutex *m) { return xSemaphoreGive(m->handle)==pdTRUE ? 0 : -1; }
uint64 os_time_thread_cputime_us(void) { return os_time_get_boot_us(); }
/* No MMU: linear memory stays in the bounded runtime pool; interpreter performs
 * software bounds checks. No executable mappings are supported. */
void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
 (void)hint; (void)file;
 if ((prot & MMAP_PROT_EXEC) || (flags & MMAP_MAP_FIXED) || size > UINT32_MAX) return NULL;
 return wasm_runtime_malloc((uint32_t)size);
}
void os_munmap(void *addr, size_t size) { (void)size; wasm_runtime_free(addr); }
void *os_mremap(void *addr, size_t old_size, size_t new_size)
{ (void)old_size; return new_size > UINT32_MAX ? NULL : wasm_runtime_realloc(addr,(uint32_t)new_size); }
int os_mprotect(void *addr, size_t size, int prot)
{ (void)addr; (void)size; return (prot & MMAP_PROT_EXEC) ? -1 : 0; }
