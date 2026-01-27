# Simplified Makefile
# Targets:
#   debug       - Build with -g, -O0, ASAN (default)
#   release     - Build with -O2, no ASAN
#   analyze     - Run structure analysis (placeholder if scripts missing)
#   lint        - Run clang-tidy
#   format      - Run clang-format
#   clean       - Remove build artifacts

SHELL = /bin/bash

CC = gcc
CFLAGS_COMMON = -std=c99 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -pthread -I src -I .
CFLAGS_DEBUG = $(CFLAGS_COMMON) -g -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS_RELEASE = $(CFLAGS_COMMON) -O2 -DNDEBUG
CFLAGS_VALGRIND = $(CFLAGS_COMMON) -g -O0

# Output Directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Output Binary
TARGET_BIN = $(BIN_DIR)/server

# Source Files
SRC := $(shell find src -name "*.c")
OBJ = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRC))

# Test Sources (Auto-discovery)
TEST_SRC = $(wildcard tests/*.c) $(wildcard src/*/test_*.c)
TEST_OBJ = $(patsubst %.c,$(OBJ_DIR)/%.o,$(TEST_SRC))
TEST_BIN = $(BIN_DIR)/run_tests

# Submodules (libs with Makefiles)
SUBMODULES = $(shell find src/libs -maxdepth 2 -name Makefile -exec dirname {} \;)

