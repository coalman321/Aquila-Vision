"""Runs the continuous logger the way it is deployed: the logger_node binary as its own process.

Points the node at a scratch log directory and checks that it comes up, discovers and subscribes to
its configured topic (which needs the typesupport libraries), writes MCAP files (which needs the
rosbag2 storage plugins), and that SIGINT shuts it down cleanly. It then runs snapshot_extractor_cli
against the directory the logger produced. A plain cc_test linking :logger would exercise none of
the binary's runtime packaging.
"""
import glob
import os
import re
import signal
import subprocess
import tempfile
import threading
import time
import unittest

import ros_test_domain
from python.runfiles import runfiles

STARTUP_TIMEOUT_S = 30
SHUTDOWN_TIMEOUT_S = 15


class LoggerNodeTest(unittest.TestCase):
    def setUp(self):
        self._runfiles = r = runfiles.Create()
        workspace = os.environ.get("TEST_WORKSPACE", "_main")
        logger = r.Rlocation(f"{workspace}/basin/logging/continuous_logger/logger_node")
        self.assertTrue(logger and os.path.exists(logger), "logger_node not in runfiles")
        self._cli = r.Rlocation(f"{workspace}/basin/logging/continuous_logger/snapshot_extractor_cli")
        self.assertTrue(self._cli and os.path.exists(self._cli), "extractor cli not in runfiles")

        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.log_dir = os.path.join(self._tmp.name, "logs")

        # Split every second so more than one MCAP file shows up within the test's lifetime.
        logger_params = os.path.join(self._tmp.name, "logger.yaml")
        with open(logger_params, "w") as f:
            f.write(
                "/**:\n"
                "  ros__parameters:\n"
                f'    log_path: "{self.log_dir}"\n'
                "    log_split_time: 1\n"
                "    topics:\n"
                "      rosout:\n"
                '        name: "/rosout"\n'
                "        throttle: 0\n"
            )

        # Concurrent test processes on one DDS domain deadlock each other's node construction.
        domain = ros_test_domain.claim()
        self.addCleanup(domain.close)  # LIFO: runs after _kill_tree, once nothing uses the domain

        self._output = []
        self._output_changed = threading.Condition()
        self._proc = subprocess.Popen(
            [logger, "--ros-args", "--params-file", logger_params],
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
                seen = len(self._output)
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self._proc.poll() is not None and seen == len(self._output):
                    break
                self._output_changed.wait(min(remaining, 0.5))
        self.fail(f"never saw /{pattern}/ in output:\n" + "\n".join(self._output))

    def _stop_logger(self):
        os.killpg(self._proc.pid, signal.SIGINT)
        try:
            returncode = self._proc.wait(timeout=SHUTDOWN_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            self.fail("logger did not exit after SIGINT:\n" + "\n".join(self._output))
        self.assertEqual(returncode, 0, "logger exited uncleanly:\n" + "\n".join(self._output))

    def test_node_starts_logs_and_stops(self):
        # The node came up...
        self._wait_for_output(r"\[logger\]: Started logger", STARTUP_TIMEOUT_S)
        # ...discovered its topic from the params file (its GenericSubscription needs the
        # typesupport libraries)...
        self._wait_for_output(r"Subscribed to topic /rosout", STARTUP_TIMEOUT_S)
        # ...and is rolling over MCAP files, which only works if the storage plugins loaded.
        deadline = time.monotonic() + STARTUP_TIMEOUT_S
        while len(glob.glob(os.path.join(self.log_dir, "*.mcap"))) < 2:
            self.assertLess(time.monotonic(), deadline, "logger never split its log:\n" + "\n".join(self._output))
            time.sleep(0.25)
        self._stop_logger()
        self._wait_for_output(r"Logger node shutdown complete", 1)

        # The directory the logger left behind is one the extractor accepts.
        result = subprocess.run(
            [self._cli, "validate", self.log_dir],
            capture_output=True,
            text=True,
            env=dict(os.environ, **self._runfiles.EnvVars()),
            timeout=SHUTDOWN_TIMEOUT_S,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Log directory is valid", result.stdout)


class ExtractorCliTest(unittest.TestCase):
    def test_help(self):
        r = runfiles.Create()
        workspace = os.environ.get("TEST_WORKSPACE", "_main")
        cli = r.Rlocation(f"{workspace}/basin/logging/continuous_logger/snapshot_extractor_cli")
        result = subprocess.run([cli, "--help"], capture_output=True, text=True, env=dict(os.environ, **r.EnvVars()))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Usage", result.stdout)

    def test_rejects_missing_directory(self):
        r = runfiles.Create()
        workspace = os.environ.get("TEST_WORKSPACE", "_main")
        cli = r.Rlocation(f"{workspace}/basin/logging/continuous_logger/snapshot_extractor_cli")
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run(
                [cli, "validate", os.path.join(tmp, "does_not_exist")],
                capture_output=True,
                text=True,
                env=dict(os.environ, **r.EnvVars()),
            )
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
