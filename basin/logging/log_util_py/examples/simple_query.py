#!/usr/bin/env python3
"""Simple example showing basic snapshot querying."""

from log_util_py.utils import list_snapshots

# Replace with your log directory path
LOG_DIR = '/path/to/your/logs'


def main():
    print(f'Querying snapshots in: {LOG_DIR}\n')

    # List all snapshots
    snapshots = list_snapshots(LOG_DIR)

    if not snapshots:
        print('No snapshots found.')
        return

    print(f'Found {len(snapshots)} snapshots:\n')

    for snap in snapshots:
        duration = snap.end_time - snap.start_time
        print(f'Snapshot: {snap.id}')
        print(f'  Duration: {duration} seconds')
        print(f'  Trigger: {snap.trigger}')
        print(f'  Description: {snap.description}')
        if snap.tags:
            print(f'  Tags: {", ".join(snap.tags)}')
        print()


if __name__ == '__main__':
    main()
