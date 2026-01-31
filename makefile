CC := gcc
SOURCES := $(wildcard src/fetch/*.c) $(wildcard src/libs/*.c)
INCLUDES := $(shell find src -type d | sed 's/^/-I/')
CFLAGS := -Wall -Wextra -pthread $(INCLUDES)
LDFLAGS := -lcurl -lcjson
TARGET := fetch_manager

fetch_manager: src/fetch/fetch_manager.c
	$(CC) $(SOURCES) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f fetch_manager

run:
	./fetch_manager 1337 900