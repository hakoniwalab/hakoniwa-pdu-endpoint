#!/usr/bin/env python3
import sys
import time
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey

    endpoint = Endpoint("py_ws_server", "inout")
    config_path = repo_root / "config" / "sample" / "endpoint_websocket_server.json"
    key = PduResolvedKey(robot="py_ws_demo_robot", channel_id=1)
    received = []

    def on_recv(recv_key, payload):
        received.append((recv_key, payload))
        print(f"server callback: robot={recv_key.robot} channel_id={recv_key.channel_id} payload={payload!r}")

    try:
        endpoint.open(str(config_path))
        endpoint.subscribe_on_recv_callback(key, on_recv)
        endpoint.start()
        print("server started: waiting for payload on ws://0.0.0.0:54003/ws")

        deadline = time.time() + 30.0
        while time.time() < deadline:
            if received:
                recv_key, payload = received[0]
                print(f"server received: {payload.decode('utf-8', errors='replace')}")
                return 0
            time.sleep(0.1)

        raise RuntimeError("timeout waiting for payload")
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
