# Two different version's of clang clash on my machine - Apple Clang and LLVM Clang
ifeq ($(origin cc), default)
	CC := clang
endif

CFLAGS := -Wall -Wextra -std=c17
LDFLAG := -lm

ifeq ($(DEBUG),1)
# Homebrew LLVM's ASan hangs on this macOS
	CC      := /usr/bin/clang          
	CFLAGS  += -g -O0 -fsanitize=address,undefined
	LDFLAGS += -fsanitize=address,undefined
	OUT     := bin/debug
else
	CFLAGS  += -O2
	OUT     := bin/release
endif

# Directories
SRC_DIR 	:= .
TEST_DIR 	:= tests
BIN_DIR 	:= bin

# Source files
HEADER 	:= $(SRC_DIR)/spda.h
OBJ 	:= $(BIN_DIR)/spda.o
DLIB 	:= $(BIN_DIR)/libspda.so

# Executables
BASIC_TEST 	:= $(BIN_DIR)/basic_test
MAIN_TEST 	:= $(BIN_DIR)/main_test

# Targets
.PHONY: all build clean build_lib

all: build $(BASIC_TEST) $(MAIN_TEST) $(UTILITY_TEST)

build: $(HEADER)
	$(CC) $(CFLAGS) -x c -DSPDA_IMPLEMENTATION -c $< -o $(OBJ)

$(BASIC_TEST): $(TEST_DIR)/basic.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ 

$(MAIN_TEST): $(TEST_DIR)/test.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR):
	mkdir -p $@

PLAYGROUND_FILE := playground/play.c
play: $(PLAYGROUND_FILE) $(OBJ)
	$(CC) $(CFLAGS) -I./ -o ./playground/bin/play $< $(OBJ)
	./playground/bin/play

BUILD_DL_FLAGS := -Wall -Wextra -Werror -std=c17 -shared -O3
build_lib: $(BIN_DIR) $(OBJ)
	$(CC) $(BUILD_DL_FLAGS) -o $(DLIB) -fPIC $(OBJ)

test: $(MAIN_TEST)
	./$<

clean:
	@rm -rf $(BIN_DIR)
