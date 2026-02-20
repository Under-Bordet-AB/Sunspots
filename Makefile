SHELL := /bin/sh
.DEFAULT_GOAL := build
PATH := $(HOME)/.local/bin:$(PATH)

GIT_BRANCH ?= $(shell git symbolic-ref --quiet --short HEAD 2>/dev/null || echo "no_git")

CMAKE ?= cmake
CTEST ?= ctest
GENERATOR ?= Unix Makefiles
CONFIG ?= Debug
RUN_ARGS ?=
DAEMON_BIN ?= sunspots_daemon
M ?=
BUILD_DIR ?= build/debug
VALGRIND_BUILD_DIR ?= build/valgrind
TIDY_BUILD_DIR ?= build/tidy
MODULE_TARGET_HELPER ?= scripts/module_targets.sh

LOG_ROOT ?= logs/make/$(GIT_BRANCH)
RAW_LOGS_DIR ?= $(LOG_ROOT)/raw_logs
DEBUG_CONFIG_LOG ?= $(RAW_LOGS_DIR)/debug_configure.log
DEBUG_BUILD_LOG ?= $(RAW_LOGS_DIR)/debug_build.log
DEBUG_TESTS_BUILD_LOG ?= $(RAW_LOGS_DIR)/debug_build_tests.log
DEBUG_TEST_LOG ?= $(RAW_LOGS_DIR)/debug_test.log
VALGRIND_CONFIG_LOG ?= $(RAW_LOGS_DIR)/valgrind_configure.log
VALGRIND_BUILD_LOG ?= $(RAW_LOGS_DIR)/valgrind_build.log
VALGRIND_TESTS_BUILD_LOG ?= $(RAW_LOGS_DIR)/valgrind_build_tests.log
VALGRIND_TEST_LOG ?= $(RAW_LOGS_DIR)/valgrind_test.log
VALGRIND_WRAPPER_LOG ?= $(RAW_LOGS_DIR)/run_valgrind_$(DAEMON_BIN).log
TIDY_CONFIG_LOG ?= $(RAW_LOGS_DIR)/tidy_configure.log
TIDY_BUILD_LOG ?= $(RAW_LOGS_DIR)/tidy_build.log
CPPCHECK_LOG ?= $(RAW_LOGS_DIR)/cppcheck.log
LIZARD_LOG ?= $(RAW_LOGS_DIR)/lizard.log
VALGRIND_LOG_DIR ?= $(RAW_LOGS_DIR)/valgrind_tree
VALGRIND_RUN_SECONDS ?= 20
VALGRIND_BASE_FLAGS ?= --leak-check=full --show-leak-kinds=all --track-origins=yes --trace-children=yes --child-silent-after-fork=no --time-stamp=yes
VALGRIND_EXTRA_FLAGS ?=
VALGRIND_FOREGROUND_ENV ?= SUNSPOTS_FOREGROUND=1
VALGRIND_FOREGROUND_ARGS ?= --foreground
ASAN_LOG_DIR ?= $(RAW_LOGS_DIR)/asan
ASAN_LOG_PREFIX ?= $(ASAN_LOG_DIR)/asan
UBSAN_LOG_PREFIX ?= $(ASAN_LOG_DIR)/ubsan
ASAN_DEFAULT_OPTIONS ?= detect_leaks=1:abort_on_error=1:fast_unwind_on_malloc=0
UBSAN_DEFAULT_OPTIONS ?= print_stacktrace=1:halt_on_error=1
WARNINGS_DIR ?= $(RAW_LOGS_DIR)
WARNINGS_REPORT ?= $(LOG_ROOT)/warnings_report.txt
LIZARD_REPORT ?= $(LOG_ROOT)/lizard_report.txt
WARNINGS_RAW_LOG ?= $(RAW_LOGS_DIR)/warnings_raw.log
LIZARD_RAW_LOG ?= $(RAW_LOGS_DIR)/lizard_raw.log
ALL_LOCK_FILE ?= $(RAW_LOGS_DIR)/.make_all.lock
WARNINGS_LOCK_FILE ?= $(RAW_LOGS_DIR)/.make_warnings.lock
WARNINGS_SKIP_PREFLIGHT ?= 0
ALLOW_DURING_ALL ?= 0

CMAKE_FLAGS_DEBUG := -G "$(GENERATOR)" -S . -B "$(BUILD_DIR)" \
	-DCMAKE_BUILD_TYPE=$(CONFIG) \
	-DSUNSPOTS_ENABLE_SANITIZERS=ON \
	-DBUILD_TESTING=ON \
	-DSUNSPOTS_BUILD_BENCHMARKS=ON

CMAKE_FLAGS_VALGRIND := -G "$(GENERATOR)" -S . -B "$(VALGRIND_BUILD_DIR)" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DSUNSPOTS_ENABLE_SANITIZERS=OFF \
	-DBUILD_TESTING=ON \
	-DSUNSPOTS_BUILD_BENCHMARKS=ON

CMAKE_FLAGS_TIDY := -G "$(GENERATOR)" -S . -B "$(TIDY_BUILD_DIR)" \
	-DCMAKE_BUILD_TYPE=Debug \
	-DSUNSPOTS_ENABLE_SANITIZERS=OFF \
	-DSUNSPOTS_ENABLE_CLANG_TIDY=ON \
	-DBUILD_TESTING=ON \
	-DSUNSPOTS_BUILD_BENCHMARKS=ON

