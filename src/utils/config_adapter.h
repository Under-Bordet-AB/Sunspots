#ifndef CONFIG_ADAPTER_H
#define CONFIG_ADAPTER_H

#include <config/config.h>
#include <jj_log/jj_log.h>

/**
 * @brief Convert a config subtree to a jj_log_config struct.
 *        This acts as an adapter between the Config system and the Logger.
 *
 * @param cfg Subtree configuration (e.g. "modules.jj_log").
 * @param out_log_cfg Pointer to the struct to fill.
 */
void config_to_logger(const config* cfg, jj_log_config* out_log_cfg);

/**
 * @brief  Initialize jj_log directly from a configuration subtree.
 * @param  cfg Subtree configuration (e.g. "modules.jj_log").
 * @return 0 on success, or non-zero error from jj_log_init.
 */
int jj_log_init_from_config(const config* cfg);

#endif  // CONFIG_ADAPTER_H
