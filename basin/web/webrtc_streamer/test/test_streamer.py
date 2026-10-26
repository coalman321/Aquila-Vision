"""Runs webrtc_streamer_node against a fake camera and a browser-style offer.

The node is started as deployed, with GStreamer's plugins from the runfiles. A publisher stands in
for a camera on /debug_img (a topic the node discovers by default). The test then sends the node a
real WebRTC offer, made by webrtcbin (@gstreamer//:make_offer), for that topic and expects
an SDP answer back: which needs the image pipeline (appsrc, videoconvert, VP8 and RTP payloading) and
webrtcbin's ICE, DTLS and SRTP handling to work in the process.
"""
import json
import os
import signal
import socket
import subprocess
import threading
import time
import unittest
import urllib.error
import urllib.request

import ros_test_domain
from python.runfiles import runfiles

TOPIC = "/debug_img"
STARTUP_TIMEOUT_S = 60
DISCOVERY_TIMEOUT_S = 30


def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class WebRtcStreamerTest(unittest.TestCase):
    def setUp(self):
        r = runfiles.Create()
        workspace = os.environ.get("TEST_WORKSPACE", "_main")

        def find(rlocation):
            found = r.Rlocation(rlocation)
            self.assertTrue(found and os.path.exists(found), f"{rlocation} not in runfiles")
            return found

        node = find(f"{workspace}/basin/web/webrtc_streamer/webrtc_streamer_node")
        publisher = find(f"{workspace}/basin/web/webrtc_streamer/image_publisher")
        # In another module's repository, so its path comes from the BUILD file (see MAKE_OFFER there).
        self._make_offer = find(os.environ["MAKE_OFFER"])

        # Concurrent test processes on one DDS domain deadlock each other's node construction.
        domain = ros_test_domain.claim()
        self.addCleanup(domain.close)  # LIFO: runs after the processes are gone
        self._env = domain.env(dict(os.environ, **r.EnvVars()))

        self._port = _free_port()
        self._output = []
        self._procs = []
        self._pumps = []
        self.addCleanup(self._print_node_output)  # LIFO: after _kill_all has collected all of it
        self.addCleanup(self._kill_all)
        self._node = self._spawn([node, "--ros-args", "-p", f"port:={self._port}"], capture=True)
        self._spawn([publisher])
        self._wait_for(self._status, "the node's web server", STARTUP_TIMEOUT_S)

    def _spawn(self, argv, capture=False):
        proc = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE if capture else subprocess.DEVNULL,
            stderr=subprocess.STDOUT if capture else subprocess.DEVNULL,
            text=True,
            env=self._env,
            start_new_session=True,  # so a failed test can take the whole process tree down
        )
        self._procs.append(proc)
        if capture:
            pump = threading.Thread(target=self._pump, args=(proc,), daemon=True)
            pump.start()
            self._pumps.append(pump)
        return proc

    def _pump(self, proc):
        for line in proc.stdout:
            self._output.append(line.rstrip("\n"))

    def _kill_all(self):
        for proc in self._procs:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait()
        for pump in self._pumps:
            pump.join(timeout=5)  # EOF follows the process, so this only drains what's left
        for proc in self._procs:
            if proc.stdout is not None:
                proc.stdout.close()

    def _print_node_output(self):
        # Bazel shows this only for a failing test. Without it, a failure after startup (say the node
        # crashing while it answers the offer) reports a bare connection error and nothing about why.
        print("--- webrtc_streamer_node output ---")
        print("\n".join(self._output))
        print("--- end of webrtc_streamer_node output ---")

    def _url(self, path):
        return f"http://127.0.0.1:{self._port}{path}"

    def _status(self):
        return self._get("/webrtc/status")

    def _get(self, path):
        with urllib.request.urlopen(self._url(path), timeout=5) as response:
            return json.load(response)

    def _post(self, path, body):
        request = urllib.request.Request(
            self._url(path), data=json.dumps(body).encode(), headers={"Content-Type": "application/json"}
        )
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as e:
            self.fail(f"POST {path} returned {e.code}: {e.read().decode(errors='replace')}")
        except (urllib.error.URLError, OSError) as e:
            exit_code = self._node.poll()
            state = "still running" if exit_code is None else f"dead (exit code {exit_code})"
            self.fail(f"POST {path} failed ({e!r}); the node is {state}")

    def _wait_for(self, condition, what, timeout_s):
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self.assertIsNone(self._node.poll(), "the node exited:\n" + "\n".join(self._output[-30:]))
            try:
                result = condition()
                if result:
                    return result
            except (urllib.error.URLError, ConnectionError, OSError):
                pass
            time.sleep(0.5)
        self.fail(f"timed out waiting for {what}:\n" + "\n".join(self._output[-30:]))

    def _discovered(self):
        return any(s.get("stream_id") == TOPIC for s in self._get("/webrtc/streams")["streams"])

    def test_answers_an_offer_for_a_discovered_camera(self):
        # The node answers once ICE gathering completes, and libnice ignores loopback, so with no other
        # interface it gathers nothing and the offer only ever times out. That is what a sandbox without
        # network access looks like, so say so rather than leave a bare "Timeout generating answer".
        self.assertTrue(
            [name for _, name in socket.if_nameindex() if name != "lo"],
            "no network interface besides lo: libnice finds no ICE candidates, so the node never answers. "
            "This test needs the host's network (tags = [\"requires-network\"] in its BUILD file).",
        )
        self._wait_for(self._discovered, f"{TOPIC} to be discovered", DISCOVERY_TIMEOUT_S)

        offer = subprocess.run([self._make_offer], env=self._env, capture_output=True, text=True, timeout=30)
        self.assertEqual(offer.returncode, 0, offer.stderr)
        self.assertIn("m=video", offer.stdout)

        status, body = self._post("/webrtc/offer", {"type": "offer", "sdp": offer.stdout, "stream_id": TOPIC})
        self.assertEqual(status, 200, body)
        answer = json.loads(body)
        self.assertEqual(answer["type"], "answer")
        self.assertEqual(answer["stream_id"], TOPIC)
        self.assertTrue(answer["peer_id"])
        for expected in ("m=video", "VP8/90000", "a=fingerprint:sha-256", "a=ice-ufrag"):
            self.assertIn(expected, answer["sdp"])

        self.assertEqual(self._status()["active_peers"], 1)
        status, _ = self._post("/webrtc/disconnect", {"peer_id": answer["peer_id"]})
        self.assertEqual(status, 200)
        self.assertEqual(self._status()["active_peers"], 0)

    def test_uses_the_bundled_plugins(self):
        self._wait_for(self._discovered, f"{TOPIC} to be discovered", DISCOVERY_TIMEOUT_S)
        self.assertFalse(
            [line for line in self._output if "system's GStreamer" in line],
            "the node fell back to system plugins:\n" + "\n".join(self._output[:30]),
        )


if __name__ == "__main__":
    unittest.main()
