# Mini GitHub Issue Tracker (terminal, C, SQLite)

A small issue tracker that runs entirely in the terminal. There is no server, no browser, and no network connection. Everything is stored in a single SQLite database file.

## Building

Requires a C compiler and `libsqlite3` (no pkg-config, no OpenSSL).

```bash
make
```

This produces the `mini-gh-tracker` binary.

## Running

```bash
./mini-gh-tracker
```

The database path comes from the `DB_PATH` environment variable. If unset, it defaults to `issues.db` in the current directory.

```bash
DB_PATH=/path/to/mydata.db ./mini-gh-tracker
```

The menu lets you create and browse projects, issues, labels, users, and comments.

## Testing

```bash
make test
```

This builds and runs every `tests/test_*.c` file. Tests use an in-memory database, so they do not touch `issues.db`.

## Project layout

```
mini-gh-tracker/
├── Makefile
├── include/
│   ├── db.h, models.h, util.h
│   ├── core/     business logic per module
│   └── ui/menu.h
├── src/
│   ├── main.c    entry point, opens the database and starts the menu
│   ├── util.c    id generation and small string helpers
│   ├── db.c      SQLite CRUD
│   ├── core/     business rules (projects, issues, labels, users, comments)
│   └── ui/menu.c terminal menu front-end
└── tests/        one test file per module, run by make test
```

## Notes

Signing in with GitHub is not wired up yet. Every action in the menu is available without logging in.
