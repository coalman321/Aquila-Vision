"""
Basic tests for log_util_py bindings.

These tests demonstrate how to use the Python bindings.
They require a valid log directory with snapshots.yaml to run.
"""

import os
import sys
import tempfile

from log_util_py import (
    LogManager,
    SnapshotExtractor,
    SnapshotInfo,
    SnapshotWriter,
)
import pytest


def test_snapshot_info_creation():
    """Test creating a SnapshotInfo object."""
    info = SnapshotInfo()
    info.id = 'test_snapshot'
    info.start_time = 1000
    info.end_time = 2000
    info.trigger = 'manual'
    info.description = 'Test snapshot'
    info.tags = ['test', 'example']

    assert info.id == 'test_snapshot'
    assert info.start_time == 1000
    assert info.end_time == 2000
    assert info.trigger == 'manual'
    assert info.description == 'Test snapshot'
    assert len(info.tags) == 2
    assert 'test' in info.tags


def test_snapshot_extractor_creation():
    """Test creating a SnapshotExtractor instance."""
    extractor = SnapshotExtractor()
    assert extractor is not None


def test_snapshot_writer_creation():
    """Test creating a SnapshotWriter instance."""
    writer = SnapshotWriter()
    assert writer is not None


def test_log_manager_creation():
    """Test creating a LogManager instance."""
    manager = LogManager()
    assert manager is not None


def test_log_manager_disk_limits():
    """Test setting disk limits on LogManager."""
    manager = LogManager()
    manager.set_disk_limits(80.0, 70.0)
    # If no exception, test passes


def test_validate_invalid_directory():
    """Test validation of an invalid log directory."""
    extractor = SnapshotExtractor()
    valid, error = extractor.validate_log_directory('/nonexistent/path')

    assert not valid
    assert len(error) > 0


def test_snapshot_writer_directory():
    """Test setting recording directory on SnapshotWriter."""
    writer = SnapshotWriter()

    with tempfile.TemporaryDirectory() as tmpdir:
        writer.set_recording_directory(tmpdir)
        # If no exception, test passes


# Integration tests (require actual log data)
@pytest.mark.skipif(
    not os.path.exists('/tmp/test_logs'),
    reason='Test log directory not available'
)
def test_list_snapshots_integration():
    """Integration test for listing snapshots."""
    extractor = SnapshotExtractor()
    snapshots = extractor.list_snapshots('/tmp/test_logs')

    # Should return a list (may be empty)
    assert isinstance(snapshots, list)


@pytest.mark.skipif(
    not os.path.exists('/tmp/test_logs'),
    reason='Test log directory not available'
)
def test_log_manager_scan_integration():
    """Integration test for scanning log directory."""
    manager = LogManager()
    manager.set_log_directory('/tmp/test_logs')

    num_logs = manager.scan_log_directory()

    # Should return a non-negative number
    assert num_logs >= 0


if __name__ == '__main__':
    # Run tests with pytest
    sys.exit(pytest.main([__file__, '-v']))
