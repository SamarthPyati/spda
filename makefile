CC := cc
CFLAGS := -Wall -Wextra -std=c17
LDFLAGS := -lm

# Directories
SRC_DIR 	:= .
TEST_DIR 	:= tests
BIN_DIR 	:= bin
BUILD_DIR 	:= build

# Source files
SRC 	:= $(SRC_DIR)/spda.c
HEADER 	:= $(SRC_DIR)/spda.h
OBJ 	:= $(SRC_DIR)/spda.o
DLIB 	:= $(BUILD_DIR)/libspda.so

# Executables
BASIC_TEST 	:= $(BIN_DIR)/basic_test
MAIN_TEST 	:= $(BIN_DIR)/main_test

# Targets
.PHONY: all clean build_lib

all: $(BASIC_TEST) $(MAIN_TEST) $(UTILITY_TEST)

$(BASIC_TEST): $(SRC) $(TEST_DIR)/basic.c $(HEADER) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(TEST_DIR)/basic.c -o $@ $(LDFLAGS)

$(MAIN_TEST): $(SRC) $(TEST_DIR)/test.c $(HEADER) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(TEST_DIR)/test.c -o $@ $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

PLAYGROUND_FILE := playground/play.c
play: $(PLAYGROUND_FILE)
	$(CC) $(CFLAGS) -I./ -o ./playground/bin/play $< $(SRC)
	./playground/bin/play

BUILD_DL_FLAGS := -Wall -Wextra -Werror -std=c17 -shared -O3
build_lib: $(BUILD_DIR) | $(BUILD_DIR)
	$(CC) $(BUILD_DL_FLAGS) -o $(DLIB) -fPIC $(SRC)

test: $(MAIN_TEST)
	./$<

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)
