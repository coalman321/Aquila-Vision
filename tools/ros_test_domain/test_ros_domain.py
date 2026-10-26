"""Tests for ros_test_domain's claim/release behaviour.

The lock files are pointed at a private directory so exhausting the domains here can't push tests
running alongside this one onto random ones.
"""
import os
import tempfile
import unittest
from unittest import mock

import ros_test_domain


class ClaimTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        patches = [
            mock.patch.object(ros_test_domain, "LOCK_PATH", os.path.join(tmp.name, "domain_{}.lock")),
            mock.patch.object(ros_test_domain, "DOMAIN_COUNT", 4),
            mock.patch.dict(os.environ),
        ]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)
        os.environ.pop("ROS_DOMAIN_ID", None)

    def test_live_claims_are_distinct(self):
        claims = [ros_test_domain.claim() for _ in range(4)]
        self.addCleanup(lambda: [c.close() for c in claims])
        self.assertEqual(sorted(c.domain_id for c in claims), [0, 1, 2, 3])

    def test_an_exhausted_range_falls_back_instead_of_failing(self):
        claims = [ros_test_domain.claim() for _ in range(4)]
        self.addCleanup(lambda: [c.close() for c in claims])
        extra = ros_test_domain.claim()
        self.assertIn(extra.domain_id, range(4))
        self.assertIsNone(extra._lock_file)

    def test_a_closed_claim_can_be_taken_again(self):
        claims = [ros_test_domain.claim() for _ in range(4)]
        self.addCleanup(lambda: [c.close() for c in claims])
        freed = claims[2].domain_id
        claims[2].close()
        claims[2].close()  # idempotent
        self.assertEqual(ros_test_domain.claim().domain_id, freed)

    def test_env_sets_the_domain_without_touching_the_input(self):
        with ros_test_domain.claim() as domain:
            base = {"PATH": "/bin"}
            env = domain.env(base)
            self.assertEqual(env["ROS_DOMAIN_ID"], str(domain.domain_id))
            self.assertEqual(env["PATH"], "/bin")
            self.assertNotIn("ROS_DOMAIN_ID", base)

    def test_an_explicit_ros_domain_id_wins(self):
        os.environ["ROS_DOMAIN_ID"] = "42"
        with ros_test_domain.claim() as domain:
            self.assertEqual(domain.domain_id, 42)
            self.assertEqual(domain.env({"ROS_DOMAIN_ID": "42"})["ROS_DOMAIN_ID"], "42")
            # Nothing was locked, so nothing is held against other tests.
            self.assertIsNone(domain._lock_file)


if __name__ == "__main__":
    unittest.main()
