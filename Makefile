CC       := cc
CFLAGS   := -Wall -Wextra -g -Iinclude
LDFLAGS  := -lsqlite3 -lcurl

# make SANITIZE=1 <target> turns on ASan+UBSan for both the app and the tests.
ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
endif

# Application sources: entry point and service/util modules, plus everything
# under src/core (business logic) and src/ui (screens).
# Standalone build: no corestack/tetrish_gate here -- this version starts
# on its own and never checks for a live tetrisd.
SRC := src/main.c src/util.c src/db.c src/json.c src/token_store.c src/github.c src/render.c src/assets.c \
       src/dotenv.c \
       $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
OBJ := $(SRC:.c=.o)
BIN := mini-gh-tracker

# Unity (ThrowTheSwitch) is vendored under tests/vendor so `make test` needs
# nothing installed on the machine. Only the test build sees it.
UNITY_SRC   := tests/vendor/unity/unity.c
TEST_CFLAGS := $(CFLAGS) -Itests/vendor/unity

# Tests belonging to the corestack half of the project (tetriSH), kept here
# for reference but not buildable from this tree: test_ring/stress_ring need
# src/tetrisd/ring.c and stub_client needs libtetrissh and libhtttp, none of
# which live in this repo. They build and pass in the corestack project,
# whose Makefile has rules for them. Excluded by name rather than by moving
# the files, so the copies stay where whoever committed them expects.
CORESTACK_TESTS := tests/test_ring.c

# One test binary per module. Each links the module under test plus its deps.
TEST_SRC  := $(filter-out $(CORESTACK_TESTS),$(wildcard tests/test_*.c))
TEST_BINS := $(patsubst tests/%.c,build/%,$(TEST_SRC))
TEST_OBJS := $(patsubst tests/%.c,build/tests/%.o,$(TEST_SRC))

# Sources every test binary links against, besides its own tests/test_*.o.
COMMON_TEST_SRC := $(UNITY_SRC) src/util.c src/db.c src/json.c src/token_store.c src/github.c src/render.c src/assets.c \
                    $(wildcard src/core/*.c) $(wildcard src/ui/*.c)
COMMON_TEST_OBJ := $(addprefix build/,$(COMMON_TEST_SRC:.c=.o))

.PHONY: all clean test e2e check run
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
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

# Test objects get their own .o under build/ (TEST_CFLAGS instead of CFLAGS) rather
# than compiling and linking every source in one cc call: a single invocation given
# several sources only keeps the dependency file from the last one compiled, so a
# header change picked up by an earlier source would go undetected.
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -MMD -MP -c $< -o $@

-include $(COMMON_TEST_OBJ:.o=.d) $(TEST_OBJS:.o=.d)

# Test binaries link everything except main.o so a test can drive the library directly.
# .SECONDARY keeps the objects above around between runs: since both rules that build
# them are pattern rules, make would otherwise treat them as scratch files of the
# build/test_x chain and delete each one right after linking, forcing a full rebuild
# on every `make test` even with nothing changed.
.SECONDARY: $(COMMON_TEST_OBJ) $(TEST_OBJS)
build/%: build/tests/%.o $(COMMON_TEST_OBJ)
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
	rm -f $(OBJ) $(BIN) $(OBJ:.o=.d); rm -rf build