ifeq ($(NO_COLOR),1)
C_RESET :=
C_BOLD :=
C_CYAN :=
C_GREEN :=
C_YELLOW :=
C_RED :=
else
C_RESET := \033[0m
C_BOLD := \033[1m
C_CYAN := \033[36m
C_GREEN := \033[32m
C_YELLOW := \033[33m
C_RED := \033[31m
endif

.PHONY: help all build build-valgrind build-tests build-tests-valgrind run run-valgrind run-tests run-tests-valgrind list-modules list-modules-valgrind tidy cppcheck lizard warnings e2e e2e-valgrind clean deepclean

help:
	@printf "\nSunspots Make Targets\n\n"
	@printf "  %-26s %s\n" "make (default)" "Same as make build"
	@printf "  %-26s %s\n" "make all" "Serialized full pipeline (build lanes, tests, reports)"
	@printf "  %-26s %s\n" "make build" "Configure + build debug lane only"
	@printf "  %-26s %s\n" "make build-valgrind" "Configure + build valgrind lane only"
	@printf "  %-26s %s\n" "make build-tests" "Configure + build test binaries (debug lane)"
	@printf "  %-26s %s\n" "make build-tests-valgrind" "Configure + build test binaries (valgrind lane)"
	@printf "  %-26s %s\n" "make list-modules" "Discover runnable module targets in debug lane"
	@printf "  %-26s %s\n" "make list-modules-valgrind" "Discover runnable module targets in valgrind lane"
	@printf "\n"
	@printf "  %-26s %s\n" "make run" "Run daemon (default) or module (with M=...) from debug build"
	@printf "  %-26s %s\n" "make run-valgrind" "Valgrind run on daemon (default) or module (with M=...)"
	@printf "  %-26s %s\n" "make run-tests" "Run tests in existing $(BUILD_DIR) (no rebuild)"
	@printf "  %-26s %s\n" "make run-tests-valgrind" "Run valgrind tests in existing $(VALGRIND_BUILD_DIR) (no rebuild)"
	@printf "\n"
	@printf "  %-26s %s\n" "make tidy" "Configure + build clang-tidy lane only"
	@printf "  %-26s %s\n" "make cppcheck" "Run cppcheck on src/ and tests/"
	@printf "  %-26s %s\n" "make lizard" "Run lizard complexity checks on src/ and tests/"
	@printf "  %-26s %s\n" "make warnings" "Build all lanes + refresh warning/analysis reports"
	@printf "\n"
	@printf "  %-26s %s\n" "make e2e" "Placeholder: not implemented yet"
	@printf "  %-26s %s\n" "make e2e-valgrind" "Placeholder: not implemented yet"
	@printf "\n"
	@printf "  %-26s %s\n" "make clean" "Clean configured build trees"
	@printf "  %-26s %s\n" "make deepclean" "Remove build/log/warning output trees"
	@printf "\nOutput locations\n"
	@printf "  logs root:       logs/make/<current-branch>/\n\n"
	@printf "  module filter:   pass M=<module-or-target>, e.g. M=daemon or M=sunspots_fetch_openmeteo\n"
	@printf "  note: daemon-level valgrind is not reliable until daemon supports foreground mode\n\n"

all:
	@set -e; \
		mkdir -p "$(RAW_LOGS_DIR)"; \
		lock_file="$(ALL_LOCK_FILE)"; \
		if ! ( set -C; : > "$$lock_file" ) 2>/dev/null; then \
			printf "%b[fail]%b make all is already running\n" "$(C_RED)" "$(C_RESET)"; \
			printf "      lock: %s\n" "$$lock_file"; \
			if [ -f "$$lock_file" ]; then \
				sed -n '1,5p' "$$lock_file"; \
			fi; \
			exit 1; \
		fi; \
		{ \
			printf "target=all\n"; \
			printf "pid=%s\n" "$$$$"; \
			printf "started=%s\n" "$$(date -u +"%Y-%m-%d %H:%M:%S UTC")"; \
			printf "cwd=%s\n" "$(CURDIR)"; \
		} > "$$lock_file"; \
		trap 'rm -f "$$lock_file"' EXIT INT TERM; \
		printf "%b[run]%b serialized full pipeline\n" "$(C_CYAN)" "$(C_RESET)"; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 build; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 build-tests; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 build-valgrind; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 build-tests-valgrind; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 run-tests; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 run-tests-valgrind; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 tidy; \
		$(MAKE) --no-print-directory ALLOW_DURING_ALL=1 WARNINGS_SKIP_PREFLIGHT=1 warnings; \
		printf "%b[ok]%b full pipeline complete\n" "$(C_GREEN)" "$(C_RESET)"

build:
	@mkdir -p "$(BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@printf "%b[1/2]%b configure debug build\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CMAKE) $(CMAKE_FLAGS_DEBUG) > "$(DEBUG_CONFIG_LOG)" 2>&1 || { \
		printf "%b[fail]%b configure failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(DEBUG_CONFIG_LOG)"; \
		tail -n 40 "$(DEBUG_CONFIG_LOG)"; \
		exit 1; \
	}
	@requested_modules="$(M)"; \
	if [ -n "$$requested_modules" ] && [ "$$requested_modules" != "all" ]; then \
		targets=$$($(MODULE_TARGET_HELPER) resolve "$(BUILD_DIR)" "$$requested_modules" | tr '\n' ' '); \
		printf "%b[2/2]%b build debug targets (M=%s)\n" "$(C_CYAN)" "$(C_RESET)" "$$requested_modules"; \
		$(CMAKE) --build "$(BUILD_DIR)" --parallel --target $$targets > "$(DEBUG_BUILD_LOG)" 2>&1 || { \
			printf "%b[fail]%b build failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(DEBUG_BUILD_LOG)"; \
			tail -n 40 "$(DEBUG_BUILD_LOG)"; \
			exit 1; \
		}; \
	else \
		printf "%b[2/2]%b build debug targets\n" "$(C_CYAN)" "$(C_RESET)"; \
		$(CMAKE) --build "$(BUILD_DIR)" --parallel > "$(DEBUG_BUILD_LOG)" 2>&1 || { \
			printf "%b[fail]%b build failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(DEBUG_BUILD_LOG)"; \
			tail -n 40 "$(DEBUG_BUILD_LOG)"; \
			exit 1; \
		}; \
	fi
	@grep -E '^[^:]+:[0-9]+(:[0-9]+)?:[[:space:]]warning:' "$(DEBUG_BUILD_LOG)" | sed 's|$(CURDIR)/||g' || true
	@printf "%b[ok]%b build complete\n" "$(C_GREEN)" "$(C_RESET)"
	@printf "      configure log: %s\n" "$(DEBUG_CONFIG_LOG)"
	@printf "      build log:     %s\n" "$(DEBUG_BUILD_LOG)"

build-valgrind:
	@mkdir -p "$(VALGRIND_BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@printf "%b[1/2]%b configure valgrind build (sanitizers OFF)\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CMAKE) $(CMAKE_FLAGS_VALGRIND) > "$(VALGRIND_CONFIG_LOG)" 2>&1 || { \
		printf "%b[fail]%b configure failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_CONFIG_LOG)"; \
		tail -n 40 "$(VALGRIND_CONFIG_LOG)"; \
		exit 1; \
	}
	@requested_modules="$(M)"; \
	if [ -n "$$requested_modules" ] && [ "$$requested_modules" != "all" ]; then \
		targets=$$($(MODULE_TARGET_HELPER) resolve "$(VALGRIND_BUILD_DIR)" "$$requested_modules" | tr '\n' ' '); \
		printf "%b[2/2]%b build valgrind targets (M=%s)\n" "$(C_CYAN)" "$(C_RESET)" "$$requested_modules"; \
		$(CMAKE) --build "$(VALGRIND_BUILD_DIR)" --parallel --target $$targets > "$(VALGRIND_BUILD_LOG)" 2>&1 || { \
			printf "%b[fail]%b build failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_BUILD_LOG)"; \
			tail -n 40 "$(VALGRIND_BUILD_LOG)"; \
			exit 1; \
		}; \
	else \
		printf "%b[2/2]%b build valgrind targets\n" "$(C_CYAN)" "$(C_RESET)"; \
		$(CMAKE) --build "$(VALGRIND_BUILD_DIR)" --parallel > "$(VALGRIND_BUILD_LOG)" 2>&1 || { \
			printf "%b[fail]%b build failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_BUILD_LOG)"; \
			tail -n 40 "$(VALGRIND_BUILD_LOG)"; \
			exit 1; \
		}; \
	fi
	@grep -E '^[^:]+:[0-9]+(:[0-9]+)?:[[:space:]]warning:' "$(VALGRIND_BUILD_LOG)" | sed 's|$(CURDIR)/||g' || true
	@printf "%b[ok]%b valgrind build complete\n" "$(C_GREEN)" "$(C_RESET)"
	@printf "      configure log: %s\n" "$(VALGRIND_CONFIG_LOG)"
	@printf "      build log:     %s\n" "$(VALGRIND_BUILD_LOG)"

run:
	@mkdir -p "$(RAW_LOGS_DIR)" "$(ASAN_LOG_DIR)"
	@module_selector="$(M)"; \
	bin_name="$(DAEMON_BIN)"; \
	if [ -n "$$module_selector" ] && [ "$$module_selector" != "all" ]; then \
		resolved=$$($(MODULE_TARGET_HELPER) resolve "$(BUILD_DIR)" "$$module_selector"); \
		count=$$(printf '%s\n' "$$resolved" | sed '/^$$/d' | wc -l | tr -d ' '); \
		if [ "$$count" -ne 1 ]; then \
			printf "%b[fail]%b run expects exactly one module target. selector '%s' matched %s targets\n" "$(C_RED)" "$(C_RESET)" "$$module_selector" "$$count"; \
			printf "      matched: %s\n" "$$(printf '%s' "$$resolved" | tr '\n' ' ')"; \
			printf "      hint: run 'make list-modules' and pick one target\n"; \
			exit 1; \
		fi; \
		bin_name=$$(printf '%s\n' "$$resolved" | head -n 1); \
	fi; \
	daemon_path=$$(find "$(BUILD_DIR)" -type f -name "$$bin_name" 2>/dev/null | sort | head -n 1); \
	if [ ! -x "$$daemon_path" ]; then \
		printf "%b[fail]%b binary '%s' not found under %s. run: make build M=%s\n" "$(C_RED)" "$(C_RESET)" "$$bin_name" "$(BUILD_DIR)" "$$bin_name"; \
		exit 1; \
	fi; \
	asan_log_prefix="$(CURDIR)/$(ASAN_LOG_PREFIX)"; \
	ubsan_log_prefix="$(CURDIR)/$(UBSAN_LOG_PREFIX)"; \
	asan_opts="$(ASAN_DEFAULT_OPTIONS)"; \
	ubsan_opts="$(UBSAN_DEFAULT_OPTIONS)"; \
	case "$$ASAN_OPTIONS" in *log_path=*) ;; *) asan_opts="$$asan_opts:log_path=$$asan_log_prefix" ;; esac; \
	case "$$UBSAN_OPTIONS" in *log_path=*) ;; *) ubsan_opts="$$ubsan_opts:log_path=$$ubsan_log_prefix" ;; esac; \
	if [ -n "$$ASAN_OPTIONS" ]; then asan_opts="$$ASAN_OPTIONS:$$asan_opts"; fi; \
	if [ -n "$$UBSAN_OPTIONS" ]; then ubsan_opts="$$UBSAN_OPTIONS:$$ubsan_opts"; fi; \
	printf "%b[run]%b %s %s\n" "$(C_CYAN)" "$(C_RESET)" "$$daemon_path" "$(RUN_ARGS)"; \
	printf "      sanitizer logs: %s\n" "$(ASAN_LOG_DIR)"; \
	env ASAN_OPTIONS="$$asan_opts" UBSAN_OPTIONS="$$ubsan_opts" "$$daemon_path" $(RUN_ARGS)

