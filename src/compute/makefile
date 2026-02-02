CC := gcc
SOURCES := $(wildcard *.c) $(wildcard ../libs/*.c)
INCLUDES := $(shell find src -type d | sed 's/^/-I')
CFLAGS := -Wall -Wextra -pthread $(INCLUDES)
LDFLAGS := -lcurl -lcjson
TARGET := compute_manager

compute_manager: compute_manager.c
	$(CC) $(SOURCES) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

run:
	./compute_manager 5

clean:
	rm -f compute_manager