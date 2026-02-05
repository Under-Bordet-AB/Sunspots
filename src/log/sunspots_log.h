#ifndef SUNSPOTS_LOG_H
#define SUNSPOTS_LOG_H

#include <stdarg.h>

typedef enum sunspots_log_level {
    SUNSPOTS_LOG_TRACE = 0,
    SUNSPOTS_LOG_DEBUG = 1,
    SUNSPOTS_LOG_INFO = 2,
    SUNSPOTS_LOG_WARN = 3,
    SUNSPOTS_LOG_ERROR = 4,
    SUNSPOTS_LOG_FATAL = 5
} sunspots_log_level;

typedef struct sunspots_log sunspots_log;

int sunspots_log_open(sunspots_log** out, const char* path, const char* proc, const char* worker,
                      const char* config_version);
void sunspots_log_close(sunspots_log** logp);

int sunspots_log_vwrite(sunspots_log* log, sunspots_log_level level, const char* category, const char* file, int line,
                        const char* fmt, va_list ap);
int sunspots_log_write(sunspots_log* log, sunspots_log_level level, const char* category, const char* file, int line,
                       const char* fmt, ...);

const char* sunspots_log_level_name(sunspots_log_level level);

#define SUNSPOTS_LOG_TRACE(log, cat, ...) \
    sunspots_log_write(log, SUNSPOTS_LOG_TRACE, cat, __FILE__, __LINE__, __VA_ARGS__)
#define SUNSPOTS_LOG_DEBUG(log, cat, ...) \
    sunspots_log_write(log, SUNSPOTS_LOG_DEBUG, cat, __FILE__, __LINE__, __VA_ARGS__)
#define SUNSPOTS_LOG_INFO(log, cat, ...) \
    sunspots_log_write(log, SUNSPOTS_LOG_INFO, cat, __FILE__, __LINE__, __VA_ARGS__)
#define SUNSPOTS_LOG_WARN(log, cat, ...) \
    sunspots_log_write(log, SUNSPOTS_LOG_WARN, cat, __FILE__, __LINE__, __VA_ARGS__)
#define SUNSPOTS_LOG_ERROR(log, cat, ...) \
    sunspots_log_write(log, SUNSPOTS_LOG_ERROR, cat, __FILE__, __LINE__, __VA_ARGS__)
#define SUNSPOTS_LOG_FATAL(log, cat, ...) \
    sunspots_log_write(log, SUNSPOTS_LOG_FATAL, cat, __FILE__, __LINE__, __VA_ARGS__)

#endif
