SHELL := /bin/sh
.DEFAULT_GOAL := build

BUILD_DIR ?= build/debug
VALGRIND_BUILD_DIR ?= build/valgrind
FUZZ_BUILD_DIR ?= build/fuzz
CONFIG ?= Debug
GENERATOR ?= Ninja
CMAKE ?= cmake
CTEST ?= ctest
COMPONENT ?=
ARGS ?=
FUZZ_ENGINE ?= libfuzzer
FUZZ_TIME ?= 60
FUZZ_TARGETS ?= http_request_fuzzer openmeteo_transform_fuzzer config_args_fuzzer compute_calculator_fuzzer fetch_manager_config_fuzzer
FUZZ_CORPUS_DIR ?= fuzz/corpus
FUZZ_LOG_DIR ?= build/fuzz-logs
FUZZ_ARTIFACT_DIR ?= build/fuzz-artifacts
FUZZ_AFL_OUT_DIR ?= build/afl-out

CMAKE_FLAGS := -G "$(GENERATOR)" -S . -B "$(BUILD_DIR)" \
	-DCMAKE_BUILD_TYPE=$(CONFIG) \
	-DSUNSPOTS_ENABLE_SANITIZERS=ON \
	-DBUILD_TESTING=ON \
	-DSUNSPOTS_BUILD_BENCHMARKS=ON

.PHONY: configure build rebuild test test-unit test-integration test-component run bench valgrind tidy fuzz-configure fuzz-build fuzz-run fuzz-all fuzz clean deepclean

configure:
	@$(CMAKE) $(CMAKE_FLAGS)

build: configure
	@$(CMAKE) --build "$(BUILD_DIR)" --parallel

rebuild: clean build

test: build
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure

test-unit: build
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure --no-tests=error -L unit

test-integration: build
	@list_out="$$( $(CTEST) --test-dir "$(BUILD_DIR)" -N -L integration 2>&1 )"; \
	if printf "%s\n" "$$list_out" | grep -q "Total Tests: 0"; then \
		echo "test-integration: no integration tests registered; skipping"; \
	else \
		$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure --no-tests=error -L integration; \
	fi

test-component: build
	@if [ -z "$(COMPONENT)" ]; then \
		echo "Set COMPONENT=<name>, e.g. make test-component COMPONENT=compute"; \
		exit 1; \
	fi
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure --no-tests=error -L "component:$(COMPONENT)"

run: build
	@if [ -z "$(COMPONENT)" ]; then \
		echo "Set COMPONENT=<target>, e.g. make run COMPONENT=sunspots_frontend"; \
		exit 1; \
	fi
	@component_bin="$$(find "$(BUILD_DIR)" -type f -perm -111 -name "$(COMPONENT)" | head -n 1)"; \
	if [ -z "$$component_bin" ]; then \
		echo "run: could not find executable named '$(COMPONENT)' under $(BUILD_DIR)"; \
		echo "run: build with that target name first or pass the exact executable name"; \
		exit 1; \
	fi; \
	"$$component_bin" $(ARGS)

bench: build
	@if [ -x "$(BUILD_DIR)/benchmarks/sample_benchmark" ]; then \
		"$(BUILD_DIR)/benchmarks/sample_benchmark" --benchmark_min_time=0.01 --benchmark_repetitions=1; \
	else \
		echo "bench: benchmark executable not found (build first)"; \
		exit 1; \
	fi

valgrind:
	@$(CMAKE) -G "$(GENERATOR)" -S . -B "$(VALGRIND_BUILD_DIR)" \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DSUNSPOTS_ENABLE_SANITIZERS=OFF \
		-DBUILD_TESTING=ON \
		-DSUNSPOTS_BUILD_BENCHMARKS=ON
	@$(CMAKE) --build "$(VALGRIND_BUILD_DIR)" --parallel
	@$(CTEST) --test-dir "$(VALGRIND_BUILD_DIR)" --output-on-failure -L valgrind

tidy:
	@$(CMAKE) -G "$(GENERATOR)" -S . -B build/tidy \
		-DCMAKE_BUILD_TYPE=Debug \
		-DSUNSPOTS_ENABLE_SANITIZERS=ON \
		-DSUNSPOTS_ENABLE_CLANG_TIDY=ON \
		-DBUILD_TESTING=ON \
		-DSUNSPOTS_BUILD_BENCHMARKS=ON
	@$(CMAKE) --build build/tidy --parallel

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
		echo "fuzz-configure: FUZZ_ENGINE must be libfuzzer or afl"; \
		exit 1; \
	fi

fuzz-build: fuzz-configure
	@$(CMAKE) --build "$(FUZZ_BUILD_DIR)" --parallel

fuzz-run: fuzz-build
	@if [ -z "$(TARGET)" ]; then \
		echo "Set TARGET=<fuzzer>, e.g. make fuzz-run TARGET=http_request_fuzzer"; \
		exit 1; \
	fi
	@mkdir -p "$(FUZZ_CORPUS_DIR)/$(TARGET)" "$(FUZZ_LOG_DIR)" "$(FUZZ_ARTIFACT_DIR)/$(TARGET)"
	@log_file="$(FUZZ_LOG_DIR)/$(TARGET).log"; \
	if [ "$(FUZZ_ENGINE)" = "libfuzzer" ]; then \
		echo "fuzz-run: running $(TARGET) with libFuzzer for $(FUZZ_TIME)s"; \
		bash -o pipefail -c 'ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 \
			"$(FUZZ_BUILD_DIR)/fuzz/$(TARGET)" \
				-max_total_time="$(FUZZ_TIME)" \
				-print_final_stats=1 \
				-artifact_prefix="$(FUZZ_ARTIFACT_DIR)/$(TARGET)/" \
				"$(FUZZ_CORPUS_DIR)/$(TARGET)" 2>&1 | tee "$$1"' _ "$$log_file"; \
	elif [ "$(FUZZ_ENGINE)" = "afl" ]; then \
		echo "fuzz-run: running $(TARGET) with AFL++ for $(FUZZ_TIME)s"; \
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
		echo "fuzz-run: FUZZ_ENGINE must be libfuzzer or afl"; \
		exit 1; \
	fi

fuzz-all: fuzz-build
	@for t in $(FUZZ_TARGETS); do \
		echo "fuzz-all: starting $$t"; \
		$(MAKE) --no-print-directory fuzz-run TARGET="$$t" FUZZ_ENGINE="$(FUZZ_ENGINE)" FUZZ_TIME="$(FUZZ_TIME)" || exit $$?; \
	done

fuzz: fuzz-all

clean:
	@for d in "$(BUILD_DIR)" "$(VALGRIND_BUILD_DIR)" "$(FUZZ_BUILD_DIR)" "build/tidy"; do \
		if [ -f "$$d/CMakeCache.txt" ]; then \
			$(CMAKE) --build "$$d" --target clean --parallel >/dev/null 2>&1; \
		fi; \
	done
	@echo "clean: cleaned configured build outputs (if present)"

deepclean:
	@rm -rf build
	@echo "deepclean: removed build directory"
