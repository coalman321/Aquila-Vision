"""
Round-trip tests for the bindings that need no external log data.

test_bindings.py's integration tests are skipped unless /tmp/test_logs exists, which it never does
in Bazel's sandbox. These build the directory they need: write a snapshot with SnapshotWriter, then
read it back through SnapshotExtractor and LogManager the way a real log directory is read. Reading
real MCAP data is covered by test_logger_output.py.
"""

import os
import sys
import tempfile

from log_util_py import (
    LogManager,
    SnapshotExtractor,
    SnapshotWriter,
)
import pytest


@pytest.fixture
def log_dir():
    """A log directory holding one snapshot and one (empty) MCAP file."""
    with tempfile.TemporaryDirectory() as tmpdir:
        writer = SnapshotWriter()
        writer.set_recording_directory(tmpdir)
        writer.start_snapshot('snap_a', 'manual', 'first snapshot', ['alpha', 'beta'], 100)
        writer.stop_snapshot('snap_a', 200)
        writer.save_to_file()

        # The extractor only checks that an MCAP file exists to call the directory valid.
        open(os.path.join(tmpdir, 'log_0_000000.mcap'), 'wb').close()
        yield tmpdir


def test_snapshot_survives_a_save_and_reload(log_dir):
    writer = SnapshotWriter()
    writer.set_recording_directory(log_dir)
    writer.load_from_file()

    assert writer.has_snapshot('snap_a')
    assert not writer.is_snapshot_running('snap_a')
    info = writer.get_snapshot('snap_a')
    assert info.id == 'snap_a'
    assert info.start_time == 100
    assert info.end_time == 200
    assert info.trigger == 'manual'
    assert info.description == 'first snapshot'
    assert list(info.tags) == ['alpha', 'beta']


def test_running_snapshot_is_reported_until_stopped():
    with tempfile.TemporaryDirectory() as tmpdir:
        writer = SnapshotWriter()
        writer.set_recording_directory(tmpdir)
        writer.start_snapshot('open_one', 'manual', '', [], 10)

        assert writer.has_snapshot('open_one')
        assert writer.is_snapshot_running('open_one')

        writer.stop_snapshot('open_one', 20)
        assert not writer.is_snapshot_running('open_one')


def test_extractor_validates_and_lists_the_directory(log_dir):
    extractor = SnapshotExtractor()

    valid, error = extractor.validate_log_directory(log_dir)
    assert valid, error

    snapshots = extractor.list_snapshots(log_dir)
    assert [s.id for s in snapshots] == ['snap_a']
    assert snapshots[0].start_time == 100
    assert snapshots[0].end_time == 200


def test_extractor_rejects_a_directory_without_an_mcap_file():
    with tempfile.TemporaryDirectory() as tmpdir:
        writer = SnapshotWriter()
        writer.set_recording_directory(tmpdir)
        writer.save_to_file()

        valid, error = SnapshotExtractor().validate_log_directory(tmpdir)
        assert not valid
        assert 'MCAP' in error


def test_log_manager_ignores_empty_mcap_files(log_dir):
    # A zero-byte MCAP is what a writer leaves behind if it dies before its first flush; the
    # manager skips those rather than trying to read a time range out of them.
    manager = LogManager()
    manager.set_log_directory(log_dir)

    assert manager.scan_log_directory() == 0
    assert list(manager.get_log_files()) == []


if __name__ == '__main__':
    sys.exit(pytest.main([__file__, '-v']))
