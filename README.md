# Mini GitHub Issue Tracker (terminal, C, SQLite)

A small issue tracker that runs in the terminal. There is no server and no browser, but it does reach the network: signing in with GitHub uses the device flow, and once signed in the app calls the live GitHub API to fetch your profile and list your repos. Everything else is stored in a single SQLite database file, and the tracker is fully usable offline under a local identity if you skip GitHub login.

## Building

Requires a C compiler, `libsqlite3`, and `libcurl` (GitHub login and repo listing go over HTTPS via curl).

On Debian/Ubuntu:

```bash
sudo apt install libsqlite3-dev libcurl4-openssl-dev
```

On macOS (with Homebrew):

```bash
brew install sqlite curl
```

Then:

```bash
make
```

This produces the `mini-gh-tracker` binary.

## Running

```bash
./mini-gh-tracker
```

or

```bash
make run
```

The database path comes from the `DB_PATH` environment variable. If unset, it defaults to `issues.db` in the current directory.

```bash
DB_PATH=/path/to/mydata.db ./mini-gh-tracker
```

The menu lets you create and browse projects, issues, labels, users, and comments.

### Environment variables

- `DB_PATH` - path to the SQLite database file. Defaults to `issues.db`.
- `GH_CLIENT_ID` - OAuth app client id for GitHub's device flow. Required to use menu item 4 (GitHub login); without it, login fails immediately and the tracker stays on the local identity.
- `GH_SCOPE` - OAuth scope requested during login. Defaults to `read:user`.
- `NO_COLOR` - when set to any non-empty value, disables the terminal color/highlight codes.
- `XDG_CONFIG_HOME` - where the cached GitHub token is kept. Falls back to `$HOME/.config` when unset.

### Where the GitHub token is stored

After a successful login the access token is written to `$XDG_CONFIG_HOME/mini-gh-tracker/token`, or `~/.config/mini-gh-tracker/token` when `XDG_CONFIG_HOME` is not set. The directories are created with mode 0700 and the file with mode 0600, so no other user on the machine can read it. The token is never written to the database.

Both test suites redirect `XDG_CONFIG_HOME` at a scratch directory, so running them neither reads nor overwrites a token you have saved, and never reaches the network.

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
│   ├── github.c  GitHub device flow login and API calls (libcurl)
│   ├── core/     business rules (projects, issues, labels, users, comments)
│   └── ui/menu.c terminal menu front-end
└── tests/        one test file per module, run by make test
```

## Notes

Signing in with GitHub is menu item 4, backed by the device flow in `github_login`. Every action in the menu is also available signed in locally, without GitHub, so the tracker stays usable with no network at all.
