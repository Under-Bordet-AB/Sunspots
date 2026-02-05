SHELL = /bin/bash

CC = gcc
CFLAGS = -std=c99 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g -O0 -I src -I .
LDFLAGS = -pthread -lcurl

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
ROOT_DAEMON = sunspotsd

CORE_SRCS = \
	src/config/config.c \
	src/core/config_runtime.c \
	src/core/worker_bootstrap.c \
	src/log/sunspots_log.c \
	src/sdk/canonical_types.c \
	src/sdk/sunspots_sdk.c \
	src/libs/json/cJSON.c

PROVIDER_SRCS = \
	src/providers/openmeteo_provider.c \
	src/providers/smhi_provider.c

DAEMON_SRCS = src/core/sunspotsd.c $(CORE_SRCS)
FETCH_OPENMETEO_SRCS = src/workers/fetch_openmeteo.c src/providers/openmeteo_provider.c $(CORE_SRCS)
FETCH_SMHI_SRCS = src/workers/fetch_smhi.c src/providers/smhi_provider.c $(CORE_SRCS)
CALC_SRCS = src/workers/calc_smhi_avg_temp.c $(CORE_SRCS)
SERVER_SRCS = src/server/sunspots_server.c src/config/config.c src/core/worker_bootstrap.c src/log/sunspots_log.c src/libs/json/cJSON.c
TEST_SRCS = tests/test_config.c tests/test_args.c tests/test_sdk.c tests/test_fetch_providers.c src/config/config.c src/core/worker_bootstrap.c src/sdk/canonical_types.c src/sdk/sunspots_sdk.c src/log/sunspots_log.c $(PROVIDER_SRCS) src/libs/json/cJSON.c

DAEMON_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(DAEMON_SRCS))
FETCH_OPENMETEO_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(FETCH_OPENMETEO_SRCS))
FETCH_SMHI_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(FETCH_SMHI_SRCS))
CALC_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(CALC_SRCS))
SERVER_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SERVER_SRCS))
TEST_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(TEST_SRCS))

.PHONY: all clean test run root-daemon

all: $(BIN_DIR)/sunspotsd $(BIN_DIR)/fetch_openmeteo $(BIN_DIR)/fetch_smhi $(BIN_DIR)/calc_smhi_avg_temp $(BIN_DIR)/sunspots_server root-daemon

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/sunspotsd: $(DAEMON_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/fetch_openmeteo: $(FETCH_OPENMETEO_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/fetch_smhi: $(FETCH_SMHI_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/calc_smhi_avg_temp: $(CALC_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/sunspots_server: $(SERVER_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/run_tests: $(TEST_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(BIN_DIR)/run_tests
	./$(BIN_DIR)/run_tests

root-daemon: $(BIN_DIR)/sunspotsd
	cp $(BIN_DIR)/sunspotsd $(ROOT_DAEMON)
	chmod +x $(ROOT_DAEMON)

run: all
	@mkdir -p .db endpoints logs runtime/config
	./$(ROOT_DAEMON)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(ROOT_DAEMON)
