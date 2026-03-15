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

    endpoint = Endpoint("py_example_async", "inout")
    endpoint.open(str(repo_root / "config/sample/endpoint_internal_cache.json"))

    async_endpoint = EndpointAsync(endpoint)

    def on_recv(event):
        print("async callback:", event.key.robot, event.key.channel_id, list(event.payload))

    key = PduResolvedKey(robot="py_example_async_robot", channel_id=2)
    async_endpoint.on_recv(key, on_recv)
    async_endpoint.start_dispatch()
    async_endpoint.start()

    async_endpoint.send(key, b"\x0a\x0b\x0c\x0d")
    time.sleep(0.1)

    async_endpoint.stop()
    async_endpoint.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
