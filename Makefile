SHELL := /bin/sh
.DEFAULT_GOAL := build

BUILD_DIR ?= build/debug
VALGRIND_BUILD_DIR ?= build/valgrind
FUZZ_BUILD_DIR ?= build/fuzz
CONFIG ?= Debug
GENERATOR ?= Unix Makefiles
CMAKE ?= cmake
CTEST ?= ctest
COMPONENT ?=
ARGS ?=
FUZZ_ENGINE ?= libfuzzer
FUZZ_TIME ?= 60
FUZZ_TARGETS ?= http_request_fuzzer openmeteo_transform_fuzzer config_args_fuzzer compute_calculator_fuzzer fetch_manager_config_fuzzer sdk_db_fuzzer
FUZZ_CORPUS_DIR ?= fuzz/corpus
FUZZ_LOG_DIR ?= build/fuzz-logs
FUZZ_ARTIFACT_DIR ?= build/fuzz-artifacts
FUZZ_AFL_OUT_DIR ?= build/afl-out
COVERAGE_OUT_DIR ?= scripts/out/coverage
COVERAGE_SRC_JSON ?= $(COVERAGE_OUT_DIR)/coverage_src_current.json
WARNINGS_DIR ?= warnings
WARNING_SPLITTER ?= ./scripts/split_warnings.sh
WARNING_SPLITTER_VERBOSE ?= 0

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

CMAKE_FLAGS_QUICK := -G "$(GENERATOR)" -S . -B "$(BUILD_DIR)" \
	-DCMAKE_BUILD_TYPE=$(CONFIG) \
	-DSUNSPOTS_ENABLE_SANITIZERS=ON \
	-DBUILD_TESTING=OFF \
	-DSUNSPOTS_BUILD_BENCHMARKS=OFF

CMAKE_FLAGS_FULL := -G "$(GENERATOR)" -S . -B "$(BUILD_DIR)" \
	-DCMAKE_BUILD_TYPE=$(CONFIG) \
	-DSUNSPOTS_ENABLE_SANITIZERS=ON \
	-DBUILD_TESTING=ON \
	-DSUNSPOTS_BUILD_BENCHMARKS=ON

.PHONY: help all configure configure-full build build-full rebuild test test-unit test-integration test-component run run-prepare stop bench valgrind tidy fuzz-configure fuzz-build fuzz-run fuzz-all check-code coverage-json check-scripts scripts clean deepclean

help:
	@printf "\nSunspots Make Targets\n\n"
	@printf "Build:\n"
	@printf "  %-48s %s\n" "make (default) / make build" "Fast Debug build, no tests ($(BUILD_DIR))"
	@printf "  %-48s %s\n" "make build-full" "Configure and build with tests+bench enabled"
	@printf "  %-48s %s\n" "make rebuild" "Clean then quick build"
	@printf "  %-48s %s\n" "make all" "Full pass: build-full + tests + tidy"
	@printf "\nTest:\n"
	@printf "  %-48s %s\n" "make test" "Build and run all tests"
	@printf "  %-48s %s\n" "make test-unit" "Build and run unit tests"
	@printf "  %-48s %s\n" "make test-integration" "Build and run integration tests (if present)"
	@printf "  %-48s %s\n" "make test-component COMPONENT=<name>" "Run tests for one component label"
	@printf "\nRun/Bench:\n"
	@printf "  %-48s %s\n" "make run [COMPONENT=<bin>] [ARGS=\"...\"]" "Run a built executable"
	@printf "  %-48s %s\n" "make bench" "Run sample + SDK benchmarks"
	@printf "  %-48s %s\n" "make valgrind" "Run valgrind-labeled tests"
	@printf "  %-48s %s\n" "make tidy" "Build with clang-tidy enabled"
	@printf "\nFuzz:\n"
	@printf "  %-48s %s\n" "make fuzz-configure FUZZ_ENGINE=libfuzzer|afl" "Configure fuzz build"
	@printf "  %-48s %s\n" "make fuzz-build FUZZ_ENGINE=libfuzzer|afl" "Build fuzzers"
	@printf "  %-48s %s\n" "make fuzz-run TARGET=<fuzzer> [FUZZ_ENGINE=...] [FUZZ_TIME=60]" "Run one fuzzer target"
	@printf "  %-48s %s\n" "make fuzz-all [FUZZ_ENGINE=...] [FUZZ_TIME=60]" "Run all configured fuzzers"
	@printf "\nChecks/Cleanup:\n"
	@printf "  %-48s %s\n" "make check-code" "Run code quality report script"
	@printf "  %-48s %s\n" "make scripts" "Run project script checks + coverage JSON"
	@printf "  %-48s %s\n" "make check-scripts" "Run script checks + coverage JSON"
	@printf "  %-48s %s\n" "make clean" "Clean configured build outputs"
	@printf "  %-48s %s\n" "make deepclean" "Remove build directory"
	@printf "\nLogs:\n"
	@printf "  %-48s %s\n" "$(BUILD_DIR)/configure.log" "Last quick configure output"
	@printf "  %-48s %s\n" "$(BUILD_DIR)/configure_full.log" "Last full configure output"
	@printf "\n"

