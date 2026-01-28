#include "config_adapter.h"

#include <stddef.h>

void config_to_logger(const config* cfg, jj_log_config* out_log_cfg) {
    if (!cfg || !out_log_cfg) {
        return;
    }

    out_log_cfg->file_path = config_get_string_or(cfg, "file_path", "sunspots.log");
    out_log_cfg->console_enabled = config_get_bool_or(cfg, "console_enabled", true);
    out_log_cfg->console_color = config_get_bool_or(cfg, "console_color", true);
    out_log_cfg->file_max_bytes = (size_t) config_get_int_or(cfg, "file_max_bytes", 0);
}

int jj_log_init_from_config(const config* cfg) {
    if (!cfg) {
        return -1;
    }
    jj_log_config lc;
    config_to_logger(cfg, &lc);
    return jj_log_init(&lc);
}
