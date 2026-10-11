# Compiler & Flags-std=c99 
CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude
# Raylib flags (adjust if on macOS/Windows)
RAYLIB_FLAGS = -lraylib -lm -lpthread -ldl

# Target Executable Name
TARGET = ori_vm

# Source Files & Build Objects
SRCS = src/main.c src/lexer.c src/parser.c src/codegen.c src/vm.c
OBJS = $(SRCS:src/%.c=build/%.o)

# Default Rule
all: build $(TARGET)

# Create build directory for object files
build:
	mkdir -p build

# Link all object files into the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(RAYLIB_FLAGS)

# Compile source files into the build folder
build/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run the compiled VM engine with the example source file
run: all
	./$(TARGET) examples/example_code.ori

# Clean up build artifacts and target binary
clean:
	rm -rf build $(TARGET)

.PHONY: all run clean