all:
	@build_status="PASS"; test_status="SKIP"; tidy_status="SKIP"; \
	printf "\n%bSunspots Full Pipeline%b\n" "$(C_BOLD)" "$(C_RESET)"; \
	printf "%b[step]%b build-full\n" "$(C_CYAN)" "$(C_RESET)"; \
	if $(MAKE) --no-print-directory build-full; then \
		build_status="PASS"; \
	else \
		build_status="FAIL"; \
	fi; \
	if [ "$$build_status" = "PASS" ]; then \
		printf "%b[step]%b test\n" "$(C_CYAN)" "$(C_RESET)"; \
		if $(MAKE) --no-print-directory test; then \
			test_status="PASS"; \
		else \
			test_status="FAIL"; \
		fi; \
	else \
		test_status="SKIP"; \
		printf "%b[warn]%b skipping tests because build-full failed\n" "$(C_YELLOW)" "$(C_RESET)"; \
	fi; \
	printf "%b[step]%b tidy\n" "$(C_CYAN)" "$(C_RESET)"; \
	if $(MAKE) --no-print-directory tidy; then \
		tidy_status="PASS"; \
	else \
		tidy_status="FAIL"; \
	fi; \
	printf "\n%b%-14s %-10s%b\n" "$(C_BOLD)" "Stage" "Result" "$(C_RESET)"; \
	printf "%-14s %-10s\n" "build-full" "$$build_status"; \
	printf "%-14s %-10s\n" "test" "$$test_status"; \
	printf "%-14s %-10s\n" "tidy" "$$tidy_status"; \
	if [ "$$build_status" = "FAIL" ] || [ "$$test_status" = "FAIL" ]; then \
		printf "%b[error]%b make all finished with failures\n" "$(C_RED)" "$(C_RESET)"; \
		exit 1; \
	fi; \
	printf "%b[ok]%b make all complete\n" "$(C_GREEN)" "$(C_RESET)"

configure:
	@printf "%b[step]%b configure quick (%s)\n" "$(C_CYAN)" "$(C_RESET)" "$(BUILD_DIR)"
	@mkdir -p "$(BUILD_DIR)"
	@$(CMAKE) $(CMAKE_FLAGS_QUICK) >"$(BUILD_DIR)/configure.log" 2>&1 || { \
		printf "%b[error]%b configure failed. Last lines from %s:\n" "$(C_RED)" "$(C_RESET)" "$(BUILD_DIR)/configure.log"; \
		tail -n 60 "$(BUILD_DIR)/configure.log"; \
		exit 1; \
	}
	@printf "%b[ok]%b configure complete\n" "$(C_GREEN)" "$(C_RESET)"

