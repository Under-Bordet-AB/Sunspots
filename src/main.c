#include <config/config.h>
#include <jj_log/jj_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/config_adapter.h>

int main(int argc, char* argv[]) {
    config* cfg = config_create();
    if (!cfg) {
        return 1;
    }

    // Can be called in any order, last called overwrites previous values
    config_load_file(cfg, "config/sunspots.json");
    config_load_env(cfg);
    config_load_args(cfg, argc, argv);

    if (jj_log_init_from_config(config_get_subtree(cfg, "modules.jj_log")) != 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    jj_log_info("MAIN", "Sunspots starting up (Ver: %s)", config_get_string_or(cfg, "system.version", "unknown"));
    jj_log_info("MAIN", "jj_log submodule loaded successfully\n");

    // ... application code ...

    jj_log_fini();
    config_destroy(&cfg);
    return 0;
}
