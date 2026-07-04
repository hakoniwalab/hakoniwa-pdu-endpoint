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
    from python.std_msgs.pdu_cdr_conv_UInt64 import cdr_to_py_UInt64

    config_path = (
        Path(sys.argv[1])
        if len(sys.argv) > 1
        else repo_root / "config/sample/endpoint_rmw_zenoh_sub.json"
    )

    endpoint = Endpoint("py_rmw_zenoh_sub_cdr_example", "in")

    def on_recv(key, payload: bytes) -> None:
        try:
            msg = cdr_to_py_UInt64(payload)
            print(f"received sample_state_cdr={msg.data} bytes={len(payload)}")
        except Exception as err:
            print(f"received undecodable CDR payload bytes={len(payload)} error={err}")

    key = PduResolvedKey(robot="StorageDemo", channel_id=0)
    endpoint.open(str(config_path))
    endpoint.subscribe_on_recv_callback(key, on_recv)
    endpoint.start()

    print("Waiting for Zenoh CDR samples...")
    try:
        time.sleep(5.0)
    finally:
        endpoint.stop()
        endpoint.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
