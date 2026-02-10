.PHONY: all debug release test clean run stop

all: debug

debug:
	cmake --preset debug
	cmake --build --preset debug

release:
	cmake --preset release
	cmake --build --preset release

test: debug
	@echo "tests are intentionally removed in this development phase"

run: debug
	./build/bin/sunspotsd

stop:
	-pkill -TERM -x sunspotsd
	-pkill -TERM -x fetch_worker
	-pkill -TERM -x sunspots_server
	-pkill -TERM -f '(^|/)make run($$| )'
	sleep 1
	-pkill -KILL -x sunspotsd
	-pkill -KILL -x fetch_worker
	-pkill -KILL -x sunspots_server
	-pkill -KILL -f '(^|/)make run($$| )'

clean:
	rm -rf build
