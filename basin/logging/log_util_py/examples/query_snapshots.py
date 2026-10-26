#!/usr/bin/env python3
"""
Example script demonstrating how to query and extract snapshots using log_util_py.

This script shows various ways to:
- List snapshots in a log directory
- Filter snapshots by various criteria
- Extract snapshots to rosbag files
- Query log metadata
"""

import argparse
from datetime import datetime
import sys

from log_util_py import LogManager
from log_util_py.utils import (
    extract_all_snapshots,
    extract_snapshot,
    find_snapshots_by_tag,
    find_snapshots_by_time_range,
    find_snapshots_by_trigger,
    list_snapshots,
    validate_log_directory,
)


def format_timestamp(epoch_seconds):
    """Convert epoch seconds to readable datetime string."""
    return datetime.fromtimestamp(epoch_seconds).strftime('%Y-%m-%d %H:%M:%S')


def format_duration(seconds):
    """Format duration in seconds to human-readable string."""
    if seconds < 60:
        return f'{seconds}s'
    elif seconds < 3600:
        minutes = seconds // 60
        secs = seconds % 60
        return f'{minutes}m {secs}s'
    else:
        hours = seconds // 3600
        minutes = (seconds % 3600) // 60
        return f'{hours}h {minutes}m'


def cmd_list(args):
    """List all snapshots in a log directory."""
    # Validate directory
    valid, error = validate_log_directory(args.log_dir)
    if not valid:
        print(f'Error: {error}')
        return 1

    # List snapshots
    snapshots = list_snapshots(args.log_dir)

    if not snapshots:
        print('No snapshots found.')
        return 0

    print(f'\nFound {len(snapshots)} snapshots:\n')
    print('-' * 100)

    for snap in snapshots:
        duration = snap.end_time - snap.start_time
        print(f'ID: {snap.id}')
        print(f'  Time:        '
              f'{format_timestamp(snap.start_time)} - {format_timestamp(snap.end_time)}')
        print(f'  Duration:    {format_duration(duration)}')
        print(f'  Trigger:     {snap.trigger}')
        print(f'  Description: {snap.description}')
        if snap.tags:
            print(f'  Tags:        {", ".join(snap.tags)}')
        print('-' * 100)

    return 0


def cmd_filter_tag(args):
    """Find snapshots with a specific tag."""
    snapshots = find_snapshots_by_tag(args.log_dir, args.tag)

    if not snapshots:
        print(f"No snapshots found with tag '{args.tag}'")
        return 0

    print(f"\nFound {len(snapshots)} snapshots with tag '{args.tag}':\n")
    for snap in snapshots:
        print(f'  {snap.id}: {snap.description}')

    return 0


def cmd_filter_trigger(args):
    """Find snapshots by trigger type."""
    snapshots = find_snapshots_by_trigger(args.log_dir, args.trigger)

    if not snapshots:
        print(f"No snapshots found with trigger '{args.trigger}'")
        return 0

    print(f"\nFound {len(snapshots)} snapshots with trigger '{args.trigger}':\n")
    for snap in snapshots:
        duration = snap.end_time - snap.start_time
        print(f'  {snap.id}: {snap.description} (duration: {format_duration(duration)})')

    return 0


def cmd_filter_time(args):
    """Find snapshots in a time range."""
    snapshots = find_snapshots_by_time_range(args.log_dir, args.start_time, args.end_time)

    if not snapshots:
        print(
            f'No snapshots found between '
            f'{format_timestamp(args.start_time)} and {format_timestamp(args.end_time)}'
        )
        return 0

    print(f'\nFound {len(snapshots)} snapshots in time range:\n')
    print(f'Query range: {format_timestamp(args.start_time)} - {format_timestamp(args.end_time)}')

    for snap in snapshots:
        print(f'  {snap.id}:')
        print(f'    Time: {format_timestamp(snap.start_time)} - {format_timestamp(snap.end_time)}')
        print(f'    Description: {snap.description}')

    return 0


def cmd_extract(args):
    """Extract a specific snapshot."""
    print(f"Extracting snapshot '{args.snapshot_id}' from {args.log_dir}...")

    success = extract_snapshot(args.log_dir, args.snapshot_id, args.output_dir)

    if success:
        print(f'✓ Successfully extracted to {args.output_dir}')
        return 0
    else:
        print(f"✗ Failed to extract snapshot '{args.snapshot_id}'")
        return 1


