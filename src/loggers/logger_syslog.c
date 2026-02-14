#include <stddef.h>
#include <syslog.h>

#include "logger_syslog.h"

static int get_syslog_priority(log_type_t severity)
{
	switch ((int)severity) {
		case 0:
			return LOG_INFO;
		case 1:
			return LOG_WARNING;
		case 2:
			return LOG_ERR;
		default:
			return LOG_INFO;
	}
}

static logger_status_t logger_syslog_log_something(log_type_t severity, char *message)
{
	if (message == NULL) {
		return LOGGER_INVALID_INPUT;
	}

	syslog(get_syslog_priority(severity), "%s", message);
	return LOGGER_OK;
}

logger_t logger_syslog = {
	.log_something = logger_syslog_log_something,
};
