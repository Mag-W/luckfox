#ifndef LOG_H
#define LOG_H

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

void log_init(log_level_t level);
void log_printf(log_level_t level, const char *fmt, ...);

#define LOG_ERROR_FMT(fmt, ...) log_printf(LOG_ERROR, "[ERROR] " fmt, ##__VA_ARGS__)
#define LOG_WARN_FMT(fmt, ...)  log_printf(LOG_WARN,  "[WARN]  " fmt, ##__VA_ARGS__)
#define LOG_INFO_FMT(fmt, ...)  log_printf(LOG_INFO,  "[INFO]  " fmt, ##__VA_ARGS__)
#define LOG_DEBUG_FMT(fmt, ...) log_printf(LOG_DEBUG, "[DEBUG] " fmt, ##__VA_ARGS__)

#endif /* LOG_H */