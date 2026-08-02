# End-to-end tests

Drives the compiled `mini-gh-tracker` binary from the outside, over stdin/stdout,
the same way Selenium or Cypress drives a browser. This is separate from the
per-module unit tests in `tests/` (which link against the C source directly);
these instead exercise the actual menu loop a person sees.

## Running

Build first, then run:

```
make
python3 -m unittest discover -s tests/e2e -v
```

Pure standard library (`unittest`, `subprocess`, `tempfile`) — no pip installs.
Each test gets its own scratch sqlite database via `DB_PATH` and cleans it up
in `tearDown`, so tests are independent and leave nothing behind. Every
subprocess call has a timeout so a hang fails the test instead of blocking.

## What it covers

The ten use cases: Sign Up / Log In, View Projects, Create Project, View Issue,
Create Issue, Close Issue, Reopen Issue, View Labels, Search Issue, and Assign
User to Issue — plus Create Label, per-project issue numbering (each project's
issues start again at #1), empty states (`(no projects yet)`, `(no issues yet)`),
and invalid menu input not crashing the app.

`test_assign_user_to_issue` is guarded with `unittest.skipUnless` on a probe of
the Assignees screen's own text, since the `c) create` username flow it needs
was landing in parallel with this suite.

## Known defects (not weakened, marked instead)

- `test_filter_by_label_name_is_broken` is marked `@unittest.expectedFailure`.
  The issue filter's label field (`src/ui/menu.c:250-251`) is passed straight
  through as a label id, but no screen in the app ever shows a label's id to
  the user — only its name. Typing the name you can actually see (e.g. "bug")
  never matches, so filtering by label is effectively unusable from the menu.
  Repro: `printf '1\nc\nP\n1\nc\nI\nd\n1\nl\n1\n0\nf\n\nbug\n0\n0\n0\n' | DB_PATH=/tmp/x.db ./mini-gh-tracker`
- Comments are stored with no author field (`comment_t` in `include/models.h`
  only carries `id` and `text`), so the detail screen can never attribute a
  comment to whoever wrote it. Not covered by a dedicated test since it's a
  missing field rather than a behavior to assert on.
