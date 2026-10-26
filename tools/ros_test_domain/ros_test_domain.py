"""Gives a test process a DDS domain that no other live test process on this host is using.

Bazel runs test processes concurrently -- one target's own runs, plus every other target that
touches DDS -- and processes on a shared domain discover each other. Their receive threads then
handle each other's traffic and, worse, Fast DDS deadlocks internally while entities are created and
destroyed. From a test's side that is a node constructor that never finishes, so the log line the
test waits for never appears and it times out: a few failures in 20 concurrent runs of any test that
starts a ROS process.

This is the Python counterpart of claimIsolatedDomainId() in
//basin/core/node_core/test/runner.cpp, and it uses the same lock files, so C++ and Python tests keep
out of each other's domains too:

    domain = ros_test_domain.claim()
    self.addCleanup(domain.close)          # after whatever stops the processes; see below
    proc = subprocess.Popen(cmd, env=domain.env(dict(os.environ, **r.EnvVars())))

Each ID is claimed with a non-blocking flock on a per-ID file that the claim keeps open. The kernel
drops the lock when the process exits, however it exits, so a crashed test can't leak a domain. Close
the claim only once the processes using the domain are gone (unittest cleanups run last-in first-out,
so register it before the one that kills the processes). IDs 0-101 are the range ROS 2 documents as
safe for the default port mapping. If every ID is claimed, or the lock files can't be used, the claim
falls back to a random one, which is no worse than sharing a domain.
"""

import fcntl
import os
import random

DOMAIN_COUNT = 102
LOCK_PATH = "/tmp/basin_test_ros_domain_{}.lock"


class RosDomain:
    """A claimed DDS domain ID (or, if ROS_DOMAIN_ID was set explicitly, that choice)."""

    def __init__(self, domain_id, lock_file, explicit):
        self.domain_id = domain_id
        self._lock_file = lock_file
        self._explicit = explicit

    def env(self, base=None):
        """Returns a copy of `base` (default: os.environ) with ROS_DOMAIN_ID set to this domain.

        An explicit ROS_DOMAIN_ID -- a deliberate choice, e.g. debugging against a live system --
        is left as it was.
        """
        env = dict(os.environ if base is None else base)
        if not self._explicit:
            env["ROS_DOMAIN_ID"] = str(self.domain_id)
        return env

    def close(self):
        """Releases the claim. Safe to call more than once."""
        if self._lock_file:
            self._lock_file.close()
            self._lock_file = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def claim():
    """Claims an isolated domain; keep the result open while processes are using the domain."""
    if "ROS_DOMAIN_ID" in os.environ:
        return RosDomain(int(os.environ["ROS_DOMAIN_ID"]), None, explicit=True)
    start = random.randrange(DOMAIN_COUNT)
    for i in range(DOMAIN_COUNT):
        domain_id = (start + i) % DOMAIN_COUNT
        try:
            lock_file = open(LOCK_PATH.format(domain_id), "a+")
        except OSError:
            continue
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            lock_file.close()
            continue
        return RosDomain(domain_id, lock_file, explicit=False)
    return RosDomain(start, None, explicit=False)
