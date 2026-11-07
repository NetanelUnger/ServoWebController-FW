#ifndef TEST_LOG_MODULE_H
#define TEST_LOG_MODULE_H
/* Preserve argument evaluation/use so strict host builds also check diagnostic
 * call sites; do not generate UART output or require the embedded logger. */
static inline void test_log_message(const char *format, ...) { (void)format; }
#define LOG_WARNING_APP(...) test_log_message(__VA_ARGS__)
#define LOG_INFO_APP(...) test_log_message(__VA_ARGS__)
#endif
