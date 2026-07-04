#!/usr/bin/env python3
import os
import sys
import time
from pathlib import Path


def add_registry_python_path(repo_root: Path) -> None:
    registry_pdu = Path(
        os.environ.get(
            "HAKO_PDU_REGISTRY_PDU_PATH",
            repo_root.parent / "hakoniwa-pdu-registry" / "pdu",
        )
    ).expanduser().resolve()
    sys.path.insert(0, str(registry_pdu))


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))
    add_registry_python_path(repo_root)

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey
    from python.std_msgs.pdu_cdr_conv_UInt64 import py_to_cdr_UInt64
    from python.std_msgs.pdu_pytype_UInt64 import UInt64

    config_path = (
        Path(sys.argv[1])
        if len(sys.argv) > 1
        else repo_root / "config/sample/endpoint_rmw_zenoh_pub.json"
    )

    endpoint = Endpoint("py_rmw_zenoh_pub_cdr_example", "out")
    endpoint.open(str(config_path))
    endpoint.start()

    key = PduResolvedKey(robot="StorageDemo", channel_id=0)
    try:
        for value in range(1, 6):
            msg = UInt64()
            msg.data = value
            payload = py_to_cdr_UInt64(msg)
            endpoint.send(key, payload)
            print(f"published sample_state_cdr={value} bytes={len(payload)}")
            time.sleep(0.5)
    finally:
        endpoint.stop()
        endpoint.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
