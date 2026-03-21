#!/usr/bin/env python3
import sys
import time
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey

    endpoint = Endpoint("py_c_endpoint_callback_smoke", "inout")
    endpoint.open(str(repo_root / "config/sample/endpoint_internal_cache.json"))

    callback_hits = []
    callback_hits_second = []

    def on_recv(event):
        callback_hits.append((event.key.robot, event.key.channel_id, event.payload))

    def on_recv_second(event):
        callback_hits_second.append((event.key.robot, event.key.channel_id, event.payload))

    key = PduResolvedKey(robot="py_robot_callback", channel_id=7)
    endpoint.on_recv(key, on_recv)
    endpoint.on_recv(key, on_recv_second)
    endpoint.start_dispatch()
    endpoint.start()

    payload = b"\x0a\x0b\x0c\x0d"
    endpoint.send(key, payload)

    deadline = time.time() + 2.0
    while time.time() < deadline and (not callback_hits or not callback_hits_second):
        time.sleep(0.01)

    assert callback_hits == [("py_robot_callback", 7, payload)], callback_hits
    assert callback_hits_second == [("py_robot_callback", 7, payload)], callback_hits_second

    endpoint.stop()
    endpoint.close()

    print("python c_endpoint callback smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
