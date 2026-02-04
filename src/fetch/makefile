CC := gcc
SOURCES_t := $(wildcard *.c) $(wildcard ../libs/*.c)
SOURCES := fetch_manager_forks.c curly.c $(wildcard ../libs/*.c)
INCLUDES := -I.. -I../../ -I/usr/include/cjson/
CFLAGS := -Wall -Wextra -Wno-unused-parameter -pthread $(INCLUDES)
LDFLAGS := -lcurl -lcjson
TARGET := fetch_manager_forks

fetch_manager_forks: fetch_manager_forks.c
	$(CC) $(SOURCES) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f fetch_manager_forks

run:
	./fetch_manager_forks 1337