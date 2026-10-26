"""Utility functions for working with snapshots."""

from typing import List, Optional, Tuple

from ._log_util_py import SnapshotExtractor, SnapshotInfo


def list_snapshots(log_directory: str) -> List[SnapshotInfo]:
    """
    List all snapshots in a log directory.

    Args:
        log_directory: Path to the directory containing MCAP files and snapshots.yaml

    Returns
    -------
    List of SnapshotInfo objects

    Example:
        >>> snapshots = list_snapshots('/path/to/logs')
        >>> for snap in snapshots:
        ...     print(f"{snap.id}: {snap.description}")

    """
    extractor = SnapshotExtractor()
    return extractor.list_snapshots(log_directory)


def find_snapshots_by_tag(log_directory: str, tag: str) -> List[SnapshotInfo]:
    """
    Find all snapshots that have a specific tag.

    Args:
        log_directory: Path to the directory containing snapshots.yaml
        tag: Tag to search for

    Returns
    -------
    List of SnapshotInfo objects with the specified tag

    Example:
        >>> critical_snapshots = find_snapshots_by_tag('/path/to/logs', 'critical')

    """
    extractor = SnapshotExtractor()
    all_snapshots = extractor.list_snapshots(log_directory)
    return [snap for snap in all_snapshots if tag in snap.tags]


def find_snapshots_by_time_range(
    log_directory: str,
    start_time: int,
    end_time: int
) -> List[SnapshotInfo]:
    """
    Find all snapshots that overlap with a given time range.

    Args:
        log_directory: Path to the directory containing snapshots.yaml
        start_time: Start time in epoch seconds
        end_time: End time in epoch seconds

    Returns
    -------
    List of SnapshotInfo objects that overlap with the time range

    Example:
        >>> import time
        >>> now = int(time.time())
        >>> hour_ago = now - 3600
        >>> recent = find_snapshots_by_time_range('/path/to/logs', hour_ago, now)

    """
    extractor = SnapshotExtractor()
    all_snapshots = extractor.list_snapshots(log_directory)

    # Check if snapshot time range overlaps with query time range
    return [
        snap for snap in all_snapshots
        if not (snap.end_time < start_time or snap.start_time > end_time)
    ]


def extract_snapshot(
    log_directory: str,
    snapshot_id: str,
    output_directory: str
) -> bool:
    """
    Extract a single snapshot to a rosbag.

    Args:
        log_directory: Directory containing MCAP files and snapshots.yaml
        snapshot_id: ID of the snapshot to extract
        output_directory: Directory where the extracted bag will be created

    Returns
    -------
    True if extraction was successful

    Example:
        >>> success = extract_snapshot(
        ...     '/path/to/logs',
        ...     'snapshot_001',
        ...     '/path/to/output'
        ... )

    """
    extractor = SnapshotExtractor()
    return extractor.extract_snapshot(log_directory, snapshot_id, output_directory)


def extract_all_snapshots(
    log_directory: str,
    output_base_directory: str
) -> int:
    """
    Extract all snapshots from a log directory.

    Args:
        log_directory: Directory containing MCAP files and snapshots.yaml
        output_base_directory: Base directory where snapshot bags will be created

    Returns
    -------
    Number of snapshots successfully extracted

    Example:
        >>> count = extract_all_snapshots('/path/to/logs', '/path/to/output')
        >>> print(f"Extracted {count} snapshots")

    """
    extractor = SnapshotExtractor()
    return extractor.extract_all_snapshots(log_directory, output_base_directory)


def validate_log_directory(log_directory: str) -> Tuple[bool, str]:
    """
    Validate that a log directory has the required files.

    Args:
        log_directory: Directory to validate

    Returns
    -------
    Tuple of (is_valid, error_message)

    Example:
        >>> valid, error = validate_log_directory('/path/to/logs')
        >>> if not valid:
        ...     print(f"Invalid directory: {error}")

    """
    extractor = SnapshotExtractor()
    return extractor.validate_log_directory(log_directory)


def get_snapshot_info(log_directory: str, snapshot_id: str) -> Optional[SnapshotInfo]:
    """
    Get information about a specific snapshot.

    Args:
        log_directory: Directory containing snapshots.yaml
        snapshot_id: ID of the snapshot

    Returns
    -------
    SnapshotInfo object or None if not found

    Example:
        >>> info = get_snapshot_info('/path/to/logs', 'snapshot_001')
        >>> if info:
        ...     print(f"Duration: {info.end_time - info.start_time} seconds")

    """
    extractor = SnapshotExtractor()
    snapshots = extractor.list_snapshots(log_directory)
    for snap in snapshots:
        if snap.id == snapshot_id:
            return snap
    return None


def find_snapshots_by_trigger(log_directory: str, trigger: str) -> List[SnapshotInfo]:
    """
    Find all snapshots that were triggered by a specific trigger.

    Args:
        log_directory: Path to the directory containing snapshots.yaml
        trigger: Trigger type to search for (e.g., 'manual' or 'event-<trigger_id>')

    Returns
    -------
    List of SnapshotInfo objects with the specified trigger

    Example:
        >>> manual_snapshots = find_snapshots_by_trigger('/path/to/logs', 'manual')

    """
    extractor = SnapshotExtractor()
    all_snapshots = extractor.list_snapshots(log_directory)
    return [snap for snap in all_snapshots if snap.trigger == trigger]
