import os
from typing import Optional


class Scheduling:
    """Linux scheduler settings applied to a process before it is executed."""

    # Maps the user facing policy names to the kernel policy constants
    POLICIES = {
        "other": os.SCHED_OTHER,
        "batch": os.SCHED_BATCH,
        "idle": os.SCHED_IDLE,
        "fifo": os.SCHED_FIFO,
        "rr": os.SCHED_RR,
    }

    # Policies that are scheduled by a static priority rather than the nice value
    REALTIME_POLICIES = ("fifo", "rr")

    # Policies that respect the nice value
    NICE_POLICIES = ("other", "batch")

    def __init__(self, policy: str = "other", priority: int = 0, nice: Optional[int] = None) -> None:
        """
        Create a scheduler configuration.

        :param policy: One of 'other', 'batch', 'idle', 'fifo' or 'rr'
        :param priority: Static priority, 1-99 for 'fifo' and 'rr', must be 0 otherwise
        :param nice: Nice value from -20 to 19 for 'other' and 'batch', None leaves it inherited
        """
        if policy not in self.POLICIES:
            raise ValueError(f"Unknown scheduler policy '{policy}', expected one of {list(self.POLICIES.keys())}")

        policy_id = self.POLICIES[policy]
        min_priority = os.sched_get_priority_min(policy_id)
        max_priority = os.sched_get_priority_max(policy_id)
        if not min_priority <= priority <= max_priority:
            raise ValueError(
                f"Scheduler priority {priority} is invalid for policy '{policy}', "
                f"expected {min_priority}-{max_priority}"
            )

        if nice is not None:
            if policy not in self.NICE_POLICIES:
                raise ValueError(f"A nice value can only be used with the policies {list(self.NICE_POLICIES)}")
            if not -20 <= nice <= 19:
                raise ValueError(f"Nice value {nice} is invalid, expected -20 to 19")

        self.policy = policy
        self.policy_id = policy_id
        self.priority = priority
        self.nice = nice

    def __repr__(self) -> str:
        return f"Scheduling(policy={self.policy!r}, priority={self.priority}, nice={self.nice})"
