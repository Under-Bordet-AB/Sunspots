CC := gcc
SOURCES := $(wildcard *.c) $(wildcard ../libs/*.c)
INCLUDES := -I.. -I../../ -I/usr/include/cjson/
CFLAGS := -Wall -Wextra -pthread $(INCLUDES)
LDFLAGS := -lcurl -lcjson
TARGET := fetch_manager

fetch_manager: fetch_manager.c
	$(CC) $(SOURCES) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f fetch_manager

run:
	./fetch_manager 1337 900