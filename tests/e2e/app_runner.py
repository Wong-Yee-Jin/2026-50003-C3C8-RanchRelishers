"""Shared plumbing for driving the compiled mini-gh-tracker binary end to end.

The app reads keypresses from stdin with fgets() and returns false on EOF, so
a whole session (menu choices, typed names, back-outs) can be scripted as one
newline-joined string piped into the process. DB_PATH points it at a scratch
sqlite file so tests never touch the real issues.db.

XDG_CONFIG_HOME is redirected at the scratch directory for the same reason.
Without it the app finds the developer's own cached GitHub token at startup,
and github_resume_session() then makes a live API call to revalidate it. That
would make every test depend on the network and on who is running it, and the
request outlives our subprocess timeout, so the suite would fail on any machine
where someone had signed in.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(REPO_ROOT, "mini-gh-tracker")
TIMEOUT_SECONDS = 5


class AppTestCase(unittest.TestCase):
    """Base class: a fresh scratch database per test, plus a one-call run_app()."""

    def setUp(self):
        self.scratch_dir = tempfile.mkdtemp(prefix="mgt-e2e-")
        self.db_path = os.path.join(self.scratch_dir, "test.db")

    def tearDown(self):
        # rmtree instead of tracking sqlite's -wal/-shm/-journal siblings by name.
        shutil.rmtree(self.scratch_dir, ignore_errors=True)

    def _run_env(self, db_path=None):
        """Environment that keeps a run inside the scratch directory.

        Leading underscore matters: unittest collects any method named test_*
        as a test case, so calling this test_env silently added a fake passing
        test to every subclass.
        """
        return {
            **os.environ,
            "DB_PATH": db_path or self.db_path,
            "XDG_CONFIG_HOME": self.scratch_dir,
            # Without a client id the device flow fails immediately instead of
            # reaching the network, which is what we want for an offline test.
            "GH_CLIENT_ID": "",
        }

    def run_app(self, stdin_script, db_path=None):
        """Pipes stdin_script into the binary and returns the completed process.

        stdin_script is the full session as one string, e.g. "1\\nc\\nDemo\\n0\\n0\\n".
        """
        return subprocess.run(
            [BINARY],
            input=stdin_script,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=TIMEOUT_SECONDS,
            env=self._run_env(db_path),
        )
