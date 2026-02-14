#include "logger_provider.h"

#include "logger_printf.h"
#include "logger_syslog.h"
#include "logger_sdk.h"

const logger_t *get_logger(logger_implementation_t implementation)
{
	switch (implementation) {
		case LOGGER_PRINTF:
			return &logger_printf;
			break;
		case LOGGER_SYSLOG:
			return &logger_syslog;
			break;
		case LOGGER_SDK:
			return &logger_sdk;
			break;
	}

	return &logger_printf;
}
