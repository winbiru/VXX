import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from shard_native_golden import is_independent, load_costs, shard


class NativeGoldenShardingTests(unittest.TestCase):
    def test_balances_expensive_fixtures_without_reordering_shards(self):
        sources = [
            "src/tests/slow_a.vi",
            "src/tests/fast_a.vi",
            "src/tests/slow_b.vi",
            "src/tests/fast_b.vi",
        ]
        groups, loads = shard(sources, 2, {"slow_a": 9000, "slow_b": 9000})
        self.assertEqual(loads, [11000, 11000])
        self.assertCountEqual([item for group in groups for item in group], sources)
        for group in groups:
            self.assertEqual(group, [source for source in sources if source in group])

    def test_keeps_duplicate_source_on_one_worker(self):
        duplicated = "src/tests/kiểm tra tên tiếng Việt.vi"
        groups, loads = shard([duplicated, "x.vi", duplicated, "y.vi"], 2, {})
        self.assertEqual(sum(group.count(duplicated) for group in groups), 2)
        self.assertIn(2, [group.count(duplicated) for group in groups])
        self.assertEqual(sum(loads), 8000)

    def test_keeps_external_state_in_order_and_fills_idle_lane(self):
        ordered = ["src/tests/state_a.vi", "src/tests/state_b.vi"]
        sources = ["src/tests/slow.vi", "src/tests/small_a.vi", "src/tests/small_b.vi"]
        groups, loads = shard(
            sources, 2,
            {"state_a": 1000, "state_b": 1000, "slow": 4000,
             "small_a": 1000, "small_b": 1000},
            ordered,
        )
        self.assertEqual(groups[0][:2], ordered)
        self.assertCountEqual([item for group in groups for item in group], ordered + sources)
        self.assertEqual(loads, [4000, 4000])
        for group in groups:
            independent_in_group = [source for source in group if source in sources]
            self.assertEqual(independent_in_group, [s for s in sources if s in group])

    def test_rejects_source_both_ordered_and_independent(self):
        with self.assertRaisesRegex(ValueError, "overlap"):
            shard(["x.vi"], 2, {}, ["x.vi"])

    def test_loads_costs_and_rejects_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            costs = Path(directory) / "costs.tsv"
            costs.write_text("# comment\nslow\t10000\n", encoding="utf-8")
            self.assertEqual(load_costs(costs), {"slow": 10000})
            costs.write_text("slow\t10000\nslow\t20000\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                load_costs(costs)

    def test_windows_state_classification_matches_unix_policy(self):
        for name in (
            "kiem_tra_ffi_socket.vi", "kiem_tra_goi_thoi_gian.vi",
            "kiem_tra_api_db_project.vi", "kiem_tra_ffi_tep.vi",
            "kiem_tra_goi_dong_thoi.vi", "kiem_tra_http_transport_bat_loi.vi",
        ):
            self.assertFalse(is_independent("src/tests/" + name), name)
        for name in (
            "kiem_tra_su_kien.vi", "kiem_tra_goi_luong_tu.vi",
            "kiem_tra_socket_ip.vi", "kiem_tra_http_thuan_vpp.vi",
            "kiem_tra_so_nguyen.vi", "kiem_tra_ham.vi",
        ):
            self.assertTrue(is_independent("src\\tests\\" + name), name)

    def test_manifest_auto_partition_keeps_external_state_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = [
                "src/tests/kiem_tra_ffi_tep.vi", "src/tests/kiem_tra_ham.vi",
                "src/tests/kiem_tra_goi_thoi_gian.vi", "src/tests/kiem_tra_so_nguyen.vi",
            ]
            (root / "manifest.list").write_text("\n".join(manifest) + "\n", encoding="utf-8")
            (root / "costs.tsv").write_text("kiem_tra_ham\t2000\n", encoding="utf-8")
            subprocess.run([
                sys.executable, str(Path(__file__).with_name("shard_native_golden.py")),
                "--workers", "2", "--costs", str(root / "costs.tsv"),
                "--output", str(root / "groups"), "--sources-file",
                str(root / "manifest.list"), "--auto-classify",
            ], check=True, capture_output=True, text=True)
            group0 = (root / "groups/0.list").read_text(encoding="utf-8").splitlines()
            group1 = (root / "groups/1.list").read_text(encoding="utf-8").splitlines()
            self.assertEqual(group0[:2], [manifest[0], manifest[2]])
            self.assertCountEqual(group0 + group1, manifest)


if __name__ == "__main__":
    unittest.main()
