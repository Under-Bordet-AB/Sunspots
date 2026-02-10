#include <stdlib.h>

#include "core/worker_bootstrap.h"
#include "fetch/fetch_engine.h"

int main(int argc, char** argv) {
    config* cfg = NULL;
    char version[64];
    if (worker_cfg_load_from_env(&cfg, version, sizeof(version)) != 0) {
        return EXIT_FAILURE;
    }

    const config* common = NULL;
    const config* worker = NULL;
    if (worker_cfg_get_common(cfg, &common) != 0 || worker_cfg_get_section(cfg, &worker) != 0) {
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }

    int rc = fetch_engine_run(argc, argv, cfg, common, worker);
    config_destroy(&cfg);
    return rc;
}
