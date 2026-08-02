# Mini GitHub Issue Tracker (terminal, C, SQLite)

A small issue tracker that runs in the terminal. There is no server and no browser, but it does reach the network: signing in with GitHub uses the device flow, and once signed in the app calls the live GitHub API to fetch your profile and list your repos. Everything else is stored in a single SQLite database file, and the tracker is fully usable offline under a local identity if you skip GitHub login.

## Building

Requires a C compiler, `libsqlite3`, and `libcurl` (GitHub login and repo listing go over HTTPS via curl).

On Debian/Ubuntu:

```bash
sudo apt install libsqlite3-dev libcurl4-openssl-dev
```

On macOS, the Xcode SDK already ships `sqlite3` and `curl`, so nothing extra needs installing. `brew install sqlite` would not help even if you ran it: Homebrew keeps its sqlite keg-only and off the default include path.

Then:

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

The menu lets you create and browse projects, issues, labels, users, and comments. The projects screen shows a small progress meter next to each project (closed issues out of the total), and every screen ends with a help row listing what its keys do.

### Terminal presentation

Color depth is detected automatically: `COLORTERM=truecolor` or `24bit` gives full color, a `TERM` containing `256color` gives 256 colors, anything else falls back to 16 colors. `NO_COLOR` (any non-empty value) disables color outright regardless of what the terminal reports.

The app switches to the terminal's alternate screen on start and restores your original screen on exit, including on Ctrl-C. Borders and meters use box-drawing and block characters when the locale is UTF-8, and fall back to plain ASCII otherwise.

None of this shows up when the program is not attached to a terminal: piped or scripted runs, including the e2e suite and any shell script, get plain undecorated text with no escape codes at all, so their output stays stable.

### Environment variables

- `DB_PATH` - path to the SQLite database file. Defaults to `issues.db`.
- `GH_CLIENT_ID` - OAuth app client id for GitHub's device flow. Required to use menu item 4 (GitHub login); without it, login fails immediately and the tracker stays on the local identity.
- `GH_SCOPE` - OAuth scope requested during login. Defaults to `read:user`.
- `NO_COLOR` - when set to any non-empty value, disables the terminal color/highlight codes.
- `XDG_CONFIG_HOME` - where the cached GitHub token is kept. Falls back to `$HOME/.config` when unset.

### Where the GitHub token is stored

After a successful login the access token is written to `$XDG_CONFIG_HOME/mini-gh-tracker/token`, or `~/.config/mini-gh-tracker/token` when `XDG_CONFIG_HOME` is not set. The directories are created with mode 0700 and the file with mode 0600, so no other user on the machine can read it. The token is never written to the database.

The unit tests (`make test`) and the end-to-end tests (`make e2e`) both redirect `XDG_CONFIG_HOME` at a scratch directory, so running them neither reads nor overwrites a token you have saved, and never reaches the network.

## Testing

```bash
make test
```

This builds and runs every `tests/test_*.c` file against an in-memory database, so it never touches `issues.db`.

```bash
make e2e
```

This drives the compiled binary itself through piped stdin, the way a person would use it. It needs a build first, which the `e2e` target does for you.

```bash
make check
```

Runs both suites, `make test` then `make e2e`.

Any of the three can run with ASan and UBSan turned on:

```bash
make clean && make SANITIZE=1 test
```

Start from `make clean` first: object files are keyed on mtime, not on which flags built them, so a plain `make SANITIZE=1 test` after an unsanitized build reuses the old objects and reports a pass without the sanitizers having seen any of the code.

## Project layout

```
mini-gh-tracker/
├── Makefile
├── .clangd         editor include paths, kept in sync with the real build flags
├── include/
│   ├── db.h, models.h, util.h, json.h, token_store.h, render.h, assets.h
│   ├── core/          business logic per module
│   └── ui/menu.h
├── src/
│   ├── main.c         entry point, opens the database and starts the menu
│   ├── util.c         id generation and small string helpers
│   ├── db.c           SQLite CRUD
│   ├── github.c       GitHub device flow login and API calls (libcurl)
│   ├── json.c         minimal JSON parser for GitHub API responses
│   ├── token_store.c  reads and writes the cached GitHub token (0600)
│   ├── render.c       terminal color/size decisions and the splash screen
│   ├── assets.c       embedded ASCII wordmark art
│   ├── core/          business rules (projects, issues, labels, users, comments)
│   └── ui/menu.c      terminal menu front-end
└── tests/             one test file per module, run by make test
```

## Notes

Signing in with GitHub is menu item 4, backed by the device flow in `github_login`. Every action in the menu is also available signed in locally, without GitHub, so the tracker stays usable with no network at all.
