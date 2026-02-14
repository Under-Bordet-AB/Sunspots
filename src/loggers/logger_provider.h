#ifndef LOGGER_PROVIDER_H
#define LOGGER_PROVIDER_H

#include "../interfaces/i_logger.h"

typedef enum {
	LOGGER_PRINTF = 0,
	LOGGER_SYSLOG,
	LOGGER_SDK
} logger_implementation_t;

const logger_t *get_logger(logger_implementation_t implementation);

#endif