run-valgrind:
	@if ! command -v valgrind >/dev/null 2>&1; then \
		printf "%b[fail]%b valgrind is not installed\n" "$(C_RED)" "$(C_RESET)"; \
		exit 1; \
	fi
	@mkdir -p "$(RAW_LOGS_DIR)"
	@module_selector="$(M)"; \
	bin_name="$(DAEMON_BIN)"; \
	if [ -n "$$module_selector" ] && [ "$$module_selector" != "all" ]; then \
		resolved=$$($(MODULE_TARGET_HELPER) resolve "$(VALGRIND_BUILD_DIR)" "$$module_selector"); \
		count=$$(printf '%s\n' "$$resolved" | sed '/^$$/d' | wc -l | tr -d ' '); \
		if [ "$$count" -ne 1 ]; then \
			printf "%b[fail]%b run-valgrind expects exactly one module target. selector '%s' matched %s targets\n" "$(C_RED)" "$(C_RESET)" "$$module_selector" "$$count"; \
			printf "      matched: %s\n" "$$(printf '%s' "$$resolved" | tr '\n' ' ')"; \
			printf "      hint: run 'make list-modules-valgrind' and pick one target\n"; \
			exit 1; \
		fi; \
		bin_name=$$(printf '%s\n' "$$resolved" | head -n 1); \
	fi; \
	daemon_path=$$(find "$(VALGRIND_BUILD_DIR)" -type f -name "$$bin_name" 2>/dev/null | sort | head -n 1); \
	if [ ! -x "$$daemon_path" ]; then \
		printf "%b[fail]%b binary '%s' not found under %s. run: make build-valgrind M=%s\n" "$(C_RED)" "$(C_RESET)" "$$bin_name" "$(VALGRIND_BUILD_DIR)" "$$bin_name"; \
		exit 1; \
	fi; \
	log_dir_abs="$(CURDIR)/$(VALGRIND_LOG_DIR)"; \
	wrapper_log="$(VALGRIND_WRAPPER_LOG)"; \
	if [ "$$bin_name" != "$(DAEMON_BIN)" ]; then wrapper_log="$(RAW_LOGS_DIR)/run_valgrind_$$bin_name.log"; fi; \
	mkdir -p "$$log_dir_abs"; \
	printf "%b[run]%b valgrind %s (trace children, %ss window)\n" "$(C_CYAN)" "$(C_RESET)" "$$daemon_path" "$(VALGRIND_RUN_SECONDS)"; \
	if [ "$$bin_name" = "$(DAEMON_BIN)" ]; then \
		printf "%b[note]%b daemon currently daemonizes; daemon-level valgrind output is not reliable in background mode\n" "$(C_YELLOW)" "$(C_RESET)"; \
		if strings "$$daemon_path" 2>/dev/null | grep -Eq -- "--foreground|SUNSPOTS_FOREGROUND"; then \
			printf "      mode: foreground capture enabled\n"; \
			timeout --signal=SIGINT "$(VALGRIND_RUN_SECONDS)s" \
				env $(VALGRIND_FOREGROUND_ENV) \
				valgrind \
					$(VALGRIND_BASE_FLAGS) \
					$(VALGRIND_EXTRA_FLAGS) \
					--log-file="$$log_dir_abs/%p.log" \
					"$$daemon_path" $(VALGRIND_FOREGROUND_ARGS) $(RUN_ARGS) > "$$wrapper_log" 2>&1; \
		else \
			printf "      mode: daemon foreground flag not detected, running without foreground args\n"; \
			timeout --signal=SIGINT "$(VALGRIND_RUN_SECONDS)s" \
				valgrind \
					$(VALGRIND_BASE_FLAGS) \
					$(VALGRIND_EXTRA_FLAGS) \
					--log-file="$$log_dir_abs/%p.log" \
					"$$daemon_path" $(RUN_ARGS) > "$$wrapper_log" 2>&1; \
		fi; \
	else \
		printf "      mode: direct module capture\n"; \
		timeout --signal=SIGINT "$(VALGRIND_RUN_SECONDS)s" \
			valgrind \
				$(VALGRIND_BASE_FLAGS) \
				$(VALGRIND_EXTRA_FLAGS) \
				--log-file="$$log_dir_abs/%p.log" \
				"$$daemon_path" $(RUN_ARGS) > "$$wrapper_log" 2>&1; \
	fi; \
	rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
		printf "%b[fail]%b valgrind run failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$$wrapper_log"; \
		tail -n 80 "$$wrapper_log"; \
		exit $$rc; \
	fi; \
	if [ $$rc -eq 124 ]; then \
		printf "%b[ok]%b valgrind capture window elapsed after %ss\n" "$(C_GREEN)" "$(C_RESET)" "$(VALGRIND_RUN_SECONDS)"; \
	else \
		printf "%b[ok]%b valgrind run complete\n" "$(C_GREEN)" "$(C_RESET)"; \
	fi; \
	printf "      log: %s\n" "$$wrapper_log"
	@printf "      per-pid logs: %s\n" "$(VALGRIND_LOG_DIR)"

build-tests:
	@mkdir -p "$(BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@printf "%b[1/2]%b configure debug test build\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CMAKE) $(CMAKE_FLAGS_DEBUG) > "$(DEBUG_CONFIG_LOG)" 2>&1 || { \
		printf "%b[fail]%b configure failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(DEBUG_CONFIG_LOG)"; \
		tail -n 40 "$(DEBUG_CONFIG_LOG)"; \
		exit 1; \
	}
	@requested_modules="$(M)"; \
	if [ -n "$$requested_modules" ] && [ "$$requested_modules" != "all" ]; then \
		targets=$$($(MODULE_TARGET_HELPER) resolve "$(BUILD_DIR)" "$$requested_modules" | awk '/_test$$/' | tr '\n' ' '); \
		printf "%b[2/2]%b build debug test targets (M=%s)\n" "$(C_CYAN)" "$(C_RESET)" "$$requested_modules"; \
	else \
		targets=$$($(MODULE_TARGET_HELPER) list "$(BUILD_DIR)" | awk '/_test$$/' | tr '\n' ' '); \
		printf "%b[2/2]%b build debug test targets\n" "$(C_CYAN)" "$(C_RESET)"; \
	fi; \
	if [ -z "$$targets" ]; then \
		printf "%b[fail]%b no test targets matched M=%s\n" "$(C_RED)" "$(C_RESET)" "$$requested_modules"; \
		printf "      hint: run 'make list-modules' to inspect available module targets\n"; \
		exit 1; \
	fi; \
	$(CMAKE) --build "$(BUILD_DIR)" --parallel --target $$targets > "$(DEBUG_TESTS_BUILD_LOG)" 2>&1 || { \
		printf "%b[fail]%b test build failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(DEBUG_TESTS_BUILD_LOG)"; \
		tail -n 60 "$(DEBUG_TESTS_BUILD_LOG)"; \
		exit 1; \
	}
	@printf "%b[ok]%b debug test build complete\n" "$(C_GREEN)" "$(C_RESET)"
	@printf "      configure log: %s\n" "$(DEBUG_CONFIG_LOG)"
	@printf "      test build log: %s\n" "$(DEBUG_TESTS_BUILD_LOG)"

