"""
Reads real logger output through the bindings.

Runs the actual logger_node for a few seconds (splitting its MCAP files every second), then reads
the directory it wrote with LogManager and SnapshotExtractor. This is the one place the C++ writer
and the Python reader meet on real data; test_snapshot_roundtrip.py fakes the directory instead.
"""

import glob
import os
import signal
import subprocess
import sys
import tempfile
import time

from log_util_py import LogManager, SnapshotExtractor
import pytest
import ros_test_domain
from python.runfiles import runfiles

STARTUP_TIMEOUT_S = 30
SHUTDOWN_TIMEOUT_S = 15
MAX_TIME_NS = 2**62


@pytest.fixture(scope='module')
def log_dir():
    """Runs the logger until it has rolled over at least two MCAP files, then stops it."""
    r = runfiles.Create()
    workspace = os.environ.get('TEST_WORKSPACE', '_main')
    logger = r.Rlocation(f'{workspace}/basin/logging/continuous_logger/logger_node')
    assert logger and os.path.exists(logger), 'logger_node not in runfiles'

    # Concurrent test processes on one DDS domain deadlock each other's node construction.
    with tempfile.TemporaryDirectory() as tmpdir, ros_test_domain.claim() as domain:
        proc = subprocess.Popen(
            [
                logger, '--ros-args',
                '-p', f'log_path:={tmpdir}',
                '-p', 'log_split_time:=1',
                '-p', 'topics.rosout.name:=/rosout',
                '-p', 'topics.rosout.throttle:=0',
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=domain.env(dict(os.environ, **r.EnvVars())),
        )
        try:
            deadline = time.monotonic() + STARTUP_TIMEOUT_S
            while len(glob.glob(os.path.join(tmpdir, '*.mcap'))) < 2:
                assert proc.poll() is None, 'logger exited early:\n' + proc.stdout.read()
                assert time.monotonic() < deadline, 'logger never split its log'
                time.sleep(0.25)
            proc.send_signal(signal.SIGINT)
            output, _ = proc.communicate(timeout=SHUTDOWN_TIMEOUT_S)
            assert proc.returncode == 0, 'logger exited uncleanly:\n' + output
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()
        yield tmpdir


def test_extractor_accepts_the_directory(log_dir):
    valid, error = SnapshotExtractor().validate_log_directory(log_dir)
    assert valid, error
    assert SnapshotExtractor().list_snapshots(log_dir) == []


def test_log_manager_reads_the_time_range_of_each_file(log_dir):
    manager = LogManager()
    manager.set_log_directory(log_dir)

    # scan_log_directory only keeps files whose time range it could read out of the MCAP itself.
    found = manager.scan_log_directory()
    assert found >= 1
    files = list(manager.get_log_files())
    assert len(files) == found
    assert all(os.path.exists(f) and f.endswith('.mcap') for f in files)


def test_time_range_query_finds_the_files(log_dir):
    found = SnapshotExtractor().find_relevant_mcap_files(log_dir, 0, MAX_TIME_NS)
    assert len(found) >= 1
    assert all(isinstance(f, str) and os.path.exists(f) for f in found)  # documented as List[str]


if __name__ == '__main__':
    sys.exit(pytest.main([__file__, '-v']))
