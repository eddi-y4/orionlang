# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude
# Target Executable Name
TARGET = ori_vm
VISUALIZER = ori_stack_viz
MEISSA = meissa
ORI_FILE ?= examples/hello.ori
TEST_TARGET = build/test_vm
LANGUAGE_TEST_TARGET = build/test_language
RAYLIB_PKG_CFLAGS = $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_PKG_LIBS = $(shell pkg-config --libs raylib 2>/dev/null)
RAYLIB_CFLAGS ?= $(RAYLIB_PKG_CFLAGS)
RAYLIB_LIBS ?= $(if $(RAYLIB_PKG_LIBS),$(RAYLIB_PKG_LIBS),-lraylib)
ifeq ($(shell uname -s),Darwin)
RAYLIB_PLATFORM_LIBS = -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
else ifeq ($(shell uname -s),Linux)
RAYLIB_PLATFORM_LIBS = -lGL -lm -lpthread -ldl -lrt -lX11
endif

# Source Files & Build Objects
SRCS = src/main.c src/lexer.c src/language_parser.c src/interpreter.c src/listing.c src/vm.c
OBJS = $(SRCS:src/%.c=build/%.o)

# Default Rule
all: build $(TARGET)

# Create build directory for object files
build:
	mkdir -p build

# Link all object files into the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile source files into the build folder
build/%.o: src/%.c $(wildcard include/orion/*.h include/orion/*.def.h)
	$(CC) $(CFLAGS) -c $< -o $@

# Run the compiled VM engine with the example source file
run: all
	./$(TARGET) examples/two_sum.ori

$(VISUALIZER): src/stack_visualizer.c src/lexer.c src/language_parser.c src/interpreter.c src/listing.c src/vm.c $(wildcard include/orion/*.h include/orion/*.def.h) assets/NotoSans-VariableFont.ttf
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ src/stack_visualizer.c src/lexer.c src/language_parser.c src/interpreter.c src/listing.c src/vm.c $(RAYLIB_LIBS) $(RAYLIB_PLATFORM_LIBS)

visualizer: $(VISUALIZER)
	./$(VISUALIZER) $(ORI_FILE)

$(MEISSA): src/meissa.c src/lexer.c src/language_parser.c src/listing.c $(wildcard include/orion/*.h include/orion/*.def.h) assets/NotoSans-VariableFont.ttf
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ src/meissa.c src/lexer.c src/language_parser.c src/listing.c $(RAYLIB_LIBS) $(RAYLIB_PLATFORM_LIBS)

meissa-run: $(MEISSA)
	./$(MEISSA) $(ORI_FILE)

$(TEST_TARGET): tests/test_vm.c src/vm.c include/orion/vm.h include/orion/opcode.def.h
	$(CC) $(CFLAGS) -o $@ tests/test_vm.c src/vm.c

test: $(TEST_TARGET)
	./$(TEST_TARGET)
	$(MAKE) $(LANGUAGE_TEST_TARGET)
	./$(LANGUAGE_TEST_TARGET)

$(LANGUAGE_TEST_TARGET): tests/test_language.c src/lexer.c src/language_parser.c src/interpreter.c src/vm.c $(wildcard include/orion/*.h include/orion/*.def.h)
	$(CC) $(CFLAGS) -o $@ tests/test_language.c src/lexer.c src/language_parser.c src/interpreter.c src/vm.c

# Clean up build artifacts and target binary
clean:
	rm -rf build $(TARGET) $(VISUALIZER) $(MEISSA)

.PHONY: all run visualizer meissa-run test clean
