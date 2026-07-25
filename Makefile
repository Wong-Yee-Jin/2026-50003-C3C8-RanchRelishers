CC       := cc
CFLAGS   := -Wall -Wextra -g -Iinclude
LDFLAGS  := -lsqlite3

# Application sources. corestack/router/template/form_util/oauth are removed in M3;
# until then keep this list pointing only at what actually compiles in the new tree.
SRC := src/main.c src/util.c src/db.c \
       $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
OBJ := $(SRC:.c=.o)
BIN := mini-gh-tracker

# One test binary per module. Each links the module under test plus its deps.
TEST_BINS := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))

.PHONY: all clean test
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test binaries link everything except main.o so a test can drive the library directly.
build/%: tests/%.c src/util.c src/db.c $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@fail=0; for t in $(TEST_BINS); do ./$$t || fail=1; done; exit $$fail

clean:
	rm -f $(OBJ) $(BIN); rm -rf build