configure-full:
	@printf "%b[step]%b configure full (%s)\n" "$(C_CYAN)" "$(C_RESET)" "$(BUILD_DIR)"
	@mkdir -p "$(BUILD_DIR)"
	@$(CMAKE) $(CMAKE_FLAGS_FULL) >"$(BUILD_DIR)/configure_full.log" 2>&1 || { \
		printf "%b[error]%b full configure failed. Last lines from %s:\n" "$(C_RED)" "$(C_RESET)" "$(BUILD_DIR)/configure_full.log"; \
		tail -n 60 "$(BUILD_DIR)/configure_full.log"; \
		exit 1; \
	}
	@printf "%b[ok]%b full configure complete\n" "$(C_GREEN)" "$(C_RESET)"

build: configure
	@printf "%b[step]%b build compiling targets\n" "$(C_CYAN)" "$(C_RESET)"
	@mkdir -p "$(WARNINGS_DIR)/build"
	@bash -o pipefail -c '$(CMAKE) --build "$(BUILD_DIR)" --parallel 2>&1 | WARNING_SPLITTER_VERBOSE="$(WARNING_SPLITTER_VERBOSE)" WARN_PREFIX="$(C_YELLOW)[warn]$(C_RESET)" $(WARNING_SPLITTER) "$(WARNINGS_DIR)/build" "build.raw.log"'
	@printf "%b[ok]%b build complete\n" "$(C_GREEN)" "$(C_RESET)"

build-full: configure-full
	@printf "%b[step]%b build-full compiling targets\n" "$(C_CYAN)" "$(C_RESET)"
	@mkdir -p "$(WARNINGS_DIR)/build-full"
	@bash -o pipefail -c '$(CMAKE) --build "$(BUILD_DIR)" --parallel 2>&1 | WARNING_SPLITTER_VERBOSE="$(WARNING_SPLITTER_VERBOSE)" WARN_PREFIX="$(C_YELLOW)[warn]$(C_RESET)" $(WARNING_SPLITTER) "$(WARNINGS_DIR)/build-full" "build-full.raw.log"'
	@printf "%b[ok]%b build-full complete\n" "$(C_GREEN)" "$(C_RESET)"

rebuild: clean build

test: build-full
	@printf "%b[step]%b test running all tests\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure
	@printf "%b[ok]%b tests complete\n" "$(C_GREEN)" "$(C_RESET)"

test-unit: build-full
	@printf "%b[step]%b test-unit running unit tests\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure --no-tests=error -L unit
	@printf "%b[ok]%b unit tests complete\n" "$(C_GREEN)" "$(C_RESET)"

test-integration: build-full
	@printf "%b[step]%b test-integration running integration tests (if present)\n" "$(C_CYAN)" "$(C_RESET)"
	@list_out="$$( $(CTEST) --test-dir "$(BUILD_DIR)" -N -L integration 2>&1 )"; \
	if printf "%s\n" "$$list_out" | grep -q "Total Tests: 0"; then \
		printf "%b[warn]%b no integration tests registered; skipping\n" "$(C_YELLOW)" "$(C_RESET)"; \
	else \
		$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure --no-tests=error -L integration; \
	fi
	@printf "%b[ok]%b integration test stage complete\n" "$(C_GREEN)" "$(C_RESET)"

test-component: build-full
	@if [ -z "$(COMPONENT)" ]; then \
		printf "%b[error]%b set COMPONENT=<name>, e.g. make test-component COMPONENT=compute\n" "$(C_RED)" "$(C_RESET)"; \
		exit 1; \
	fi
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure --no-tests=error -L "component:$(COMPONENT)"

