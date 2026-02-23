# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -I.
LDFLAGS =

# Build directory
BUILD_DIR = ../../build

# Target executable
TARGET = $(BUILD_DIR)/client

# Source files (mix of .cpp and .c)
SRCS = main.cpp \
       http/http_client.cpp \
       http/http_request.cpp \
       http/http_response.cpp \
       menu/menu.cpp \
       menu/input/input.cpp \
       plan_service/plan_service.cpp \
       ../libs/json/cJSON.c

# split sources by extension
CPP_SRCS := $(filter %.cpp,$(SRCS))
C_SRCS   := $(filter %.c,$(SRCS))

# Object files (placed in build directory, preserve subpaths)
OBJS_CPP := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(CPP_SRCS))
OBJS_C   := $(patsubst %.c,   $(BUILD_DIR)/%.o, $(C_SRCS))
OBJS := $(OBJS_CPP) $(OBJS_C)

# Header files (for dependencies)
HEADERS = config.hpp \
          http/http_client.hpp \
          http/http_request.hpp \
          http/http_response.hpp \
          http/http_status.hpp \
          menu/menu.hpp \
          menu/input/input.hpp \
          plan_service/plan_service.hpp \
          ../libs/json/cJSON.h

# Default target
all: $(TARGET)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/http
	@mkdir -p $(BUILD_DIR)/menu/input
	@mkdir -p $(BUILD_DIR)/plan_service
	@mkdir -p $(dir $(OBJS_C)) || true

# Link the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
    @echo "Build complete: $(TARGET)"

# Compile C++ source files to object files
$(BUILD_DIR)/%.o: %.cpp $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile C source files to object files
$(BUILD_DIR)/%.o: %.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Run the client
run: $(TARGET)
	$(TARGET)

# Clean build artifacts
clean:
	-rm -rf $(BUILD_DIR)/*

# Rebuild everything
rebuild: clean all

# Debug/Release helpers
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: rebuild

release: CXXFLAGS += -O3 -DNDEBUG
release: rebuild

.PHONY: all run clean rebuild debug release
