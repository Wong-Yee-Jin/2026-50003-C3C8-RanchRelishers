# Mini GitHub Issue Tracker (with C programming language and MongoDB)

A small issue tracker where **every line of both frontend and backend is C**. The C backend generates the HTML the browser renders, over a TLS-secured HTTP-like connection, and persists everything to MongoDB.

## Register / log in / log out via GitHub

Signing in is "Sign in with GitHub". The same click either creates an account (first time a given GitHub user id shows up) or logs into the existing one (every time after).

- **`GET /login`** -- a "Continue with GitHub" landing page.
- **`GET /auth/github`** -- redirects to GitHub's OAuth consent screen (`scope=read:user`, i.e. read-only access to the public profile; nothing is ever posted or changed on the person's GitHub account). A random CSRF `state` value is minted and stashed in a short-lived `oauth_state` cookie.
- **`GET /auth/github/callback`** -- GitHub redirects back here with a `code`. The handler checks `state` against the `oauth_state` cookie, exchanges the code for an access token, fetches the public profile from `api.github.com/user`, and upserts a `users` row keyed on the immutable GitHub user id (`db_user_upsert_github()` in `src/db.c` 
-- this is the "register or log in" step). A first-party session is then created (`auth_start_session()`) and a `session_token` cookie is set.
- **`POST /logout`** -- deletes that session server-side and clears the `session_token` cookie. This never talks to github.com at all, so it has **no effect on the person's actual github.com login/session** 
-- only this app's own login state ends.

The nav bar shows "Log in" when signed out, or the person's GitHub avatar/username plus a "Log out" button when signed in (`render_page()` in `src/template.c`, backed by `auth_get_current_user()`). GitHub-linked accounts also automatically appear in the `/users` assignee directory (Tier 2), replacing the placeholder note that used to say account-linking wasn't implemented yet.

Nothing else in the app currently *requires* being logged in -- creating projects/issues/etc. is still open to anyone who can reach the port, matching the existing "no auth" simplification below. This feature adds the identity/session plumbing; gating specific actions behind `auth_get_current_user()` would be a small follow-up in each handler that wants it.

## Architecture

```
Browser
   │  HTTPS (TLS 1.2+)
   ▼
main.c ── accept() loop, forks per connection
   │
   ▼
corestack/secure_session.c   [SWAP POINT #1]  handshake + encrypted I/O
   │
   ▼
corestack/htttp.c            [SWAP POINT #2]  HTTP parse/serialize
   │
   ▼
router.c                      method+path -> handler dispatch
   │
   ▼
src/handlers/*.c              business logic per use case
   │            │
   ▼            ▼
db.c        template.c        MongoDB CRUD          HTML generation ("frontend")
   │
   ▼
MongoDB
```

## Building & running

```bash
# 1. install deps (Ubuntu/Debian)
sudo apt-get install build-essential pkg-config libmongoc-dev libbson-dev libssl-dev

# 2. generate a self-signed dev cert
make certs

# 3. build
make

# 4. start MongoDB (refer to https://www.mongodb.com/docs/v7.0/tutorial/install-mongodb-on-ubuntu/)
sudo systemctl start mongod

# 5. run (needs a reachable MongoDB; defaults to mongodb://localhost:27017)
MONGO_URI=mongodb://localhost:27017 ./mini-gh-tracker 8443
```

Then open `https://localhost:8443` in a browser (accept the self-signed cert warning in dev).

## Project layout

```
mini-gh-tracker/
├── Makefile
├── certs/generate_certs.sh
├── docker/Dockerfile
├── include/
│   ├── corestack/secure_session.h   [SWAP POINT #1]
│   ├── corestack/htttp.h            [SWAP POINT #2]
│   ├── db.h, models.h, router.h, template.h, handlers.h, form_util.h
├── src/
│   ├── main.c
│   ├── corestack/secure_session.c   [SWAP POINT #1]
│   ├── corestack/htttp.c            [SWAP POINT #2]
│   ├── db.c, router.c, template.c, form_util.c
│   └── handlers/
│       ├── project_handlers.c   (UC1, UC2)
│       ├── issue_handlers.c     (UC3, UC4, UC8, UC9, UC10, UC11)
│       ├── comment_handlers.c   (UC5)
│       ├── label_handlers.c     (UC6, UC7 -- fixed catalog, multi-assign)
│       ├── user_handlers.c      (Tier 2: assignees)
│       └── time_handlers.c      (Tier 2: time tracking)
```

`form_util.c` is a small standalone helper that parses repeated form keys, e.g. several checked `label_id` checkboxes in one submit -- `htttp_form_get()` only ever returns the first match for a key, which is enough for single text fields but not multi-select.
