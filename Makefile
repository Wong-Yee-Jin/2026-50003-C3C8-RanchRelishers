CC       := cc
CFLAGS   := -Wall -Wextra -g -Iinclude
LDFLAGS  := -lsqlite3 -lcurl

# Application sources. router/template/form_util/oauth are removed in M3.
# Standalone build: no corestack/tetrish_gate here -- this version starts
# on its own and never checks for a live tetrisd.
SRC := src/main.c src/util.c src/db.c src/json.c src/token_store.c src/github.c src/render.c src/assets.c \
       src/dotenv.c \
       $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
OBJ := $(SRC:.c=.o)
BIN := mini-gh-tracker

# One test binary per module. Each links the module under test plus its deps.
TEST_BINS := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))

.PHONY: all clean test run
all: $(BIN)

# `make run`: builds quietly (a sub-make with -s, so no "cc -Wall ..." lines)
# and execs the binary, so all you see is mini-gh-tracker's own output --
# the menu, not the build log. Plain `make` still prints compile commands
# as usual.
run:
	@$(MAKE) --no-print-directory -s $(BIN)
	@./$(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test binaries link everything except main.o so a test can drive the library directly.
build/%: tests/%.c src/util.c src/db.c src/json.c src/token_store.c src/github.c src/render.c src/assets.c $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@fail=0; for t in $(TEST_BINS); do ./$$t || fail=1; done; exit $$fail

clean:
	rm -f $(OBJ) $(BIN); rm -rf build
