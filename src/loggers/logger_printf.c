#include <stdio.h>
#include <stddef.h>

#include "logger_printf.h"

static const char *get_prefix(log_type_t severity)
{
	switch (severity) {
		case LOG_INFO:
			return "INFO";
		case LOG_WARNING:
			return "WARNING";
		case LOG_ERROR:
			return "ERROR";
		default:
			return "UNKNOWN";
	}
}

static logger_status_t logger_printf_log_something(log_type_t severity, char *message)
{
	if (message == NULL) {
		return LOGGER_INVALID_INPUT;
	}

	int result = fprintf(stdout, "[%s] %s\n", get_prefix(severity), message);
	if (result < 0) {
		return LOGGER_LOG_ERROR;
	}

	return LOGGER_OK;
}

logger_t logger_printf = {
	.log_something = logger_printf_log_something,
};