build-tests-valgrind:
	@mkdir -p "$(VALGRIND_BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@printf "%b[1/2]%b configure valgrind test build\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CMAKE) $(CMAKE_FLAGS_VALGRIND) > "$(VALGRIND_CONFIG_LOG)" 2>&1 || { \
		printf "%b[fail]%b configure failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_CONFIG_LOG)"; \
		tail -n 40 "$(VALGRIND_CONFIG_LOG)"; \
		exit 1; \
	}
	@requested_modules="$(M)"; \
	if [ -n "$$requested_modules" ] && [ "$$requested_modules" != "all" ]; then \
		targets=$$($(MODULE_TARGET_HELPER) resolve "$(VALGRIND_BUILD_DIR)" "$$requested_modules" | awk '/_test$$/' | tr '\n' ' '); \
		printf "%b[2/2]%b build valgrind test targets (M=%s)\n" "$(C_CYAN)" "$(C_RESET)" "$$requested_modules"; \
	else \
		targets=$$($(MODULE_TARGET_HELPER) list "$(VALGRIND_BUILD_DIR)" | awk '/_test$$/' | tr '\n' ' '); \
		printf "%b[2/2]%b build valgrind test targets\n" "$(C_CYAN)" "$(C_RESET)"; \
	fi; \
	if [ -z "$$targets" ]; then \
		printf "%b[fail]%b no valgrind test targets matched M=%s\n" "$(C_RED)" "$(C_RESET)" "$$requested_modules"; \
		printf "      hint: run 'make list-modules-valgrind' to inspect available module targets\n"; \
		exit 1; \
	fi; \
	$(CMAKE) --build "$(VALGRIND_BUILD_DIR)" --parallel --target $$targets > "$(VALGRIND_TESTS_BUILD_LOG)" 2>&1 || { \
		printf "%b[fail]%b test build failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_TESTS_BUILD_LOG)"; \
		tail -n 60 "$(VALGRIND_TESTS_BUILD_LOG)"; \
		exit 1; \
	}
	@printf "%b[ok]%b valgrind test build complete\n" "$(C_GREEN)" "$(C_RESET)"
	@printf "      configure log: %s\n" "$(VALGRIND_CONFIG_LOG)"
	@printf "      test build log: %s\n" "$(VALGRIND_TESTS_BUILD_LOG)"

tidy:
	@mkdir -p "$(TIDY_BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@printf "%b[1/2]%b configure tidy build (clang-tidy ON)\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CMAKE) $(CMAKE_FLAGS_TIDY) > "$(TIDY_CONFIG_LOG)" 2>&1 || { \
		printf "%b[fail]%b tidy configure failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(TIDY_CONFIG_LOG)"; \
		tail -n 40 "$(TIDY_CONFIG_LOG)"; \
		exit 1; \
	}
	@printf "%b[2/2]%b build tidy targets\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CMAKE) --build "$(TIDY_BUILD_DIR)" --parallel > "$(TIDY_BUILD_LOG)" 2>&1 || { \
		printf "%b[fail]%b tidy build failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(TIDY_BUILD_LOG)"; \
		tail -n 60 "$(TIDY_BUILD_LOG)"; \
		exit 1; \
	}
	@grep -E '^[^:]+:[0-9]+(:[0-9]+)?:[[:space:]]warning:' "$(TIDY_BUILD_LOG)" | sed 's|$(CURDIR)/||g' || true
	@printf "%b[ok]%b tidy build complete\n" "$(C_GREEN)" "$(C_RESET)"
	@printf "      configure log: %s\n" "$(TIDY_CONFIG_LOG)"
	@printf "      build log:     %s\n" "$(TIDY_BUILD_LOG)"

cppcheck:
	@mkdir -p "$(dir $(CPPCHECK_LOG))"
	@printf "%b[run]%b cppcheck (src/ tests/)\n" "$(C_CYAN)" "$(C_RESET)"
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=warning,style,performance,portability --inline-suppr --quiet \
			--template='[{file}:{line}]: ({severity}) {message}' \
			src tests 2> "$(CPPCHECK_LOG)" || true; \
		printf "%b[ok]%b cppcheck complete\n" "$(C_GREEN)" "$(C_RESET)"; \
		printf "      log: %s\n" "$(CPPCHECK_LOG)"; \
	else \
		printf "%b[note]%b cppcheck not found; skipping\n" "$(C_YELLOW)" "$(C_RESET)"; \
	fi

lizard:
	@mkdir -p "$(dir $(LIZARD_LOG))"
	@printf "%b[run]%b lizard (src/ tests/)\n" "$(C_CYAN)" "$(C_RESET)"
	@if command -v lizard >/dev/null 2>&1; then \
		lizard -C 10 -L 80 -a 4 src tests > "$(LIZARD_LOG)" 2>&1 || true; \
		printf "%b[ok]%b lizard complete\n" "$(C_GREEN)" "$(C_RESET)"; \
		printf "      log: %s\n" "$(LIZARD_LOG)"; \
	elif python3 -c "import lizard" >/dev/null 2>&1; then \
		python3 -m lizard -C 10 -L 80 -a 4 src tests > "$(LIZARD_LOG)" 2>&1 || true; \
		printf "%b[ok]%b lizard complete\n" "$(C_GREEN)" "$(C_RESET)"; \
		printf "      log: %s\n" "$(LIZARD_LOG)"; \
	else \
		printf "%b[note]%b lizard not found; skipping\n" "$(C_YELLOW)" "$(C_RESET)"; \
	fi

run-tests:
	@mkdir -p "$(BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		printf "%b[fail]%b no configured build found in %s. run: make build or make build-tests\n" "$(C_RED)" "$(C_RESET)" "$(BUILD_DIR)"; \
		exit 1; \
	fi
	@mkdir -p "$(ASAN_LOG_DIR)"
	@printf "%b[run]%b ctest (debug)\n" "$(C_CYAN)" "$(C_RESET)"
	@asan_log_prefix="$(CURDIR)/$(ASAN_LOG_PREFIX)"; \
	ubsan_log_prefix="$(CURDIR)/$(UBSAN_LOG_PREFIX)"; \
	asan_opts="$(ASAN_DEFAULT_OPTIONS)"; \
	ubsan_opts="$(UBSAN_DEFAULT_OPTIONS)"; \
	ctest_selector="$(M)"; \
	ctest_regex=""; \
	if [ -n "$$ctest_selector" ] && [ "$$ctest_selector" != "all" ]; then \
		resolved_tests=$$($(MODULE_TARGET_HELPER) resolve "$(BUILD_DIR)" "$$ctest_selector" 2>/dev/null | awk '/_test$$/' || true); \
		if [ -n "$$resolved_tests" ]; then \
			ctest_regex=$$(printf '%s\n' "$$resolved_tests" | sed 's/_/[._]/g' | paste -sd'|' -); \
		else \
			ctest_regex=$$(printf '%s' "$$ctest_selector" | tr ',;' '|' | tr -d ' '); \
		fi; \
		printf "      test filter: %s\n" "$$ctest_regex"; \
	fi; \
	case "$$ASAN_OPTIONS" in *log_path=*) ;; *) asan_opts="$$asan_opts:log_path=$$asan_log_prefix" ;; esac; \
	case "$$UBSAN_OPTIONS" in *log_path=*) ;; *) ubsan_opts="$$ubsan_opts:log_path=$$ubsan_log_prefix" ;; esac; \
	if [ -n "$$ASAN_OPTIONS" ]; then asan_opts="$$ASAN_OPTIONS:$$asan_opts"; fi; \
	if [ -n "$$UBSAN_OPTIONS" ]; then ubsan_opts="$$UBSAN_OPTIONS:$$ubsan_opts"; fi; \
	set -- --test-dir "$(BUILD_DIR)" --output-on-failure; \
	if [ -n "$$ctest_regex" ]; then set -- "$$@" -R "$$ctest_regex"; fi; \
	env ASAN_OPTIONS="$$asan_opts" UBSAN_OPTIONS="$$ubsan_opts" \
	$(CTEST) "$$@" > "$(DEBUG_TEST_LOG)" 2>&1 || { \
		printf "%b[fail]%b tests failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(DEBUG_TEST_LOG)"; \
		tail -n 60 "$(DEBUG_TEST_LOG)"; \
		exit 1; \
	}
	@printf "%b[ok]%b tests passed\n" "$(C_GREEN)" "$(C_RESET)"
	@printf "      test log:      %s\n" "$(DEBUG_TEST_LOG)"
	@printf "      sanitizer logs: %s\n" "$(ASAN_LOG_DIR)"

