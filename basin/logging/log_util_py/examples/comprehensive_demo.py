#!/usr/bin/env python3
"""
Comprehensive example demonstrating all log_util_py features.

This script shows how to use all the main features of the Python bindings:
- Validating log directories
- Listing and filtering snapshots
- Extracting snapshots
- Managing logs with LogManager
- Working with snapshot metadata
"""

from datetime import datetime
from pathlib import Path
import sys

from log_util_py import (
    LogManager,
    SnapshotExtractor,
    SnapshotWriter,
)
from log_util_py.utils import (
    find_snapshots_by_tag,
    find_snapshots_by_time_range,
    find_snapshots_by_trigger,
    get_snapshot_info,
    list_snapshots,
    validate_log_directory,
)


def print_section(title):
    """Print a formatted section header."""
    print(f'\n{"="*80}')
    print(f' {title}')
    print(f'{"="*80}\n')


def format_time(epoch_sec):
    """Format epoch seconds as readable time."""
    return datetime.fromtimestamp(epoch_sec).strftime('%Y-%m-%d %H:%M:%S')


def demo_validation(log_dir):
    """Demonstrate log directory validation."""
    print_section('1. Validating Log Directory')

    valid, error = validate_log_directory(log_dir)

    if valid:
        print(f'✓ Log directory is valid: {log_dir}')
    else:
        print(f'✗ Log directory is invalid: {error}')
        return False

    return True


def demo_list_snapshots(log_dir):
    """Demonstrate listing all snapshots."""
    print_section('2. Listing All Snapshots')

    snapshots = list_snapshots(log_dir)

    if not snapshots:
        print('No snapshots found in the log directory.')
        return []

    print(f'Found {len(snapshots)} snapshots:\n')

    for i, snap in enumerate(snapshots, 1):
        duration = snap.end_time - snap.start_time
        print(f'{i}. {snap.id}')
        print(f'   Start: {format_time(snap.start_time)}')
        print(f'   End:   {format_time(snap.end_time)}')
        print(f'   Duration: {duration} seconds')
        print(f'   Trigger: {snap.trigger}')
        print(f'   Description: {snap.description}')
        if snap.tags:
            print(f'   Tags: {", ".join(snap.tags)}')
        print()

    return snapshots


def demo_filter_by_tag(log_dir):
    """Demonstrate filtering snapshots by tag."""
    print_section('3. Filtering Snapshots by Tag')

    # Get all snapshots to find available tags
    all_snapshots = list_snapshots(log_dir)

    if not all_snapshots:
        print('No snapshots to filter.')
        return

    # Collect all unique tags
    all_tags = set()
    for snap in all_snapshots:
        all_tags.update(snap.tags)

    if not all_tags:
        print('No tags found on snapshots.')
        return

    print(f'Available tags: {", ".join(sorted(all_tags))}\n')

    # Filter by each tag
    for tag in sorted(all_tags):
        filtered = find_snapshots_by_tag(log_dir, tag)
        print(f"Tag '{tag}': {len(filtered)} snapshot(s)")
        for snap in filtered:
            print(f'  - {snap.id}: {snap.description}')


def demo_filter_by_trigger(log_dir):
    """Demonstrate filtering snapshots by trigger."""
    print_section('4. Filtering Snapshots by Trigger')

    all_snapshots = list_snapshots(log_dir)

    if not all_snapshots:
        print('No snapshots to filter.')
        return

    # Collect all unique triggers
    triggers = {snap.trigger for snap in all_snapshots}

    print(f'Available triggers: {", ".join(sorted(triggers))}\n')

    for trigger in sorted(triggers):
        filtered = find_snapshots_by_trigger(log_dir, trigger)
        print(f"Trigger '{trigger}': {len(filtered)} snapshot(s)")


def demo_filter_by_time(log_dir):
    """Demonstrate filtering snapshots by time range."""
    print_section('5. Filtering Snapshots by Time Range')

    all_snapshots = list_snapshots(log_dir)

    if not all_snapshots:
        print('No snapshots to filter.')
        return

    # Find the time range of all snapshots
    min_time = min(snap.start_time for snap in all_snapshots)
    max_time = max(snap.end_time for snap in all_snapshots)

    print(f'Overall time range: {format_time(min_time)} to {format_time(max_time)}\n')

    # Query the middle third
    range_duration = max_time - min_time
    query_start = min_time + range_duration // 3
    query_end = min_time + 2 * range_duration // 3

    filtered = find_snapshots_by_time_range(log_dir, query_start, query_end)

    print(f'Snapshots in middle third ({format_time(query_start)} to {format_time(query_end)}):')
    print(f'Found {len(filtered)} snapshot(s)')
    for snap in filtered:
        print(f'  - {snap.id}')


