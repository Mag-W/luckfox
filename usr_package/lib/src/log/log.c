#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static log_level_t g_log_level = LOG_INFO;

void log_init(log_level_t level) {
    g_log_level = level;
}

void log_printf(log_level_t level, const char *fmt, ...) {
    if (level > g_log_level) {
        return;
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("[%s] ", time_buf);
    
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    printf("\n");
}