run-tests-valgrind:
	@mkdir -p "$(VALGRIND_BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@if [ ! -f "$(VALGRIND_BUILD_DIR)/CMakeCache.txt" ]; then \
		printf "%b[fail]%b no configured valgrind build found in %s. run: make build-valgrind or make build-tests-valgrind\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_BUILD_DIR)"; \
		exit 1; \
	fi
	@printf "%b[run]%b ctest valgrind lane\n" "$(C_CYAN)" "$(C_RESET)"
	@ctest_selector="$(M)"; \
	ctest_regex=""; \
	if [ -n "$$ctest_selector" ] && [ "$$ctest_selector" != "all" ]; then \
		resolved_tests=$$($(MODULE_TARGET_HELPER) resolve "$(VALGRIND_BUILD_DIR)" "$$ctest_selector" 2>/dev/null | awk '/_test$$/' || true); \
		if [ -n "$$resolved_tests" ]; then \
			ctest_regex=$$(printf '%s\n' "$$resolved_tests" | sed 's/_/[._]/g' | paste -sd'|' -); \
		else \
			ctest_regex=$$(printf '%s' "$$ctest_selector" | tr ',;' '|' | tr -d ' '); \
		fi; \
		printf "      test filter: %s\n" "$$ctest_regex"; \
	fi; \
	set -- --test-dir "$(VALGRIND_BUILD_DIR)" --output-on-failure -L valgrind; \
	if [ -n "$$ctest_regex" ]; then set -- "$$@" -R "$$ctest_regex"; fi; \
	$(CTEST) "$$@" > "$(VALGRIND_TEST_LOG)" 2>&1 || { \
		printf "%b[fail]%b valgrind tests failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_TEST_LOG)"; \
		tail -n 80 "$(VALGRIND_TEST_LOG)"; \
		exit 1; \
	}
	@printf "%b[ok]%b valgrind tests passed\n" "$(C_GREEN)" "$(C_RESET)"
	@printf "      valgrind test log: %s\n" "$(VALGRIND_TEST_LOG)"

list-modules:
	@mkdir -p "$(BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		printf "%b[run]%b configure debug build for module discovery\n" "$(C_CYAN)" "$(C_RESET)"; \
		$(CMAKE) $(CMAKE_FLAGS_DEBUG) > "$(DEBUG_CONFIG_LOG)" 2>&1 || { \
			printf "%b[fail]%b configure failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(DEBUG_CONFIG_LOG)"; \
			tail -n 40 "$(DEBUG_CONFIG_LOG)"; \
			exit 1; \
		}; \
	fi
	@printf "%b[run]%b discover runnable module targets (debug lane)\n" "$(C_CYAN)" "$(C_RESET)"
	@$(MODULE_TARGET_HELPER) list-aliases "$(BUILD_DIR)" | \
		awk -F'\t' '{ printf "  %-30s alias=%s\n", $$1, $$2 }'

list-modules-valgrind:
	@mkdir -p "$(VALGRIND_BUILD_DIR)" "$(RAW_LOGS_DIR)"
	@if [ ! -f "$(VALGRIND_BUILD_DIR)/CMakeCache.txt" ]; then \
		printf "%b[run]%b configure valgrind build for module discovery\n" "$(C_CYAN)" "$(C_RESET)"; \
		$(CMAKE) $(CMAKE_FLAGS_VALGRIND) > "$(VALGRIND_CONFIG_LOG)" 2>&1 || { \
			printf "%b[fail]%b configure failed. log: %s\n" "$(C_RED)" "$(C_RESET)" "$(VALGRIND_CONFIG_LOG)"; \
			tail -n 40 "$(VALGRIND_CONFIG_LOG)"; \
			exit 1; \
		}; \
	fi
	@printf "%b[run]%b discover runnable module targets (valgrind lane)\n" "$(C_CYAN)" "$(C_RESET)"
	@$(MODULE_TARGET_HELPER) list-aliases "$(VALGRIND_BUILD_DIR)" | \
		awk -F'\t' '{ printf "  %-30s alias=%s\n", $$1, $$2 }'

