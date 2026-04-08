#!/usr/bin/env python3
import sys
import time
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey

    endpoint = Endpoint("py_ws_client", "inout")
    config_path = repo_root / "config" / "sample" / "endpoint_websocket_client.json"
    key = PduResolvedKey(robot="py_ws_demo_robot", channel_id=1)
    payload = b"hakoniwa websocket demo"

    try:
        endpoint.open(str(config_path))
        endpoint.start()
        time.sleep(1.0)
        endpoint.send(key, payload)
        print(f"client sent: {payload.decode('utf-8')}")
        return 0
    finally:
        try:
            endpoint.stop()
        except Exception:
            pass
        try:
            endpoint.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