# Colors
BLUE_BOLD := \033[1;34m
GREEN := \033[0;32m
RED := \033[0;31m
NC := \033[0m # No Color

.PHONY: all debug release clean valgrind lint format test test_submodules header_compile compile link run fireworks

# Run everything: Format -> Lint -> Compile -> Test -> Link
all: CFLAGS = $(CFLAGS_DEBUG)
all: LDFLAGS = -fsanitize=address,undefined
all: format lint compile test link

# --- Print Helpers ---
header_compile:
	@printf "$(BLUE_BOLD)>> Compiling Objects$(NC)\n"



fireworks:
	@printf "$(BLUE_BOLD)>> Starting Celebration$(NC)\n"
	@bash -c 'for i in {1..40}; do \
		tput cup $$((RANDOM % $$(tput lines))) $$((RANDOM % $$(tput cols))); \
		printf "\e[1;3$$(($$RANDOM % 7 + 1))m*\e[0m"; \
		sleep 0.02; \
	done; \
	for i in {1..8}; do \
		x=$$((RANDOM % $$(tput cols))); y=$$((RANDOM % $$(tput lines))); \
		col=3$$(($$RANDOM % 7 + 1)); \
		for r in {1..4}; do \
			tput cup $$(($$y-$$r)) $$x 2>/dev/null && printf "\e[1;$${col}m+\e[0m"; \
			tput cup $$(($$y+$$r)) $$x 2>/dev/null && printf "\e[1;$${col}m+\e[0m"; \
			tput cup $$y $$(($$x-$$r*2)) 2>/dev/null && printf "\e[1;$${col}m-\e[0m"; \
			tput cup $$y $$(($$x+$$r*2)) 2>/dev/null && printf "\e[1;$${col}m-\e[0m"; \
			sleep 0.03; \
		done; \
	done; \
	tput clear; \
	printf "$(GREEN)➜  ALL SYSTEMS NOMINAL. CELEBRATION COMPLETE.$(NC)\n"'

# --- Build Rules ---

# Compilation Rule
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo 1 >> $(BUILD_DIR)/.build_count
	@printf "  $(GREEN)[PASS]$(NC) $(notdir $<)\n"

# Explicit Compilation Target
compile: header_compile
	@mkdir -p $(BUILD_DIR)
	@rm -f $(BUILD_DIR)/.build_count
	@touch $(BUILD_DIR)/.build_count
	@$(MAKE) -s $(OBJ) $(TEST_OBJ) CFLAGS="$(CFLAGS)"
	@COUNT=$$(wc -l < $(BUILD_DIR)/.build_count 2>/dev/null || echo 0); \
	if [ "$$COUNT" -eq "0" ]; then \
		printf "  $(GREEN)➜  No files needed recompilation$(NC)\n"; \
	else \
		printf "  $(GREEN)➜  Successfully compiled $$COUNT files$(NC)\n"; \
	fi; \
	rm -f $(BUILD_DIR)/.build_count

# Link Main Binary
link: $(TARGET_BIN)

$(TARGET_BIN): $(OBJ) | $(BIN_DIR)
	@printf "$(BLUE_BOLD)>> Linking Binary$(NC)\n"
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@printf "  $(GREEN)[PASS]$(NC) $(notdir $@)\n"

# Link Test Binary
$(TEST_BIN): $(TEST_OBJ) $(filter-out $(OBJ_DIR)/src/core/main.o, $(OBJ)) | $(BIN_DIR)
	@printf "$(BLUE_BOLD)>> Linking Tests$(NC)\n"
	@if [ -n "$(strip $(TEST_SRC))" ]; then \
		$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS); \
		printf "  $(GREEN)[PASS]$(NC) $(notdir $@)\n"; \
	else \
		echo "No test sources found."; \
	fi

$(BIN_DIR):
	@mkdir -p $@

# --- Targets ---

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: LDFLAGS = -fsanitize=address,undefined
debug: format compile link

release: CFLAGS = $(CFLAGS_RELEASE)
release: format compile link

test: CFLAGS = $(CFLAGS_DEBUG)
test: LDFLAGS = -fsanitize=address,undefined
test: format compile $(TEST_BIN) test_submodules
	@printf "$(BLUE_BOLD)>> Running Unit Tests$(NC)\n"
	@if [ -f $(TEST_BIN) ]; then \
		OUTPUT=$$(./$(TEST_BIN) 2>&1); \
		echo "$$OUTPUT" | grep -v "TEST_SUMMARY" | sed 's/^/  /' \
		| sed 's/\[PASS\]/\x1b[0;32m[PASS]\x1b[0m/g'; \
		SUMMARY=$$(echo "$$OUTPUT" | grep "TEST_SUMMARY"); \
		PASSED=$$(echo "$$SUMMARY" | awk '{sum += $$5} END {print sum}'); \
		TOTAL=$$(echo "$$SUMMARY" | awk '{sum += $$2} END {print sum}'); \
		if [ -n "$$TOTAL" ] && [ "$$PASSED" == "$$TOTAL" ]; then \
			printf "  $(GREEN)➜  Passed $$PASSED of $$TOTAL test cases$(NC)\n"; \
		else \
			printf "  $(RED)✖  Failed: Only $$PASSED of $$TOTAL tests passed$(NC)\n"; \
			exit 1; \
		fi \
	else \
		echo "  No tests found."; \
	fi

test_submodules:
	@for dir in $(SUBMODULES); do \
		$(MAKE) -C $$dir test > /dev/null 2>&1 || exit 1; \
	done

clean:
	@rm -rf $(BUILD_DIR)
	@printf "$(BLUE_BOLD)>> Cleaning$(NC)\n"
	@printf "  $(GREEN)[PASS]$(NC) Cleaned build artifacts\n"

# Helper to generate compile_commands.json without external tools
compile_commands.json: clean
	@printf "$(BLUE_BOLD)>> Generating Compilation Database$(NC)\n"
	@echo "[" > $@
	@FIRST=1; \
	for f in $(SRC) $(TEST_SRC); do \
		if [ $$FIRST -ne 1 ]; then echo "," >> $@; fi; \
		echo "  {" >> $@; \
		echo "    \"directory\": \"$(shell pwd)\"," >> $@; \
		echo "    \"command\": \"$(CC) $(CFLAGS_DEBUG) -c $$f -o $(OBJ_DIR)/$${f%.c}.o\"," >> $@; \
		echo "    \"file\": \"$$f\"" >> $@; \
		echo "  }" >> $@; \
		FIRST=0; \
	done
	@echo "]" >> $@
	@printf "  $(GREEN)[PASS]$(NC) compile_commands.json generated\n"

# lint: format compile_commands.json
# 	@printf "$(BLUE_BOLD)>> Banned Function Check$(NC)\n"
# 	@bash scripts/check_banned.sh || exit 1
# 	@printf "$(BLUE_BOLD)>> Linting$(NC)\n"
# 	@if command -v clang-tidy >/dev/null; then \
# 		OUTPUT=$$(find src -name "*.c" -not -path "src/libs/*" | xargs clang-tidy 2>&1 | grep -vE "^[0-9]+ warnings? generated|^Suppressed [0-9]+ warnings|^Use -header-filter|^[[:space:]]*$$" || true); \
# 		if [ -n "$$OUTPUT" ]; then \
# 			printf "$$OUTPUT\n"; \
# 			exit 1; \
# 		fi; \
# 		printf "  $(GREEN)[PASS]$(NC) Clang-tidy checks completed\n"; \
# 	else \
# 		echo "  clang-tidy not found, skipping."; \
# 	fi

format:
	@printf "$(BLUE_BOLD)>> Formatting$(NC)\n"
	@if command -v clang-format >/dev/null; then \
		find src -name "*.c" -o -name "*.h" | xargs clang-format -i; \
		printf "  $(GREEN)[PASS]$(NC) Clang-format applied\n"; \
	else \
		echo "  clang-format not found, skipping."; \
	fi

valgrind: CFLAGS = $(CFLAGS_VALGRIND)
valgrind: compile $(TARGET_BIN)
	@printf "$(BLUE_BOLD)>> Running Valgrind$(NC)\n"
	@valgrind --leak-check=full --track-origins=yes $(TARGET_BIN)

run: format debug
	@printf "$(BLUE_BOLD)>> Launching Server$(NC)\n"
	@./$(TARGET_BIN)