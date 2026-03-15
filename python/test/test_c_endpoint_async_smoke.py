#!/usr/bin/env python3
import sys
import time
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey
    from hakoniwa_pdu_endpoint.c_endpoint_async import EndpointAsync

    endpoint = Endpoint("py_c_endpoint_async_smoke", "inout")
    endpoint.open(str(repo_root / "config/sample/endpoint_internal_cache.json"))

    async_endpoint = EndpointAsync(endpoint)
    callback_hits = []
    callback_hits_second = []

    def on_recv(event):
        callback_hits.append((event.key.robot, event.key.channel_id, event.payload))

    def on_recv_second(event):
        callback_hits_second.append((event.key.robot, event.key.channel_id, event.payload))

    key = PduResolvedKey(robot="py_robot_async", channel_id=7)
    async_endpoint.on_recv(key, on_recv)
    async_endpoint.on_recv(key, on_recv_second)
    async_endpoint.start_dispatch()
    async_endpoint.start()

    payload = b"\x0a\x0b\x0c\x0d"
    async_endpoint.send(key, payload)

    deadline = time.time() + 2.0
    while time.time() < deadline and (not callback_hits or not callback_hits_second):
        time.sleep(0.01)

    assert callback_hits == [("py_robot_async", 7, payload)], callback_hits
    assert callback_hits_second == [("py_robot_async", 7, payload)], callback_hits_second

    async_endpoint.stop()
    async_endpoint.close()

    print("python c_endpoint_async smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