run: build
	@$(MAKE) --no-print-directory run-prepare
	@component="$${COMPONENT:-sunspots_daemon}"; \
	component_bin="$$(find "$(BUILD_DIR)" -type f -perm -111 -name "$$component" | head -n 1)"; \
	if [ -z "$$component_bin" ]; then \
		printf "%b[error]%b run: could not find executable '%s' under %s\n" "$(C_RED)" "$(C_RESET)" "$$component" "$(BUILD_DIR)"; \
		printf "%b[error]%b run: build that target first or pass COMPONENT=<name>\n" "$(C_RED)" "$(C_RESET)"; \
		exit 1; \
	fi; \
	"$$component_bin" $(ARGS)

run-prepare:
	@mkdir -p "$(BUILD_DIR)"
	@if [ -e "$(BUILD_DIR)/config" ] && [ ! -L "$(BUILD_DIR)/config" ] && [ ! -d "$(BUILD_DIR)/config" ]; then \
		printf "%b[error]%b run-prepare: %s exists and is not a directory/symlink\n" "$(C_RED)" "$(C_RESET)" "$(BUILD_DIR)/config"; \
		exit 1; \
	fi
	@if [ ! -e "$(BUILD_DIR)/config" ]; then \
		ln -s "$$(pwd)/config" "$(BUILD_DIR)/config"; \
		printf "%b[ok]%b run-prepare: linked %s -> ./config\n" "$(C_GREEN)" "$(C_RESET)" "$(BUILD_DIR)/config"; \
	fi
	@if [ ! -r "$(BUILD_DIR)/config/sunspots.json" ]; then \
		printf "%b[error]%b run-prepare: missing readable %s\n" "$(C_RED)" "$(C_RESET)" "$(BUILD_DIR)/config/sunspots.json"; \
		exit 1; \
	fi

stop:
	@pids="$$(pgrep -f "$(BUILD_DIR)/src/core/sunspots_daemon($$| )" || true)"; \
	if [ -z "$$pids" ]; then \
		printf "%b[warn]%b sunspots_daemon not running\n" "$(C_YELLOW)" "$(C_RESET)"; \
	else \
		kill -SIGINT $$pids; \
		sleep 1; \
		printf "%b[ok]%b sent SIGINT to sunspots_daemon for graceful shutdown\n" "$(C_GREEN)" "$(C_RESET)"; \
	fi

bench: build-full
	@if [ ! -x "$(BUILD_DIR)/benchmarks/sample_benchmark" ] || [ ! -x "$(BUILD_DIR)/benchmarks/sdk_db_benchmark" ]; then \
		printf "%b[error]%b bench: benchmark executable not found (build first)\n" "$(C_RED)" "$(C_RESET)"; \
		exit 1; \
	fi
	@"$(BUILD_DIR)/benchmarks/sample_benchmark" --benchmark_min_time=0.01 --benchmark_repetitions=1
	@"$(BUILD_DIR)/benchmarks/sdk_db_benchmark" --benchmark_min_time=0.01 --benchmark_repetitions=1

valgrind:
	@printf "%b[step]%b valgrind configure + build + test\n" "$(C_CYAN)" "$(C_RESET)"
	@$(CMAKE) -G "$(GENERATOR)" -S . -B "$(VALGRIND_BUILD_DIR)" \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DSUNSPOTS_ENABLE_SANITIZERS=OFF \
		-DBUILD_TESTING=ON \
		-DSUNSPOTS_BUILD_BENCHMARKS=ON
	@$(CMAKE) --build "$(VALGRIND_BUILD_DIR)" --parallel
	@$(CTEST) --test-dir "$(VALGRIND_BUILD_DIR)" --output-on-failure -L valgrind
	@printf "%b[ok]%b valgrind stage complete\n" "$(C_GREEN)" "$(C_RESET)"

