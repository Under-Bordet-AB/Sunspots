#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "config/config.h"

void test_args_basic(void) {
    config* cfg = config_create();
    char* argv[] = {"app", "--port", "8080", "--debug", "true", "--host", "localhost"};
    int argc = 7;

    assert(config_load_args(cfg, argc, argv) == 0);

    assert(config_get_int_or(cfg, "port", 0) == 8080);
    assert(config_get_bool_or(cfg, "debug", false) == true);
    
    char buf[64];
    assert(config_get_string(cfg, "host", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "localhost") == 0);

    config_destroy(&cfg);
    printf("[PASS] Basic Args\n");
}

void test_args_nested(void) {
    config* cfg = config_create();
    char* argv[] = {"app", "--server.port", "9090", "--server.ssl.enabled", "true"};
    int argc = 5;

    assert(config_load_args(cfg, argc, argv) == 0);

    const config* server = config_get_subtree(cfg, "server");
    assert(server != NULL);
    assert(config_get_int_or(server, "port", 0) == 9090);

    const config* ssl = config_get_subtree(server, "ssl");
    assert(ssl != NULL);
    assert(config_get_bool_or(ssl, "enabled", false) == true);

    config_destroy(&cfg);
    printf("[PASS] Nested Args\n");
}

void test_args_types(void) {
    config* cfg = config_create();
    char* argv[] = {"app", "--val_int", "123", "--val_bool", "false", "--val_str", "hello"};
    int argc = 7;

    assert(config_load_args(cfg, argc, argv) == 0);

    assert(config_get_int_or(cfg, "val_int", 0) == 123);
    assert(config_get_bool_or(cfg, "val_bool", true) == false);
    
    char buf[64];
    assert(config_get_string(cfg, "val_str", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "hello") == 0);

    config_destroy(&cfg);
    printf("[PASS] Arg Types\n");
}

int run_arg_tests(void) {
    test_args_basic();
    test_args_nested();
    test_args_types();
    return 3;
}
