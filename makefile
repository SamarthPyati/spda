# Compiler selection
# Apple clang (/usr/bin/clang) and Homebrew LLVM clang both exist on this machine.
# Homebrew LLVM's ASan runtime deadlocks at startup on this macOS, so on Darwin
# default to Apple clang. An explicit `make CC=...` always wins.
ifeq ($(origin CC),default)
  ifeq ($(shell uname),Darwin)
    CC := /usr/bin/clang
  else
    CC := cc
  endif
endif

CFLAGS  := -Wall -Wextra -std=c17
LDFLAGS :=

ifeq ($(DEBUG),1)
  SAN     := -fsanitize=address,undefined
  CFLAGS  += -g -O0 $(SAN)
  LDFLAGS += $(SAN)
  OUT     := bin/debug
else
  CFLAGS  += -O2
  OUT     := bin/release
endif

# Files
HEADER := spda.h
OBJ    := $(OUT)/spda.o
TEST   := $(OUT)/test

ifeq ($(shell uname),Darwin)
  DLIB := $(OUT)/libspda.dylib
else
  DLIB := $(OUT)/libspda.so
endif

# Targets
.PHONY: all build build_lib test cleanup

all: build $(TEST)

build: $(OBJ)

build_lib: $(DLIB)

test: $(TEST) ; $(TEST) $(T)

$(OUT):
	mkdir -p $@

# Header compiles standalone as the implementation translation unit
$(OBJ): $(HEADER) | $(OUT)
	$(CC) $(CFLAGS) -fPIC -x c -DSPDA_IMPLEMENTATION -c $< -o $@

$(TEST): tests/test.c $(HEADER) | $(OUT)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(DLIB): $(OBJ)
	$(CC) -shared $< -o $@ $(LDFLAGS)

cleanup:
	rm -rf bin
