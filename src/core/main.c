// #include <stdbool.h>
// #include <stdio.h>

// #include "config/config.h"
// // #include "libs/jj_log/jj_log.h" // TODO: Enable when submodule available

// int main(int argc, char** argv) {
//     config* cfg = config_create();
//     if (!cfg) {
//         // jj_log_error("Main", "Failed to create config");
//         return 1;
//     }
//     if (config_load_file(cfg, "config/sunspots.json") != 0) {
//         // jj_log_info("Main", "Notice: No config file loaded (using defaults/env only)");
//     }
//     config_load_env(cfg);
//     config_load_args(cfg, argc, argv);

//     config_destroy(&cfg);
//     return 0;
// }
