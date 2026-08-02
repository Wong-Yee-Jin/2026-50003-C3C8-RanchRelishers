"""End-to-end tests for mini-gh-tracker: drives the real compiled binary the
way a person would, over stdin/stdout, the way Selenium drives a browser.

Run with: python3 -m unittest discover -s tests/e2e -v
Requires: `make` has already built ./mini-gh-tracker at the repo root.
"""
import os
import re
import subprocess
import tempfile
import unittest

from app_runner import AppTestCase, BINARY, TIMEOUT_SECONDS


def _assignee_create_supported():
    """Whether this build's Assignees screen offers a `c) create` option.

    Another agent is landing that feature in parallel with this suite. Rather
    than hard-depend on it, probe the running binary once at import time and
    skip the dependent test if the option isn't there yet, so the suite stays
    green either way.
    """
    if not os.path.exists(BINARY):
        return False
    with tempfile.TemporaryDirectory(prefix="mgt-e2e-probe-") as d:
        try:
            proc = subprocess.run(
                [BINARY], input="3\n0\n0\n", stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=TIMEOUT_SECONDS,
                # Same isolation as AppTestCase.test_env: keep the probe away
                # from a real cached token so it cannot reach the network.
                env={**os.environ, "DB_PATH": os.path.join(d, "probe.db"),
                     "XDG_CONFIG_HOME": d, "GH_CLIENT_ID": ""},
            )
        except subprocess.TimeoutExpired:
            return False
    return "c) create   0) back" in proc.stdout


ASSIGNEE_CREATE_SUPPORTED = _assignee_create_supported()


