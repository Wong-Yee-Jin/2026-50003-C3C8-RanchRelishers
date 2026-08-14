"""End-to-end tests for mini-gh-tracker: drives the real compiled binary the
way a person would, over stdin/stdout, the way Selenium drives a browser.

Run with: python3 -m unittest discover -s tests/e2e -v
Requires: `make` has already built ./mini-gh-tracker at the repo root.

Scope note (updated): the main menu is now gated on an actual GitHub
identity. A fresh session with no saved token, and no GH_CLIENT_ID (this
harness deliberately runs offline -- see app_runner.py's docstring), sees
exactly two options: "1) GitHub login" and "0) Quit". Every other screen
(Projects, Labels, Assignees, and everything reachable from them: issue
create/search, label/assignee-at-creation, the open/closed toggle) lives
behind that gate and is therefore no longer reachable from an offline e2e
run at all -- there is no local/offline fallback identity that satisfies
"signed in" for menu purposes (see menu.c's draw_main / menu_run docstring:
"'Signed in' here means an actual GitHub identity, not the local fallback").

That CRUD behavior didn't lose its test coverage, it moved: it's exercised
directly at the C level in tests/test_project_service.c,
tests/test_issue_service.c, tests/test_label_service.c, and
tests/test_user_service.c, which call the service layer (project_service_
create(), issue_service_create(), etc.) after auth_ctx_set_user() rather
than going through the login-gated menu. Those are the right place for it
now: they test the same logic without needing a real GitHub sign-in, and
they always ran (they're part of `make test`, not `make e2e`).

What's left here is only what's genuinely reachable pre-login: the gated
main menu itself, the "unknown choice" path, and what happens when someone
tries to log in without GH_CLIENT_ID configured.
"""
import os
import unittest

from app_runner import AppTestCase, BINARY


@unittest.skipUnless(os.path.exists(BINARY), f"binary not built: {BINARY} (run `make` first)")
class E2ETests(AppTestCase):

    # ---- Logged-out main menu ----
    # This is the regression guard for the gate itself: a fresh session with
    # no saved token must show exactly "1) GitHub login" and "0) Quit" --
    # never Projects/Labels/Assignees, which would mean the gate silently
    # stopped applying.
    def test_logged_out_menu_shows_only_login_and_quit(self):
        result = self.run_app("0\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("1) GitHub login", result.stdout)
        self.assertIn("0) Quit", result.stdout)
        self.assertNotIn("Projects", result.stdout)
        self.assertNotIn("Labels", result.stdout)
        self.assertNotIn("Assignees", result.stdout)

    # ---- Invalid input (scenario 11) ----
    def test_invalid_menu_choice_does_not_crash(self):
        result = self.run_app("xyz\n0\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("unknown choice", result.stdout)

    # ---- Attempting to log in without GH_CLIENT_ID configured ----
    # app_runner.py sets GH_CLIENT_ID="" specifically so the device flow
    # fails immediately instead of reaching the network -- this is what an
    # offline run of that failure path looks like, and it must not crash or
    # hang waiting on a request that will never resolve.
    def test_login_without_client_id_fails_without_hanging(self):
        result = self.run_app("1\n0\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Set GH_CLIENT_ID and enable device flow on your GitHub OAuth app", result.stdout)
        # still back at the same gated menu afterward, not stuck or crashed
        self.assertIn("1) GitHub login", result.stdout)

    # ---- Clean shutdown on EOF ----
    # A closed stdin (piped script running out, or ctrl-D) must be treated
    # like Quit rather than looping forever or exiting non-zero.
    def test_closed_stdin_exits_cleanly(self):
        result = self.run_app("")
        self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