warnings:
	@set -e; \
		mkdir -p "$(RAW_LOGS_DIR)"; \
		lock_file="$(WARNINGS_LOCK_FILE)"; \
		if ! ( set -C; : > "$$lock_file" ) 2>/dev/null; then \
			printf "%b[fail]%b warnings target already running\n" "$(C_RED)" "$(C_RESET)"; \
			printf "      lock: %s\n" "$$lock_file"; \
			if [ -f "$$lock_file" ]; then \
				sed -n '1,5p' "$$lock_file"; \
			fi; \
			exit 1; \
		fi; \
		{ \
			printf "target=warnings\n"; \
			printf "pid=%s\n" "$$$$"; \
			printf "started=%s\n" "$$(date -u +"%Y-%m-%d %H:%M:%S UTC")"; \
			printf "cwd=%s\n" "$(CURDIR)"; \
		} > "$$lock_file"; \
		trap 'rm -f "$$lock_file" "$(WARNINGS_DIR)/.warnings.tmp" "$(WARNINGS_DIR)/.warnings.resolved.tmp" "$(WARNINGS_DIR)/.basename_map.tmp" "$(WARNINGS_DIR)/.warnings.parsed.tmp" "$(WARNINGS_DIR)/.warnings.unique.tmp" "$(WARNINGS_DIR)/.warnings.sorted.tmp" "$(WARNINGS_DIR)/.lizard.parsed.tmp" "$(WARNINGS_DIR)/.lizard.sorted.tmp"' EXIT INT TERM; \
		if [ "$(WARNINGS_SKIP_PREFLIGHT)" != "1" ]; then \
			printf "%b[run]%b warnings preflight lanes (build, build-valgrind, tidy)\n" "$(C_CYAN)" "$(C_RESET)"; \
			$(MAKE) --no-print-directory build || true; \
			$(MAKE) --no-print-directory build-valgrind || true; \
			$(MAKE) --no-print-directory tidy || true; \
		else \
			printf "%b[run]%b warnings preflight skipped (using existing lane logs)\n" "$(C_CYAN)" "$(C_RESET)"; \
		fi; \
		mkdir -p "$(WARNINGS_DIR)"; \
		mkdir -p "$(RAW_LOGS_DIR)"; \
		mkdir -p "$(dir $(CPPCHECK_LOG))"; \
		mkdir -p "$(dir $(LIZARD_LOG))"; \
		rm -rf "warnings/$(GIT_BRANCH)"; \
		rm -f "$(WARNINGS_RAW_LOG)" "$(LIZARD_RAW_LOG)"; \
		rm -f "$(WARNINGS_REPORT)" "$(LIZARD_REPORT)" "$(WARNINGS_DIR)/.warnings.tmp" \
			"$(WARNINGS_DIR)/all_warnings.log" "$(WARNINGS_DIR)/summary.tsv" "$(WARNINGS_DIR)/total.count" "$(WARNINGS_DIR)/warnings_report.html" "$(WARNINGS_DIR)"/*.warn.log; \
				run_ts=$$(date -u +"%Y-%m-%d %H:%M:%S UTC"); \
				source_compiler_run="no"; \
				source_clangtidy_run="no"; \
				source_cppcheck_run="no"; \
				source_sanitizer_run="no"; \
				source_valgrind_run="no"; \
					tool_cppcheck="not installed"; \
					tool_lizard="not installed"; \
					tool_clangtidy="not installed"; \
					tool_valgrind="not installed"; \
				lizard_cmd=""; \
				if command -v cppcheck >/dev/null 2>&1; then tool_cppcheck="installed"; fi; \
				if command -v clang-tidy >/dev/null 2>&1; then tool_clangtidy="installed"; fi; \
				if command -v valgrind >/dev/null 2>&1; then tool_valgrind="installed"; fi; \
				if command -v lizard >/dev/null 2>&1; then \
					tool_lizard="installed"; \
					lizard_cmd="lizard"; \
				elif python3 -c "import lizard" >/dev/null 2>&1; then \
					tool_lizard="installed"; \
					lizard_cmd="python3 -m lizard"; \
				fi; \
				if command -v cppcheck >/dev/null 2>&1; then \
					cppcheck --enable=warning,style,performance,portability --inline-suppr --quiet \
						--template='[{file}:{line}]: ({severity}) {message}' \
						src tests 2> "$(CPPCHECK_LOG)" || true; \
					source_cppcheck_run="yes"; \
				fi; \
				if [ "$$tool_lizard" = "installed" ]; then \
					$$lizard_cmd -C 10 -L 80 -a 4 src tests > "$(LIZARD_LOG)" 2>&1 || true; \
					awk '\
						/^[[:space:]]*[0-9]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+/ { \
							nloc=$$1; ccn=$$2; token=$$3; param=$$4; len=$$5; loc=$$6; \
							split(loc, a, "@"); \
							func=a[1]; range=a[2]; file=a[3]; \
							if (file !~ /^(src|tests)\//) next; \
							if (file ~ /(^|\/)libs\//) next; \
							module="other"; \
							if (file ~ /^src\//) { split(file, p, "/"); module=p[2]; } \
							else if (file ~ /^tests\//) { module="tests"; } \
							ccn_hit=(ccn>10); len_hit=(len>80); param_hit=(param>4); \
							if (!(ccn_hit || len_hit || param_hit)) next; \
							reasons=""; \
							if (ccn_hit) reasons=reasons "CCN "; \
							if (len_hit) reasons=reasons "LEN "; \
							if (param_hit) reasons=reasons "PAR "; \
							sub(/[[:space:]]+$$/, "", reasons); \
							printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", module, file, func, range, nloc, ccn, token, param, len, reasons; \
						}' "$(LIZARD_LOG)" | sort -u > "$(WARNINGS_DIR)/.lizard.parsed.tmp"; \
					sort -t "$$(printf '\t')" -k1,1 -k2,2 -k6,6nr -k9,9nr -k8,8nr \
						"$(WARNINGS_DIR)/.lizard.parsed.tmp" > "$(WARNINGS_DIR)/.lizard.sorted.tmp"; \
					{ \
						printf "+--------------------------------------------------------------------------------+\n"; \
						printf "|                            Sunspots Lizard Report                             |\n"; \
						printf "+--------------------------------------------------------------------------------+\n"; \
						printf "Branch       : %s\n" "$(GIT_BRANCH)"; \
						printf "Generated at : %s\n" "$$run_ts"; \
						printf "Thresholds   : CCN>10, Length>80, Params>4\n"; \
						printf "Scope        : src/ and tests/ (vendor libs excluded)\n"; \
						printf "Total        : %s flagged functions\n" "$$(wc -l < "$(WARNINGS_DIR)/.lizard.sorted.tmp" | tr -d ' ')"; \
						printf "+--------------------------------------------------------------------------------+\n\n"; \
						if [ ! -s "$(WARNINGS_DIR)/.lizard.sorted.tmp" ]; then \
							printf "No lizard threshold breaches found.\n"; \
						else \
							printf "Flagged by module\n"; \
							for module in $$(cut -f1 "$(WARNINGS_DIR)/.lizard.sorted.tmp" | sort | uniq); do \
								mcount=$$(awk -F'\t' -v m="$$module" '$$1==m { c++ } END { print c+0 }' "$(WARNINGS_DIR)/.lizard.sorted.tmp"); \
								printf "  - %-12s %4s\n" "$$module" "$$mcount"; \
							done; \
							printf "\nDetailed findings\n"; \
							printf "+--------------------------------------------------------------------------------+\n"; \
							for module in $$(cut -f1 "$(WARNINGS_DIR)/.lizard.sorted.tmp" | sort | uniq); do \
								mcount=$$(awk -F'\t' -v m="$$module" '$$1==m { c++ } END { print c+0 }' "$(WARNINGS_DIR)/.lizard.sorted.tmp"); \
								printf "\n[%s] (%s)\n" "$$module" "$$mcount"; \
								for file in $$(awk -F'\t' -v m="$$module" '$$1==m { print $$2 }' "$(WARNINGS_DIR)/.lizard.sorted.tmp" | sort | uniq); do \
									fcount=$$(awk -F'\t' -v m="$$module" -v f="$$file" '$$1==m && $$2==f { c++ } END { print c+0 }' "$(WARNINGS_DIR)/.lizard.sorted.tmp"); \
									printf "  %s (%s)\n" "$$file" "$$fcount"; \
									printf "    %-30s %-9s %-6s %-6s %-5s %s\n" "Function" "Range" "CCN" "LEN" "PAR" "Flags"; \
									awk -F'\t' -v m="$$module" -v f="$$file" '\
										$$1==m && $$2==f { \
											func=$$3; \
											if (length(func) > 30) func=substr(func,1,27) "..."; \
											printf "    %-30s %-9s %-6s %-6s %-5s %s\n", func, $$4, $$6, $$9, $$8, $$10; \
										}' "$(WARNINGS_DIR)/.lizard.sorted.tmp"; \
									printf "\n"; \
								done; \
								printf "+--------------------------------------------------------------------------------+\n"; \
							done; \
						fi; \
					} > "$(LIZARD_REPORT)"; \
				else \
					{ \
						printf "+--------------------------------------------------------------------------------+\n"; \
						printf "|                            Sunspots Lizard Report                             |\n"; \
						printf "+--------------------------------------------------------------------------------+\n"; \
						printf "Branch       : %s\n" "$(GIT_BRANCH)"; \
						printf "Generated at : %s\n" "$$run_ts"; \
						printf "Result       : lizard not installed\n"; \
						printf "Install      : python3 -m pip install --user lizard\n"; \
						printf "+--------------------------------------------------------------------------------+\n"; \
					} > "$(LIZARD_REPORT)"; \
				fi; \
				if [ -f "$(DEBUG_BUILD_LOG)" ]; then \
					source_compiler_run="yes"; \
					grep -E '^[^:]+:[0-9]+(:[0-9]+)?:[[:space:]]warning:' "$(DEBUG_BUILD_LOG)" | \
						sed 's|$(CURDIR)/||g' | awk '{ print "compiler\tdebug\t" $$0 }' >> "$(WARNINGS_DIR)/.warnings.tmp" || true; \
				fi; \
				if [ -f "$(VALGRIND_BUILD_LOG)" ]; then \
					source_compiler_run="yes"; \
					grep -E '^[^:]+:[0-9]+(:[0-9]+)?:[[:space:]]warning:' "$(VALGRIND_BUILD_LOG)" | \
						sed 's|$(CURDIR)/||g' | awk '{ print "compiler\tvalgrind\t" $$0 }' >> "$(WARNINGS_DIR)/.warnings.tmp" || true; \
				fi; \
				if [ -f "$(TIDY_BUILD_LOG)" ]; then \
					source_clangtidy_run="yes"; \
					grep -E '^[^:]+:[0-9]+(:[0-9]+)?:[[:space:]]warning:' "$(TIDY_BUILD_LOG)" | \
						sed 's|$(CURDIR)/||g' | awk '\
							{ \
								source="clangtidy"; \
								if ($$0 ~ /\[-W[^]]*\]/) { source="compiler"; } \
								print source "\tdebug\t" $$0; \
							}' >> "$(WARNINGS_DIR)/.warnings.tmp" || true; \
				fi; \
				if [ -f "$(DEBUG_TEST_LOG)" ]; then \
					source_sanitizer_run="yes"; \
					grep -E '^[^:]+:[0-9]+:[0-9]+:[[:space:]]runtime error:' "$(DEBUG_TEST_LOG)" | \
						sed 's|$(CURDIR)/||g; s/[[:space:]]runtime error:/: warning:/g' | \
						awk '{ print "sanitizer\tdebug\t" $$0 }' >> "$(WARNINGS_DIR)/.warnings.tmp" || true; \
				fi; \
				if [ -f "$(VALGRIND_TEST_LOG)" ]; then \
					source_valgrind_run="yes"; \
					awk -v curdir="$(CURDIR)/" -v vglog="$(VALGRIND_TEST_LOG)" '\
						BEGIN { msg=""; pending=0; found=0; err_count=0; } \
						/^==[0-9]+== / { \
							line=$$0; \
							sub(/^==[0-9]+== /, "", line); \
							if (line ~ /^(Invalid read|Invalid write|Use of uninitialised value|Conditional jump or move depends on uninitialised value|Jump to the invalid address|Mismatched free|Syscall param|Source and destination overlap|Uninitialised value)/) { \
								msg=line; \
								pending=1; \
							} \
							if (line ~ /^ERROR SUMMARY:[[:space:]]*[1-9][0-9]*/) { \
								err_count=1; \
							} \
						} \
						{ \
							if (pending && $$0 ~ /\([^()]+:[0-9]+\)/) { \
								frame=$$0; \
								sub(/^.*\(/, "", frame); \
								sub(/\).*$$/, "", frame); \
								split(frame, p, ":"); \
								file=p[1]; \
								line_no=p[2]; \
								gsub(curdir, "", file); \
								if (file !~ /^\/usr\// && file !~ /^\/lib\// && file !~ /googletest|gtest|sqlite3/) { \
									printf "valgrind\tvalgrind\t%s:%s: warning: %s\n", file, line_no, msg; \
									pending=0; \
									found=1; \
								} \
							} \
						} \
						END { \
							if (!found && err_count) { \
								print "valgrind\tvalgrind\t" vglog ":1: warning: valgrind reported errors (see valgrind_test.log)"; \
							} \
						}' "$(VALGRIND_TEST_LOG)" >> "$(WARNINGS_DIR)/.warnings.tmp" || true; \
				fi; \
				if [ -f "$(CPPCHECK_LOG)" ]; then \
					sed -n 's/^\[\([^]:]*\):\([0-9][0-9]*\)\]:[[:space:]]*(\([^)]*\))[[:space:]]*\(.*\)$$/cppcheck\tdebug\t\1:\2: warning: (\3) \4/p' "$(CPPCHECK_LOG)" >> "$(WARNINGS_DIR)/.warnings.tmp" || true; \
				fi; \
				if [ -f "$(WARNINGS_DIR)/.warnings.tmp" ]; then \
					find src tests -type f | awk -F'/' '\
						{ b=$$NF; n[b]++; if (!(b in p)) p[b]=$$0; } \
						END { for (k in n) if (n[k]==1) print k "\t" p[k]; }' > "$(WARNINGS_DIR)/.basename_map.tmp"; \
					awk -F'\t' 'BEGIN { OFS=FS; } \
						NR==FNR { map[$$1]=$$2; next; } \
						{ \
							n=split($$3, a, ":"); \
							file=a[1]; \
							if (file !~ /\// && (file in map)) { \
								a[1]=map[file]; \
								rebuilt=a[1]; \
								for (i=2; i<=n; i++) rebuilt=rebuilt ":" a[i]; \
								$$3=rebuilt; \
							} \
							print $$1, $$2, $$3; \
						}' "$(WARNINGS_DIR)/.basename_map.tmp" "$(WARNINGS_DIR)/.warnings.tmp" > "$(WARNINGS_DIR)/.warnings.resolved.tmp"; \
					awk -F'\t' '((index($$3, "src/") == 1 || index($$3, "tests/") == 1) && $$3 !~ /(^|\/)libs\//)' "$(WARNINGS_DIR)/.warnings.resolved.tmp" > "$(WARNINGS_DIR)/.warnings.filtered.tmp"; \
					mv "$(WARNINGS_DIR)/.warnings.filtered.tmp" "$(WARNINGS_DIR)/.warnings.tmp"; \
					sort -u "$(WARNINGS_DIR)/.warnings.tmp" -o "$(WARNINGS_DIR)/.warnings.tmp"; \
				fi; \
			if [ ! -f "$(WARNINGS_DIR)/.warnings.tmp" ] || [ ! -s "$(WARNINGS_DIR)/.warnings.tmp" ]; then \
				inner=80; \
				line="+$$(printf '%*s' $$inner '' | tr ' ' '-')+"; \
				title="Sunspots Warning Report"; \
				pad=$$(( (inner - $${#title}) / 2 )); \
				rpad=$$(( inner - $${#title} - pad )); \
					{ \
						printf "%s\n" "$$line"; \
						printf "|%*s%s%*s|\n" "$$pad" "" "$$title" "$$rpad" ""; \
						printf "%s\n" "$$line"; \
						printf "Branch       : %s\n" "$(GIT_BRANCH)"; \
						printf "Generated at : %s\n" "$$run_ts"; \
						printf "Tools        : clang-tidy=%s cppcheck=%s lizard=%s valgrind=%s\n" "$$tool_clangtidy" "$$tool_cppcheck" "$$tool_lizard" "$$tool_valgrind"; \
						printf "Result       : no actionable warnings captured\n"; \
						printf "%s\n" "$$line"; \
					} > "$(WARNINGS_REPORT)"; \
					printf "%b[ok]%b no actionable warnings captured\n" "$(C_GREEN)" "$(C_RESET)"; \
					printf "      report dir: %s\n" "$(LOG_ROOT)"; \
					printf "      lizard:     %s\n" "$(LIZARD_REPORT)"; \
					printf "      tools: clang-tidy=%s cppcheck=%s lizard=%s valgrind=%s\n" "$$tool_clangtidy" "$$tool_cppcheck" "$$tool_lizard" "$$tool_valgrind"; \
				else \
				total=$$(wc -l < "$(WARNINGS_DIR)/.warnings.tmp" | tr -d ' '); \
					awk -F'\t' '\
					{ \
						source=$$1; \
						config=$$2; \
						n=split($$3, a, ":"); \
						file=a[1]; \
						line=a[2]; \
						idx=3; \
				col="-"; \
				if (a[3] ~ /^[0-9]+$$/) { \
					col=a[3]; \
					idx=4; \
				} \
				rest=a[idx]; \
				for (i=idx+1; i<=n; i++) { \
					rest=rest ":" a[i]; \
				} \
				sub(/^[[:space:]]*warning:[[:space:]]*/, "", rest); \
				module="other"; \
				if (file ~ /^src\//) { \
					split(file, p, "/"); \
					module=p[2]; \
				} else if (file ~ /^tests\//) { \
					module="tests"; \
				} else if (file ~ /^\//) { \
					module="external"; \
				} \
						printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", source, config, module, file, line, col, rest; \
					}' "$(WARNINGS_DIR)/.warnings.tmp" > "$(WARNINGS_DIR)/.warnings.parsed.tmp"; \
					awk -F'\t' '\
						{ \
							key=$$4 ":" $$5 ":" $$7; \
							rank=1; \
							if ($$1=="compiler" && $$2=="valgrind") { rank=2; } \
							if ($$1=="clangtidy") { rank=3; } \
							if ($$1=="sanitizer") { rank=4; } \
							if ($$1=="valgrind") { rank=5; } \
							if (!(key in best_rank) || rank > best_rank[key]) { \
								best_rank[key]=rank; \
								best[key]=$$0; \
						} \
					} \
						END { \
							for (k in best) print best[k]; \
						}' "$(WARNINGS_DIR)/.warnings.parsed.tmp" > "$(WARNINGS_DIR)/.warnings.unique.tmp"; \
						src_known_max=$$(printf "%s\n" compiler clangtidy cppcheck sanitizer valgrind | \
							awk 'length($$0)>m{m=length($$0)} END{print m+0}'); \
						sort -t "$$(printf '\t')" -k3,3 -k4,4 -k1,1 -k5,5n -k6,6n \
							"$(WARNINGS_DIR)/.warnings.unique.tmp" > "$(WARNINGS_DIR)/.warnings.sorted.tmp"; \
						mv "$(WARNINGS_DIR)/.warnings.sorted.tmp" "$(WARNINGS_DIR)/.warnings.unique.tmp"; \
						src_max=$$(awk -F'\t' 'length($$1)>m{m=length($$1)} END{print m+0}' "$(WARNINGS_DIR)/.warnings.unique.tmp"); \
						if [ $$src_known_max -gt $$src_max ]; then src_max=$$src_known_max; fi; \
					mod_max=$$(awk -F'\t' 'length($$3)>m{m=length($$3)} END{print m+0}' "$(WARNINGS_DIR)/.warnings.unique.tmp"); \
					bracket_label_max=$$src_max; \
					if [ $$mod_max -gt $$bracket_label_max ]; then bracket_label_max=$$mod_max; fi; \
					bracket_inner_width=$$((bracket_label_max + 2)); \
					inner=80; \
				line="+$$(printf '%*s' $$inner '' | tr ' ' '-')+"; \
				title="Sunspots Warning Report"; \
				pad=$$(( (inner - $${#title}) / 2 )); \
				rpad=$$(( inner - $${#title} - pad )); \
				{ \
					printf "%s\n" "$$line"; \
					printf "|%*s%s%*s|\n" "$$pad" "" "$$title" "$$rpad" ""; \
					printf "%s\n" "$$line"; \
					printf "Branch       : %s\n" "$(GIT_BRANCH)"; \
					printf "Generated at : %s\n" "$$run_ts"; \
					printf "Tools        : clang-tidy=%s cppcheck=%s lizard=%s valgrind=%s\n" "$$tool_clangtidy" "$$tool_cppcheck" "$$tool_lizard" "$$tool_valgrind"; \
					printf "Total        : %s warnings\n" "$$(wc -l < "$(WARNINGS_DIR)/.warnings.unique.tmp" | tr -d ' ')"; \
					printf "%s\n\n" "$$line"; \
					printf "Warnings by source\n"; \
					for source in compiler clangtidy cppcheck sanitizer valgrind; do \
						scount=$$(awk -F'\t' -v s="$$source" '$$1==s { c++ } END { print c+0 }' "$(WARNINGS_DIR)/.warnings.unique.tmp"); \
						slen=$${#source}; \
						sleft=$$(( (bracket_inner_width - slen) / 2 )); \
						sright=$$(( bracket_inner_width - slen - sleft )); \
						run_state="not run"; \
						case "$$source" in \
							compiler) if [ "$$source_compiler_run" = "yes" ]; then run_state="$$scount"; fi ;; \
							clangtidy) if [ "$$source_clangtidy_run" = "yes" ]; then run_state="$$scount"; fi ;; \
							cppcheck) if [ "$$source_cppcheck_run" = "yes" ]; then run_state="$$scount"; fi ;; \
							sanitizer) if [ "$$source_sanitizer_run" = "yes" ]; then run_state="$$scount"; fi ;; \
							valgrind) if [ "$$source_valgrind_run" = "yes" ]; then run_state="$$scount"; fi ;; \
						esac; \
						printf "  [%*s%s%*s] %s\n" "$$sleft" "" "$$source" "$$sright" "" "$$run_state"; \
					done; \
					printf "\nWarnings by module\n"; \
					for module in $$(cut -f3 "$(WARNINGS_DIR)/.warnings.unique.tmp" | sort | uniq); do \
						mcount=$$(awk -F'\t' -v m="$$module" '$$3==m { c++ } END { print c+0 }' "$(WARNINGS_DIR)/.warnings.unique.tmp"); \
						mlen=$${#module}; \
						mleft=$$(( (bracket_inner_width - mlen) / 2 )); \
						mright=$$(( bracket_inner_width - mlen - mleft )); \
						printf "  [%*s%s%*s] %5s\n" "$$mleft" "" "$$module" "$$mright" "" "$$mcount"; \
					done; \
					printf "\n%s\n" "$$line"; \
					printf "Detailed Warnings By Module And File\n"; \
					printf "%s\n" "$$line"; \
					for module in $$(cut -f3 "$(WARNINGS_DIR)/.warnings.unique.tmp" | sort | uniq); do \
						mcount=$$(awk -F'\t' -v m="$$module" '$$3==m { c++ } END { print c+0 }' "$(WARNINGS_DIR)/.warnings.unique.tmp"); \
						mlen=$${#module}; \
						mleft=$$(( (bracket_inner_width - mlen) / 2 )); \
						mright=$$(( bracket_inner_width - mlen - mleft )); \
						printf "\n[%*s%s%*s] (%s warnings)\n" "$$mleft" "" "$$module" "$$mright" "" "$$mcount"; \
						for file in $$(awk -F'\t' -v m="$$module" '$$3==m { print $$4 }' "$(WARNINGS_DIR)/.warnings.unique.tmp" | sort | uniq); do \
							fcount=$$(awk -F'\t' -v m="$$module" -v f="$$file" '$$3==m && $$4==f { c++ } END { print c+0 }' "$(WARNINGS_DIR)/.warnings.unique.tmp"); \
							printf "  %s (%s)\n" "$$file" "$$fcount"; \
								awk -F'\t' -v m="$$module" -v f="$$file" '\
									$$3==m && $$4==f { printf "%s\t%s\t%s\t%s\n", $$1, $$5, $$6, $$7 }' "$(WARNINGS_DIR)/.warnings.unique.tmp" | \
								sort -k1,1 -k2,2n -k3,3n | \
								awk -F'\t' -v w="$$bracket_inner_width" '\
									{ \
										label=$$1; \
										len=length(label); \
										left=int((w-len)/2); \
										right=w-len-left; \
										printf "    - [%*s%s%*s] L%s:C%s %s\n", left, "", label, right, "", $$2, $$3, $$4; \
									}'; \
							printf "\n"; \
						done; \
						printf "%s\n" "$$line"; \
					done; \
					} > "$(WARNINGS_REPORT)"; \
					printf "%b[ok]%b warning reports generated\n" "$(C_GREEN)" "$(C_RESET)"; \
					printf "      total warnings: %s\n" "$$(wc -l < "$(WARNINGS_DIR)/.warnings.unique.tmp" | tr -d ' ')"; \
					printf "      report:         %s\n" "$(WARNINGS_REPORT)"; \
					printf "      lizard:         %s\n" "$(LIZARD_REPORT)"; \
					printf "      tools:          clang-tidy=%s cppcheck=%s lizard=%s valgrind=%s\n" "$$tool_clangtidy" "$$tool_cppcheck" "$$tool_lizard" "$$tool_valgrind"; \
			fi
			@if [ -f "$(LIZARD_LOG)" ]; then \
				cp "$(LIZARD_LOG)" "$(LIZARD_RAW_LOG)"; \
			else \
				printf "lizard log not found\n" > "$(LIZARD_RAW_LOG)"; \
			fi
			@{ \
				printf "Sunspots warnings raw bundle\n"; \
				printf "Branch: %s\n" "$(GIT_BRANCH)"; \
				printf "Generated at: %s\n\n" "$$(date -u +"%Y-%m-%d %H:%M:%S UTC")"; \
				for f in "$(DEBUG_CONFIG_LOG)" "$(DEBUG_BUILD_LOG)" "$(DEBUG_TEST_LOG)" "$(VALGRIND_CONFIG_LOG)" "$(VALGRIND_BUILD_LOG)" "$(VALGRIND_TEST_LOG)" "$(TIDY_CONFIG_LOG)" "$(TIDY_BUILD_LOG)" "$(CPPCHECK_LOG)" "$(LIZARD_LOG)"; do \
					if [ -f "$$f" ]; then \
						printf "===== BEGIN %s =====\n" "$$f"; \
						cat "$$f"; \
						printf "\n===== END %s =====\n\n" "$$f"; \
					fi; \
				done; \
			} > "$(WARNINGS_RAW_LOG)"
			@printf "      warnings raw:   %s\n" "$(WARNINGS_RAW_LOG)"
			@printf "      lizard raw:     %s\n" "$(LIZARD_RAW_LOG)"
			@true

e2e:
	@printf "%b[note]%b e2e target is not yet implemented\n" "$(C_YELLOW)" "$(C_RESET)"
	@printf "      planned: full system scenario runner\n"
	@exit 2

e2e-valgrind:
	@printf "%b[note]%b e2e-valgrind target is not yet implemented\n" "$(C_YELLOW)" "$(C_RESET)"
	@printf "      planned: full system valgrind run with child tracing\n"
	@exit 2

clean:
	@for d in "$(BUILD_DIR)" "$(VALGRIND_BUILD_DIR)"; do \
		if [ -f "$$d/CMakeCache.txt" ]; then \
			$(CMAKE) --build "$$d" --target clean --parallel >/dev/null 2>&1 || true; \
		fi; \
	done
	@rm -rf "$(RAW_LOGS_DIR)"
	@printf "%b[ok]%b clean complete\n" "$(C_GREEN)" "$(C_RESET)"

deepclean:
	@rm -rf build "$(LOG_ROOT)" warnings
	@printf "%b[ok]%b deepclean complete\n" "$(C_GREEN)" "$(C_RESET)"
