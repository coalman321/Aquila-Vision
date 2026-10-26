"""Runs the pcap recorder the way it is deployed: as a component hosted by basin_container.

Starts the container with a config that loads basin::pcap_recorder::PcapRecorderNode from the
`pcap_recorder` ament package, pointed at the fake tcpdump, and checks that the node is found
through the ament index, loads, starts its capture program, and that SIGINT shuts both down
cleanly. What the node does with the capture is :pcap_recorder_node_test's job; this covers the
plugin packaging (ament index, dlopen, typesupport libraries in lib/), which that test doesn't.
"""
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

# What basin_container logs when a component can't be loaded. It keeps running afterwards, so
# without this a broken plugin would only fail the test once the startup timeout expired.
LOAD_FAILURE = re.compile(r"Failed to load library|Failed to find class|Component constructor threw")


class PcapRecorderComponentTest(unittest.TestCase):
    def setUp(self):
        r = runfiles.Create()
        workspace = os.environ.get("TEST_WORKSPACE", "_main")
        container = r.Rlocation(f"{workspace}/basin/core/node_core/basin_container")
        self.assertTrue(container and os.path.exists(container), "container not in runfiles")
        fake_tcpdump = r.Rlocation(f"{workspace}/basin/logging/pcap_recorder/fake_tcpdump")
        self.assertTrue(fake_tcpdump and os.path.exists(fake_tcpdump), "fake tcpdump not in runfiles")

        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

        recorder_params = os.path.join(self._tmp.name, "pcap_recorder.yaml")
        with open(recorder_params, "w") as f:
            f.write(
                "/**:\n"
                "  ros__parameters:\n"
                "    tcpdump:\n"
                f'      path: "{os.path.abspath(fake_tcpdump)}"\n'
                '      interface: "fake0"\n'
            )

        # A component only gets the arguments listed for it, so its parameters file goes in `args`.
        params = os.path.join(self._tmp.name, "container.yaml")
        with open(params, "w") as f:
            f.write(
                "/**/basin_container:\n"
                "  ros__parameters:\n"
                "    components:\n"
                "      0:\n"
                "        library_name: basin::pcap_recorder::PcapRecorderNode\n"
                "        package_name: pcap_recorder\n"
                f'        args: ["--ros-args", "--params-file", "{recorder_params}"]\n'
            )

        # Concurrent test processes on one DDS domain deadlock each other's node construction.
        domain = ros_test_domain.claim()
        self.addCleanup(domain.close)  # LIFO: runs after _kill_tree, once nothing uses the domain

        self._output = []
        self._output_changed = threading.Condition()
        self._proc = subprocess.Popen(
            [container, "--ros-args", "--params-file", params],
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
                        self.fail("component failed to load:\n" + "\n".join(self._output))
                seen = len(self._output)
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self._proc.poll() is not None and seen == len(self._output):
                    break
                self._output_changed.wait(min(remaining, 0.5))
        self.fail(f"never saw /{pattern}/ in output:\n" + "\n".join(self._output))

    def test_component_loads_captures_and_stops(self):
        self._wait_for_output(r"Instantiate class: .*basin::pcap_recorder::PcapRecorderNode", STARTUP_TIMEOUT_S)
        self._wait_for_output(r"\[pcap_recorder\]: pcap recorder started: .*fake_tcpdump -i fake0", STARTUP_TIMEOUT_S)
        self._wait_for_output(r"\[pcap_recorder\]: tcpdump: listening on fake0", STARTUP_TIMEOUT_S)

        # SIGINT to the container only: its shutdown has to stop the capture program itself
        self._proc.send_signal(signal.SIGINT)
        try:
            returncode = self._proc.wait(timeout=SHUTDOWN_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            self.fail("container did not exit after SIGINT:\n" + "\n".join(self._output))
        self.assertEqual(returncode, 0, "container exited uncleanly:\n" + "\n".join(self._output))
        self._wait_for_output(r"tcpdump: \d+ packets captured", 1)


if __name__ == "__main__":
    unittest.main()
