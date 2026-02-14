#include <stddef.h>

#include "logger_sdk.h"

static logger_status_t logger_sdk_log_something(log_type_t severity, char *message)
{
	(void)severity;
	(void)message;
	return LOGGER_NOT_IMPLEMENTED;
}

logger_t logger_sdk = {
	.log_something = logger_sdk_log_something,
};