def demo_log_manager(log_dir):
    """Demonstrate LogManager functionality."""
    print_section('6. Using LogManager')

    manager = LogManager()
    manager.set_log_directory(log_dir)
    manager.set_disk_limits(rotation_threshold_percent=80.0, snapshot_usage_threshold_percent=50.0)

    # Scan directory
    num_logs = manager.scan_log_directory()
    print('Scanned log directory')
    print(f'  Found {num_logs} MCAP file(s)')

    # Get disk usage
    try:
        usage = manager.get_disk_usage_percent()
        print(f'  Disk usage: {usage:.2f}%')

        disk = manager.get_last_disk_check()
        if disk.snapshot_usage_high:
            print(f'  ⚠ Warning: snapshots fill {disk.snapshot_usage_percent:.1f}% of the disk!')
    except Exception as e:
        print(f'  Could not get disk usage: {e}')

    # Get all log files
    log_files = manager.get_log_files()
    print(f'  Total log files: {len(log_files)}')

    # Get snapshot writer
    writer = manager.get_snapshot_writer()
    snapshots = writer.get_all_snapshots()
    print(f'  Total snapshots: {len(snapshots)}')


def demo_snapshot_extractor(log_dir):
    """Demonstrate SnapshotExtractor features."""
    print_section('7. Using SnapshotExtractor')

    extractor = SnapshotExtractor()

    # List snapshots
    snapshots = extractor.list_snapshots(log_dir)
    print(f'Listed {len(snapshots)} snapshots using extractor')

    if not snapshots:
        return

    # Get first snapshot
    first_snap = snapshots[0]
    print(f'\nExamining snapshot: {first_snap.id}')

    # Find relevant MCAP files
    mcap_files = extractor.find_relevant_mcap_files(
        log_dir,
        first_snap.start_time,
        first_snap.end_time
    )

    print(f'  Relevant MCAP files: {len(mcap_files)}')
    for mcap in mcap_files:
        print(f'    - {Path(mcap).name}')


def demo_snapshot_writer(log_dir):
    """Demonstrate SnapshotWriter features."""
    print_section('8. Using SnapshotWriter')

    writer = SnapshotWriter()
    writer.set_recording_directory(log_dir)

    # Load existing snapshots
    if writer.load_from_file():
        print('✓ Loaded snapshots from file')
    else:
        print('✗ Could not load snapshots file')
        return

    # Get all snapshots
    snapshots = writer.get_all_snapshots()
    print(f'Total snapshots: {len(snapshots)}')

    if snapshots:
        # Check specific snapshot
        snap_id = snapshots[0].id
        print(f'\nChecking snapshot: {snap_id}')
        print(f'  Exists: {writer.has_snapshot(snap_id)}')
        print(f'  Is running: {writer.is_snapshot_running(snap_id)}')

        # Get snapshot info
        info = writer.get_snapshot(snap_id)
        print(f'  Description: {info.description}')


def demo_get_snapshot_info(log_dir):
    """Demonstrate getting specific snapshot info."""
    print_section('9. Getting Specific Snapshot Info')

    all_snapshots = list_snapshots(log_dir)

    if not all_snapshots:
        print('No snapshots available.')
        return

    snap_id = all_snapshots[0].id
    print(f'Getting info for: {snap_id}')

    info = get_snapshot_info(log_dir, snap_id)

    if info:
        print('  ✓ Found snapshot')
        print(f'  ID: {info.id}')
        print(f'  Time: {format_time(info.start_time)} to {format_time(info.end_time)}')
        print(f'  Duration: {info.end_time - info.start_time} seconds')
    else:
        print('  ✗ Snapshot not found')


def main():
    """Run all log_util_py feature demonstrations."""
    if len(sys.argv) < 2:
        print('Usage: python3 comprehensive_demo.py <log_directory>')
        print('\nThis script demonstrates all features of log_util_py')
        print('by analyzing a log directory containing MCAP files and snapshots.yaml')
        return 1

    log_dir = sys.argv[1]

    print(f"""
╔══════════════════════════════════════════════════════════════════════════════╗
║                   log_util_py Comprehensive Demo                    ║
╚══════════════════════════════════════════════════════════════════════════════╝

Analyzing log directory: {log_dir}
""")

    # Run all demonstrations
    if not demo_validation(log_dir):
        print('\n✗ Log directory validation failed. Exiting.')
        return 1

    try:
        snapshots = demo_list_snapshots(log_dir)

        if snapshots:
            demo_filter_by_tag(log_dir)
            demo_filter_by_trigger(log_dir)
            demo_filter_by_time(log_dir)

        demo_log_manager(log_dir)
        demo_snapshot_extractor(log_dir)
        demo_snapshot_writer(log_dir)

        if snapshots:
            demo_get_snapshot_info(log_dir)

        print_section('Demo Complete')
        print('✓ All features demonstrated successfully!')
        print('\nFor extraction examples, see query_snapshots.py')

    except Exception as e:
        print(f'\n✗ Error during demo: {e}')
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