tidy:
	@printf "%b[step]%b tidy configure + build (non-fatal warnings policy)\n" "$(C_CYAN)" "$(C_RESET)"
	@mkdir -p "$(WARNINGS_DIR)/tidy"
	@$(CMAKE) -G "$(GENERATOR)" -S . -B build/tidy \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSUNSPOTS_ENABLE_SANITIZERS=ON \
		-DSUNSPOTS_ENABLE_CLANG_TIDY=ON \
		-DBUILD_TESTING=ON \
		-DSUNSPOTS_BUILD_BENCHMARKS=ON
	@bash -o pipefail -c '$(CMAKE) --build build/tidy --parallel 2>&1 | WARNING_SPLITTER_VERBOSE="$(WARNING_SPLITTER_VERBOSE)" WARN_PREFIX="$(C_YELLOW)[warn]$(C_RESET)" $(WARNING_SPLITTER) "$(WARNINGS_DIR)/tidy" "tidy.raw.log"' || { \
		rc=$$?; \
		printf "%b[warn]%b clang-tidy reported issues (non-fatal).\n" "$(C_YELLOW)" "$(C_RESET)"; \
		printf "%b[warn]%b tidy build exited with %s, continuing by policy.\n" "$(C_YELLOW)" "$(C_RESET)" "$$rc"; \
		exit 0; \
	}
	@printf "%b[ok]%b tidy complete (warning files: %s)\n" "$(C_GREEN)" "$(C_RESET)" "$(WARNINGS_DIR)/tidy"

fuzz-configure:
	@if [ "$(FUZZ_ENGINE)" = "libfuzzer" ]; then \
		$(CMAKE) -G "$(GENERATOR)" -S . -B "$(FUZZ_BUILD_DIR)" \
			-DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_C_COMPILER=clang \
			-DCMAKE_CXX_COMPILER=clang++ \
			-DSUNSPOTS_ENABLE_SANITIZERS=OFF \
			-DBUILD_TESTING=OFF \
			-DSUNSPOTS_BUILD_BENCHMARKS=OFF \
			-DSUNSPOTS_BUILD_FUZZERS=ON \
			-DSUNSPOTS_FUZZ_ENGINE=libfuzzer; \
	elif [ "$(FUZZ_ENGINE)" = "afl" ]; then \
		$(CMAKE) -G "$(GENERATOR)" -S . -B "$(FUZZ_BUILD_DIR)" \
			-DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_C_COMPILER=afl-clang-fast \
			-DCMAKE_CXX_COMPILER=afl-clang-fast++ \
			-DSUNSPOTS_ENABLE_SANITIZERS=OFF \
			-DBUILD_TESTING=OFF \
			-DSUNSPOTS_BUILD_BENCHMARKS=OFF \
			-DSUNSPOTS_BUILD_FUZZERS=ON \
			-DSUNSPOTS_FUZZ_ENGINE=afl; \
		else \
			printf "%b[error]%b fuzz-configure: FUZZ_ENGINE must be libfuzzer or afl\n" "$(C_RED)" "$(C_RESET)"; \
			exit 1; \
		fi

fuzz-build: fuzz-configure
	@$(CMAKE) --build "$(FUZZ_BUILD_DIR)" --parallel