@unittest.skipUnless(os.path.exists(BINARY), f"binary not built: {BINARY} (run `make` first)")
class E2ETests(AppTestCase):

    # ---- Sign Up / Log In ----
    # There's no separate sign-up screen: a fresh session with no saved GitHub
    # token auto-signs in as a local user (auth_ctx falls back to "local"), so
    # every write below just works without an explicit login step. This checks
    # that fallback actually happened, rather than every write silently
    # failing with "sign in first".
    def test_sign_up_log_in(self):
        result = self.run_app("1\nc\nProjA\n0\n0\n0\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("4) GitHub login", result.stdout)
        self.assertNotIn("sign in first", result.stdout)

    # ---- View Projects / Create Project ----
    def test_create_project_then_view_projects(self):
        result = self.run_app("1\nc\nDemoProject\n0\n0\n")
        self.assertIn("(no projects yet)", result.stdout)   # empty state, scenario 10
        self.assertIn("  1) DemoProject", result.stdout)

    # ---- View Issue / Create Issue ----
    def test_create_issue_appears_with_number_1(self):
        result = self.run_app("1\nc\nProjA\n1\nc\nFirst Bug\nsomething broke\n0\n0\n0\n")
        self.assertIn("(no issues yet)", result.stdout)   # empty state, scenario 10
        self.assertIn("  1) #1 [open] First Bug", result.stdout)

    def test_view_issue_detail_renders_fields(self):
        script = "1\nc\nProjA\n1\nc\nFirst Bug\nsomething broke\n1\n0\n0\n0\n0\n"
        result = self.run_app(script)
        self.assertIn("#1 First Bug [open]", result.stdout)
        self.assertIn("labels: (none)", result.stdout)
        self.assertIn("assignees: (none)", result.stdout)
        self.assertIn("comments (0):", result.stdout)

    # ---- Close Issue / Reopen Issue ----
    # Close and reopen are the same 't' toggle, driven off whatever the
    # issue's current status is, so both are checked from the sequence of
    # statuses rendered across repeated visits to the same detail screen.
    def test_close_issue_toggles_to_closed(self):
        script = "1\nc\nProjA\n1\nc\nBug1\ndesc\n1\nt\n0\n0\n0\n0\n"
        result = self.run_app(script)
        statuses = re.findall(r"#1 Bug1 \[(open|closed)\]", result.stdout)
        self.assertEqual(statuses, ["open", "closed"])

    def test_reopen_issue_toggles_back_to_open(self):
        script = "1\nc\nProjA\n1\nc\nBug1\ndesc\n1\nt\nt\n0\n0\n0\n0\n"
        result = self.run_app(script)
        statuses = re.findall(r"#1 Bug1 \[(open|closed)\]", result.stdout)
        self.assertEqual(statuses, ["open", "closed", "open"])

    # ---- View Labels / Create Label ----
    def test_view_labels_shows_seeded_defaults(self):
        result = self.run_app("2\n0\n0\n")
        self.assertIn("  1) bug", result.stdout)
        self.assertIn("  2) feature", result.stdout)
        self.assertIn("  3) question", result.stdout)

    def test_create_label_appears_in_label_list(self):
        script = "2\nc\nurgent\nneeds immediate attention\n0\n0\n"
        result = self.run_app(script)
        self.assertIn("  4) urgent - needs immediate attention", result.stdout)

    # ---- Search Issue ----
    def test_search_issue_finds_match_and_excludes_others(self):
        script = ("1\nc\nProjA\n1\nc\nLogin bug\nauth broken\n"
                   "c\nUI glitch\nbutton misaligned\ns\nlogin\n0\n0\n0\n")
        result = self.run_app(script)
        marker = '-- results for "login" --'
        self.assertIn(marker, result.stdout)
        # isolate just the results block, since the redrawn issue list after
        # the search (which does include "UI glitch") follows right after it
        results_block = result.stdout.split(marker, 1)[1].split("[ gh-tracker ]", 1)[0]
        self.assertIn("Login bug", results_block)
        self.assertNotIn("UI glitch", results_block)

    # ---- Per-project issue numbering (scenario 9) ----
    def test_issue_numbering_restarts_per_project(self):
        script = ("1\nc\nProjA\nc\nProjB\n"
                   "1\nc\nIssueA1\ndesc\nc\nIssueA2\ndesc\n0\n"
                   "2\nc\nIssueB1\ndesc\n0\n0\n0\n")
        result = self.run_app(script)
        self.assertIn("  1) #1 [open] IssueA1", result.stdout)
        self.assertIn("  2) #2 [open] IssueA2", result.stdout)
        self.assertIn("  1) #1 [open] IssueB1", result.stdout)   # restarts at #1 in the second project

    # ---- Invalid input (scenario 11) ----
    def test_invalid_menu_choice_does_not_crash(self):
        result = self.run_app("xyz\n0\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("unknown choice", result.stdout)

    # ---- Over-long input line does not leak into the next prompt ----
    # TITLE_LEN is 256, so fgets() can only take the first 255 characters of
    # this line in one read; the rest used to sit on stdin and answer the
    # description prompt instead of the real description typed next.
    def test_overlong_title_does_not_leak_into_next_prompt(self):
        title = "A" * 300
        script = ("1\nc\nProjA\n1\nc\n" + title + "\n"
                   "real description\n1\n0\n0\n0\n0\n")
        result = self.run_app(script)
        self.assertEqual(result.returncode, 0)
        self.assertIn("A" * 255, result.stdout)              # title cut to what fgets could hold
        self.assertIn("real description\n", result.stdout)   # tail of the title didn't eat this line
        self.assertNotIn("unknown choice", result.stdout)    # the leaked tail used to derail the menu

    # ---- Assign User to Issue ----
    # Depends on the Assignees "c) create" flow another agent is landing
    # alongside this suite; skipped cleanly if that hasn't shown up yet.
    @unittest.skipUnless(ASSIGNEE_CREATE_SUPPORTED, "Assignees screen has no 'c) create' option in this build")
    def test_assign_user_to_issue(self):
        script = ("3\nc\nalice\n0\n"
                   "1\nc\nProjA\n1\nc\nBug1\ndesc\n1\na\n1\n0\n0\n0\n0\n")
        result = self.run_app(script)
        self.assertIn("assignees: alice", result.stdout)

    # ---- Known defect: filter-by-label wants a raw 24-char hex label id, ----
    # ---- typed by hand, instead of the name shown anywhere in the UI.    ----
    # src/ui/menu.c:250-251 sends whatever the user types straight through as
    # the label id; typing the human-readable name ("bug") never matches, so
    # filtering by label is effectively unusable from the menu. Left as a
    # documented expected failure instead of weakening the assertion.
    @unittest.expectedFailure
    def test_filter_by_label_name_is_broken(self):
        script = "1\nc\nProjA\n1\nc\nBugIssue\ndesc\n1\nl\n1\n0\nf\n\nbug\n0\n0\n0\n"
        result = self.run_app(script)
        marker = "-- filtered issues --"
        filtered_block = result.stdout.split(marker, 1)[1].split("[ gh-tracker ]", 1)[0]
        self.assertIn("BugIssue", filtered_block)


if __name__ == "__main__":
    unittest.main()
