#ifndef I_LOGGER_H
#define I_LOGGER_H

typedef enum {
	LOGGER_OK = 0,
	LOGGER_INVALID_INPUT,
	LOGGER_NOT_FOUND,
	LOGGER_LOG_ERROR,
	LOGGER_NOT_IMPLEMENTED
} logger_status_t;

typedef enum {
	LOG_INFO,
	LOG_WARNING,
	LOG_ERROR
} log_type_t;

typedef struct {
	logger_status_t (*log_something)(log_type_t severity, char* message);
} logger_t;

#endif