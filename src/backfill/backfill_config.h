#ifndef SUNSPOTS_BACKFILL_CONFIG_H
#define SUNSPOTS_BACKFILL_CONFIG_H

#include "backfill_common.h"

int backfill_has_env_config(void);
void backfill_config_parse(backfill_config *cfg);

#endif