def cmd_extract_all(args):
    """Extract all snapshots."""
    print(f'Extracting all snapshots from {args.log_dir}...')

    count = extract_all_snapshots(args.log_dir, args.output_dir)

    print(f'✓ Successfully extracted {count} snapshots to {args.output_dir}')
    return 0


def cmd_info(args):
    """Show detailed info about a log directory."""
    # Validate directory
    valid, error = validate_log_directory(args.log_dir)
    if not valid:
        print(f'Error: {error}')
        return 1

    # Use LogManager to get detailed info
    manager = LogManager()
    manager.set_log_directory(args.log_dir)
    num_logs = manager.scan_log_directory()

    print('\nLog Directory Information:')
    print(f'  Path: {args.log_dir}')
    print(f'  MCAP Files: {num_logs}')

    # Get disk usage if available
    try:
        usage = manager.get_disk_usage_percent()
        print(f'  Disk Usage: {usage:.1f}%')
    except Exception:
        pass

    # Get snapshots
    snapshots = list_snapshots(args.log_dir)
    print(f'  Snapshots: {len(snapshots)}')

    if snapshots:
        total_duration = sum(snap.end_time - snap.start_time for snap in snapshots)
        print(f'  Total Snapshot Duration: {format_duration(total_duration)}')

        # Count by trigger type
        triggers = {}
        for snap in snapshots:
            triggers[snap.trigger] = triggers.get(snap.trigger, 0) + 1

        print('\n  Snapshots by Trigger:')
        for trigger, count in triggers.items():
            print(f'    {trigger}: {count}')

        # Count by tag
        tags = {}
        for snap in snapshots:
            for tag in snap.tags:
                tags[tag] = tags.get(tag, 0) + 1

        if tags:
            print('\n  Snapshots by Tag:')
            for tag, count in sorted(tags.items(), key=lambda x: x[1], reverse=True):
                print(f'    {tag}: {count}')

    return 0


def main():
    parser = argparse.ArgumentParser(
        description='Query and extract snapshots from continuous logger logs'
    )

    subparsers = parser.add_subparsers(dest='command', help='Command to run')

    # List command
    list_parser = subparsers.add_parser('list', help='List all snapshots')
    list_parser.add_argument('log_dir', help='Log directory path')

    # Filter by tag command
    tag_parser = subparsers.add_parser('filter-tag', help='Filter snapshots by tag')
    tag_parser.add_argument('log_dir', help='Log directory path')
    tag_parser.add_argument('tag', help='Tag to filter by')

    # Filter by trigger command
    trigger_parser = subparsers.add_parser('filter-trigger', help='Filter snapshots by trigger')
    trigger_parser.add_argument('log_dir', help='Log directory path')
    trigger_parser.add_argument('trigger', help='Trigger type to filter by')

    # Filter by time command
    time_parser = subparsers.add_parser('filter-time', help='Filter snapshots by time range')
    time_parser.add_argument('log_dir', help='Log directory path')
    time_parser.add_argument('start_time', type=int, help='Start time (epoch seconds)')
    time_parser.add_argument('end_time', type=int, help='End time (epoch seconds)')

    # Extract command
    extract_parser = subparsers.add_parser('extract', help='Extract a specific snapshot')
    extract_parser.add_argument('log_dir', help='Log directory path')
    extract_parser.add_argument('snapshot_id', help='Snapshot ID to extract')
    extract_parser.add_argument('output_dir', help='Output directory')

    # Extract all command
    extract_all_parser = subparsers.add_parser('extract-all', help='Extract all snapshots')
    extract_all_parser.add_argument('log_dir', help='Log directory path')
    extract_all_parser.add_argument('output_dir', help='Output base directory')

    # Info command
    info_parser = subparsers.add_parser('info', help='Show log directory information')
    info_parser.add_argument('log_dir', help='Log directory path')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Dispatch to command handler
    command_handlers = {
        'list': cmd_list,
        'filter-tag': cmd_filter_tag,
        'filter-trigger': cmd_filter_trigger,
        'filter-time': cmd_filter_time,
        'extract': cmd_extract,
        'extract-all': cmd_extract_all,
        'info': cmd_info,
    }

    handler = command_handlers.get(args.command)
    if handler:
        return handler(args)
    else:
        print(f'Unknown command: {args.command}')
        return 1


if __name__ == '__main__':
    sys.exit(main())
