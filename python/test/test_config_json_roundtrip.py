#!/usr/bin/env python3
import json
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]

# Representative existing configuration files. These files are the compatibility
# boundary for Issue #35: loading and re-serializing them must preserve semantic
# JSON equality even when formatting changes.
REPRESENTATIVE_CONFIGS = [
    Path("config/sample/endpoint_tcp_server.json"),
    Path("config/sample/cache/buffer.json"),
    Path("config/sample/comm/storage_latest_out_comm.json"),
    Path("config/sample/endpoint_container.json"),
    Path("test/test_endpoint_buffer.json"),
    Path("test/test_endpoint_queue.json"),
]


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as fp:
        return json.load(fp)


class ConfigJsonRoundTripTest(unittest.TestCase):
    def test_representative_configs_round_trip_semantically(self):
        with tempfile.TemporaryDirectory(prefix="hako-pdu-config-roundtrip-") as temp_dir:
            temp_root = Path(temp_dir)

            for relative_path in REPRESENTATIVE_CONFIGS:
                with self.subTest(config=str(relative_path)):
                    source_path = PROJECT_ROOT / relative_path
                    self.assertTrue(source_path.is_file(), f"missing fixture: {relative_path}")

                    before = load_json(source_path)
                    output_path = temp_root / relative_path.name
                    output_path.write_text(
                        json.dumps(before, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8",
                    )
                    after = load_json(output_path)

                    self.assertEqual(before, after)


if __name__ == "__main__":
    unittest.main()