fuzz-run: fuzz-build
	@if [ -z "$(TARGET)" ]; then \
		printf "%b[error]%b set TARGET=<fuzzer>, e.g. make fuzz-run TARGET=http_request_fuzzer\n" "$(C_RED)" "$(C_RESET)"; \
		exit 1; \
	fi
	@mkdir -p "$(FUZZ_CORPUS_DIR)/$(TARGET)" "$(FUZZ_LOG_DIR)" "$(FUZZ_ARTIFACT_DIR)/$(TARGET)"
	@log_file="$(FUZZ_LOG_DIR)/$(TARGET).log"; \
		if [ "$(FUZZ_ENGINE)" = "libfuzzer" ]; then \
			printf "%b[step]%b fuzz-run: %s with libFuzzer for %ss\n" "$(C_CYAN)" "$(C_RESET)" "$(TARGET)" "$(FUZZ_TIME)"; \
			bash -o pipefail -c 'ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 \
				"$(FUZZ_BUILD_DIR)/fuzz/$(TARGET)" \
					-max_total_time="$(FUZZ_TIME)" \
				-print_final_stats=1 \
				-artifact_prefix="$(FUZZ_ARTIFACT_DIR)/$(TARGET)/" \
				"$(FUZZ_CORPUS_DIR)/$(TARGET)" 2>&1 | tee "$$1"' _ "$$log_file"; \
		elif [ "$(FUZZ_ENGINE)" = "afl" ]; then \
			printf "%b[step]%b fuzz-run: %s with AFL++ for %ss\n" "$(C_CYAN)" "$(C_RESET)" "$(TARGET)" "$(FUZZ_TIME)"; \
			mkdir -p "$(FUZZ_AFL_OUT_DIR)/$(TARGET)"; \
		if [ ! -e "$(FUZZ_CORPUS_DIR)/$(TARGET)/seed" ]; then \
			printf "seed" > "$(FUZZ_CORPUS_DIR)/$(TARGET)/seed"; \
		fi; \
		bash -o pipefail -c 'AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 timeout "$(FUZZ_TIME)s" \
			afl-fuzz -i "$(FUZZ_CORPUS_DIR)/$(TARGET)" -o "$(FUZZ_AFL_OUT_DIR)/$(TARGET)" -- \
			"$(FUZZ_BUILD_DIR)/fuzz/$(TARGET)" @@ 2>&1 | tee "$$1"; \
			rc=$${PIPESTATUS[0]}; \
			if [ "$$rc" -ne 0 ] && [ "$$rc" -ne 124 ]; then \
				exit "$$rc"; \
			fi' _ "$$log_file"; \
		else \
			printf "%b[error]%b fuzz-run: FUZZ_ENGINE must be libfuzzer or afl\n" "$(C_RED)" "$(C_RESET)"; \
			exit 1; \
		fi

fuzz-all: fuzz-build
	@for t in $(FUZZ_TARGETS); do \
		printf "%b[step]%b fuzz-all: starting %s\n" "$(C_CYAN)" "$(C_RESET)" "$$t"; \
		$(MAKE) --no-print-directory fuzz-run TARGET="$$t" FUZZ_ENGINE="$(FUZZ_ENGINE)" FUZZ_TIME="$(FUZZ_TIME)" || exit $$?; \
	done

check-code:
	@python3 scripts/check_code.py \
		--out scripts/check_code_out.md \
		--unsafe-rules scripts/unsafe.md \
		--unsafe-out scripts/check_code_unsafe_out.md

coverage-json:
	@mkdir -p "$(COVERAGE_OUT_DIR)"
	@python3 -m gcovr -j 1 -r . build-cov --json "$(COVERAGE_SRC_JSON)" >/dev/null
	@printf "%b[ok]%b wrote coverage report: %s\n" "$(C_GREEN)" "$(C_RESET)" "$(COVERAGE_SRC_JSON)"

check-scripts: coverage-json
	@python3 scripts/check_code.py \
		--out scripts/check_code_out.md \
		--unsafe-rules scripts/unsafe.md \
		--unsafe-out scripts/check_code_unsafe_out.md
	@python3 scripts/check_testability.py
	@python3 scripts/check_modules_report.py --coverage-json "$(COVERAGE_SRC_JSON)"

scripts: check-scripts

clean:
	@for d in "$(BUILD_DIR)" "$(VALGRIND_BUILD_DIR)" "$(FUZZ_BUILD_DIR)" "build/tidy"; do \
		if [ -f "$$d/CMakeCache.txt" ]; then \
			$(CMAKE) --build "$$d" --target clean --parallel >/dev/null 2>&1; \
		fi; \
	done
	@rm -rf "$(WARNINGS_DIR)"
	@printf "%b[ok]%b clean: cleaned configured build outputs\n" "$(C_GREEN)" "$(C_RESET)"

deepclean:
	@rm -rf build
	@printf "%b[ok]%b deepclean: removed build directory\n" "$(C_GREEN)" "$(C_RESET)"
