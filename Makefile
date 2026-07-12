CC       := gcc
CFLAGS   := -Wall -Wextra -g -Iinclude $(shell pkg-config --cflags libmongoc-1.0)
LDFLAGS  := $(shell pkg-config --libs libmongoc-1.0) -lssl -lcrypto

SRC := $(wildcard src/*.c) $(wildcard src/corestack/*.c) $(wildcard src/handlers/*.c)
OBJ := $(SRC:.c=.o)
BIN := mini-gh-tracker

.PHONY: all clean certs run

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

certs:
	./certs/generate_certs.sh

run: all
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN)
