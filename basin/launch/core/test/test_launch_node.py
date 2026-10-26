"""Runs the launch node the way it is deployed.

Starts `//basin/launch/core:launch_node_exe` (the node plus its Python environment) pointed at
test/sleeper.prod.py, then checks that the node starts the child process the launch file describes
and that shutting the node down (SIGINT) takes the child down with it and exits cleanly.
"""
import os
import re
import signal
import subprocess
import threading
import time
import unittest

import ros_test_domain
from python.runfiles import runfiles

STARTUP_TIMEOUT_S = 30
SHUTDOWN_TIMEOUT_S = 15

# What the node logs when it can't load a launch file. It keeps running afterwards, so without this
# a broken Python environment would only fail the test once the startup timeout expired.
LOAD_FAILURE = re.compile(r"Failed to load .* for reason")


def _process_alive(pid):
    """True if `pid` exists and isn't just an unreaped zombie."""
    try:
        with open(f"/proc/{pid}/stat") as f:
            # "pid (comm) state ..." -- comm may itself contain spaces/parens, so split on the last ')'
            state = f.read().rsplit(")", 1)[1].split()[0]
    except FileNotFoundError:
        return False
    return state != "Z"


class LaunchNodeTest(unittest.TestCase):
    def setUp(self):
        r = runfiles.Create()
        workspace = os.environ.get("TEST_WORKSPACE", "_main")
        launcher = r.Rlocation(f"{workspace}/basin/launch/core/launch_node_exe")
        launch_file = r.Rlocation(f"{workspace}/basin/launch/core/test/sleeper.prod.py")
        self.assertTrue(launcher and os.path.exists(launcher), "launcher not in runfiles")
        self.assertTrue(launch_file and os.path.exists(launch_file), "launch file not in runfiles")

        # Concurrent test processes on one DDS domain deadlock each other's node construction.
        domain = ros_test_domain.claim()
        self.addCleanup(domain.close)  # LIFO: runs after _kill_tree, once nothing uses the domain

        self._output = []
        self._output_changed = threading.Condition()
        self._proc = subprocess.Popen(
            [launcher, "--ros-args", "-p", f"launch_file:={launch_file}", "-p", "launch_package:=''"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=domain.env(dict(os.environ, **r.EnvVars())),
            start_new_session=True,  # so a failed test can take the whole process tree down
        )
        self.addCleanup(self._kill_tree)
        threading.Thread(target=self._pump_output, daemon=True).start()

    def _pump_output(self):
        for line in self._proc.stdout:
            with self._output_changed:
                self._output.append(line.rstrip("\n"))
                self._output_changed.notify_all()

    def _kill_tree(self):
        try:
            os.killpg(self._proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self._proc.wait()
        self._proc.stdout.close()

    def _wait_for_output(self, pattern, timeout):
        """Blocks until a line matching `pattern` has been logged; returns its match."""
        deadline = time.monotonic() + timeout
        seen = 0
        with self._output_changed:
            while True:
                for line in self._output[seen:]:
                    match = re.search(pattern, line)
                    if match:
                        return match
                    if LOAD_FAILURE.search(line):
                        self.fail("launch file failed to load:\n" + "\n".join(self._output))
                seen = len(self._output)
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self._proc.poll() is not None and seen == len(self._output):
                    break
                self._output_changed.wait(min(remaining, 0.5))
        self.fail(f"never saw /{pattern}/ in output:\n" + "\n".join(self._output))

    def test_node_starts_and_stops_child(self):
        # The node evaluated the launch file with its embedded interpreter, and started what it described.
        pid = int(
            self._wait_for_output(
                r"Started launch_node_test_sleeper with PID: (\d+)", STARTUP_TIMEOUT_S
            ).group(1)
        )
        self.assertTrue(_process_alive(pid), "child died on its own")
        self.assertNotIn("Failed to load", "\n".join(self._output))

        os.killpg(self._proc.pid, signal.SIGINT)
        try:
            returncode = self._proc.wait(timeout=SHUTDOWN_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            self.fail("node did not exit after SIGINT:\n" + "\n".join(self._output))

        self.assertEqual(returncode, 0, "node exited uncleanly:\n" + "\n".join(self._output))
        self._wait_for_output(r"Shutting down 1 processes", 1)

        # The point of the whole system: nothing is left running once the launch node is gone.
        deadline = time.monotonic() + 5
        while _process_alive(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertFalse(_process_alive(pid), f"child {pid} outlived the launch node")


if __name__ == "__main__":
    unittest.main()
