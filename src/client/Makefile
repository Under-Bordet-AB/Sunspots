# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -I.
LDFLAGS =

# Build directory
BUILD_DIR = ../../build

# Target executable
TARGET = $(BUILD_DIR)/client

# Source files
SRCS = main.cpp \
	   menu/menu.cpp \
	   menu/input/input.cpp \
       http/http_client.cpp \
	   plan_service/plan_service.cpp 

# Object files (placed in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))

# Header files (for dependencies)
HEADERS = menu/menu.hpp \
		  menu/input/input.hpp \
		  http/http_client.hpp \
          http/http_status.hpp \
		  plan_service/plan_service.hpp \
		  config.hpp

# Default target
all: $(TARGET)

# Create build directory if it doesn't exist
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)/http

# Link the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Compile source files to object files
$(BUILD_DIR)/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Run the client
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "Cleaned build artifacts"

# Rebuild everything
rebuild: clean all

# Debug build with symbols
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: rebuild

# Release build with optimizations
release: CXXFLAGS += -O3 -DNDEBUG
release: rebuild

# Help target
help:
	@echo "Available targets:"
	@echo "  all      - Build the client (default)"
	@echo "  run      - Build and run the client"
	@echo "  clean    - Remove build artifacts"
	@echo "  rebuild  - Clean and rebuild"
	@echo "  debug    - Build with debug symbols"
	@echo "  release  - Build with optimizations"
	@echo "  help     - Show this help message"

.PHONY: all run clean rebuild debug release help
