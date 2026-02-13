SHELL := /bin/sh
.DEFAULT_GOAL := build

BUILD_DIR ?= build/debug
VALGRIND_BUILD_DIR ?= build/valgrind
CONFIG ?= Debug
GENERATOR ?= Ninja
CMAKE ?= cmake
CTEST ?= ctest
COMPONENT ?=
ARGS ?=

CMAKE_FLAGS := -G "$(GENERATOR)" -S . -B "$(BUILD_DIR)" \
	-DCMAKE_BUILD_TYPE=$(CONFIG) \
	-DSUNSPOTS_ENABLE_SANITIZERS=ON \
	-DBUILD_TESTING=ON \
	-DSUNSPOTS_BUILD_BENCHMARKS=ON

.PHONY: configure build rebuild test test-unit test-integration test-component run bench smoke valgrind tidy clean deepclean

configure:
	@$(CMAKE) $(CMAKE_FLAGS)

build: configure
	@$(CMAKE) --build "$(BUILD_DIR)" --parallel

rebuild: clean build

test: build
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure

test-unit: build
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure -L unit

test-integration: build
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure -L integration

test-component: build
	@if [ -z "$(COMPONENT)" ]; then \
		echo "Set COMPONENT=<name>, e.g. make test-component COMPONENT=compute"; \
		exit 1; \
	fi
	@$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure -L "component:$(COMPONENT)"

run: build
	@if [ -z "$(COMPONENT)" ]; then \
		echo "Set COMPONENT=<target>, e.g. make run COMPONENT=sunspots_frontend"; \
		exit 1; \
	fi
	@"$(BUILD_DIR)/$(COMPONENT)" $(ARGS)

bench: build
	@if [ -x "$(BUILD_DIR)/benchmarks/sample_benchmark" ]; then \
		"$(BUILD_DIR)/benchmarks/sample_benchmark" --benchmark_min_time=0.01 --benchmark_repetitions=1; \
	else \
		echo "bench: benchmark executable not found (build first)"; \
		exit 1; \
	fi

smoke: build
	@bash -eu -o pipefail -c '\
		manager_path="$(BUILD_DIR)/src/fetch/sunspots_fetch_manager"; \
		api_openmeteo_path="$(BUILD_DIR)/src/fetch/apis/sunspots_fetch_openmeteo"; \
		api_elpris_path="$(BUILD_DIR)/src/fetch/apis/sunspots_fetch_elprisjustnu"; \
		if [ ! -x "$$manager_path" ]; then echo "smoke: missing $$manager_path"; exit 1; fi; \
		if [ ! -x "$$api_openmeteo_path" ]; then echo "smoke: missing $$api_openmeteo_path"; exit 1; fi; \
		if [ ! -x "$$api_elpris_path" ]; then echo "smoke: missing $$api_elpris_path"; exit 1; fi; \
		mkdir -p .db; \
		rm -f .db/database.jsonl; \
		cleanup() { \
			if [ -n "$${manager_pid:-}" ] && kill -0 "$$manager_pid" 2>/dev/null; then \
				for cpid in $$(ps -o pid= --ppid "$$manager_pid" 2>/dev/null); do kill "$$cpid" 2>/dev/null || true; done; \
				kill "$$manager_pid" 2>/dev/null || true; \
			fi; \
			if [ -n "$${parent_pid:-}" ] && kill -0 "$$parent_pid" 2>/dev/null; then kill "$$parent_pid" 2>/dev/null || true; fi; \
		}; \
		trap cleanup EXIT; \
		bash -c '\''trap "" 34; sleep 30'\'' & parent_pid=$$!; \
		"$$manager_path" "$$parent_pid" 2 >/dev/null 2>&1 & manager_pid=$$!; \
		sleep 4; \
		if ! kill -0 "$$manager_pid" 2>/dev/null; then echo "smoke: fetch manager died at startup"; exit 1; fi; \
		if ! pgrep -f "$$api_openmeteo_path" >/dev/null; then echo "smoke: openmeteo worker not running"; exit 1; fi; \
		if ! pgrep -f "$$api_elpris_path" >/dev/null; then echo "smoke: elpris worker not running"; exit 1; fi; \
		if [ ! -s .db/database.jsonl ]; then echo "smoke: no database writes detected"; exit 1; fi; \
		echo "smoke: fetch manager + API workers started and wrote data"; \
	'

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

clean:
	@for d in "$(BUILD_DIR)" "$(VALGRIND_BUILD_DIR)" "build/tidy"; do \
		if [ -f "$$d/CMakeCache.txt" ]; then \
			$(CMAKE) --build "$$d" --target clean --parallel; \
		fi; \
	done
	@echo "clean: cleaned configured build outputs (if present)"

deepclean:
	@rm -rf build
	@echo "deepclean: removed build directory"
