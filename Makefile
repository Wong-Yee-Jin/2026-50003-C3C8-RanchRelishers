CC       := cc
CFLAGS   := -Wall -Wextra -g -Iinclude
LDFLAGS  := -lsqlite3 -lcurl

# Application sources. corestack/router/template/form_util/oauth are removed in M3;
# until then keep this list pointing only at what actually compiles in the new tree.
SRC := src/main.c src/util.c src/db.c src/json.c src/token_store.c src/github.c src/render.c src/assets.c \
       $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
OBJ := $(SRC:.c=.o)
BIN := mini-gh-tracker

# Unity (ThrowTheSwitch) is vendored under tests/vendor so `make test` needs
# nothing installed on the machine. Only the test build sees it.
UNITY_SRC   := tests/vendor/unity/unity.c
TEST_CFLAGS := $(CFLAGS) -Itests/vendor/unity

# One test binary per module. Each links the module under test plus its deps.
TEST_BINS := $(patsubst tests/%.c,build/%,$(wildcard tests/test_*.c))

.PHONY: all clean test e2e check
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test binaries link everything except main.o so a test can drive the library directly.
build/%: tests/%.c $(UNITY_SRC) src/util.c src/db.c src/json.c src/token_store.c src/github.c src/render.c src/assets.c $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
	@mkdir -p build
	$(CC) $(TEST_CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@fail=0; for t in $(TEST_BINS); do ./$$t || fail=1; done; exit $$fail

# End-to-end tests drive the built binary through piped stdin, so they need the
# real program rather than the test objects. Stdlib unittest only, nothing to
# install.
e2e: $(BIN)
	python3 -m unittest discover -s tests/e2e -v

check: test e2e

clean:
	rm -f $(OBJ) $(BIN); rm -rf build
