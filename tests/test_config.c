#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "config/config.h"

int run_arg_tests(void);

void test_complex_config(void) {
    // Use static fixture (persists across builds)
    const char* path = "tests/fixtures/complex_config.json";

    // 1. Lifecycle
    config* cfg = config_create();
    assert(cfg != NULL);

    int ret = config_load_file(cfg, path);
    if (ret != 0) {
        fprintf(stderr, "FAILED to load config at %s. ret=%d\n", path, ret);
        exit(1);
    }

    // 2. Application Module (View Access)
    {
        const config* app = config_get_subtree(cfg, "application");
        assert(app != NULL);
        
        // String access
        char buf[64] = {0};
        assert(config_get_string(app, "name", buf, sizeof(buf)) == 0);
        assert(strcmp(buf, "TestApp") == 0);
        
        // Bool access
        bool debug = false;
        assert(config_get_bool(app, "debug", &debug) == 0);
        assert(debug == true);
        
        // NO destruction of 'app' needed!
    }

    // 3. Server & Nested SSL
    {
        const config* srv = config_get_subtree(cfg, "server");
        assert(srv != NULL);
        
        int port = 0;
        assert(config_get_int(srv, "port", &port) == 0);
        assert(port == 8080);
        
        const config* ssl = config_get_subtree(srv, "ssl");
        assert(ssl != NULL);
        
        bool enabled = false;
        assert(config_get_bool(ssl, "enabled", &enabled) == 0);
        assert(enabled == true);
    }

    // 4. Dot Notation Access (New Feature)
    {
        int db_port = 0;
        // Direct access into deep structure
        assert(config_get_int(cfg, "database.primary.port", &db_port) == 0);
        assert(db_port == 5432);

        // Safe/Default access
        int missing_val = config_get_int_or(cfg, "non.existent.key", 999);
        assert(missing_val == 999);
    }
    
    // 5. Cleanup
    config_destroy(&cfg);
    // Note: 'app', 'srv', 'ssl' pointers are now invalid. 
    // Accessing them is Undefined Behavior.
    
    printf("[PASS] Complex Config\n");
}

// int main(void) {
//     int passed = 0;
//     test_complex_config();
//     passed++;
//     passed += run_arg_tests();

//     printf("TEST_SUMMARY: %d tests run, %d passed, 0 failed\n", passed, passed);
//     return 0;
// }
