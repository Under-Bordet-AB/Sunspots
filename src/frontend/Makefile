# ===== Project settings =====
TARGET := frontend
CC     := gcc

# Directories
SRC_DIR   := .
LIB_DIR   := ../libs/linked_list

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c) \
        $(wildcard $(LIB_DIR)/*.c)

# Object files
OBJS := $(SRCS:.c=.o)

# Include paths
INCLUDES := -I$(SRC_DIR) -I$(LIB_DIR)

# ===== Compiler flags =====
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic $(INCLUDES)
CFLAGS += -D_POSIX_C_SOURCE=200809L
LDFLAGS :=

# Release / Debug flags
RELEASE_FLAGS := -O2
DEBUG_FLAGS   := -O0 -g3 \
                 -fsanitize=address,undefined \
                 -fno-omit-frame-pointer \
                 -fstack-protector-strong \
                 -DDEBUG

# ===== Default target =====
.PHONY: all
all: CFLAGS += $(RELEASE_FLAGS)
all: $(TARGET)

# ===== Debug target =====
.PHONY: debug
debug: CFLAGS += $(DEBUG_FLAGS)
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean $(TARGET)

# ===== Linking =====
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# ===== Compilation =====
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ===== Cleanup =====
.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)