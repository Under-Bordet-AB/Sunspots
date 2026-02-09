.PHONY: all debug dev release clean format lint run-daemon

all: debug

debug:
	cmake --preset debug
	cmake --build --preset debug

dev:
	cmake --preset dev
	cmake --build --preset dev

release:
	cmake --preset release
	cmake --build --preset release

clean:
	rm -rf build

format:
	@if command -v clang-format >/dev/null; then \
		find src -name "*.c" -o -name "*.h" | xargs clang-format -i; \
		echo "clang-format applied"; \
	else \
		echo "clang-format not found, skipping."; \
	fi

lint:
	@if command -v clang-tidy >/dev/null; then \
		cmake --preset dev >/dev/null; \
		clang-tidy -p build/dev $$(find src -name "*.c" -not -path "src/libs/*") ; \
	else \
		echo "clang-tidy not found, skipping."; \
	fi

run-daemon: debug
	./bin/sunspots_daemon daemon

