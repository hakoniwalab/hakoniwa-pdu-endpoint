#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint import c_endpoint

    endpoint = c_endpoint.Endpoint("py_c_endpoint_smoke", "inout")
    endpoint.open(str(repo_root / "config/sample/endpoint_internal_cache.json"))

    callback_hits = []

    def on_recv(key, payload):
        callback_hits.append((key.robot, key.channel_id, payload))

    endpoint.subscribe_on_recv_callback(
        c_endpoint.PduResolvedKey(robot="py_robot", channel_id=5),
        on_recv,
    )
    endpoint.start()

    key = c_endpoint.PduResolvedKey(robot="py_robot", channel_id=5)
    payload = b"\x01\x02\x03\x04"
    endpoint.send(key, payload)
    recv_payload = endpoint.recv(key, 16)
    assert recv_payload == payload, (recv_payload, payload)
    assert endpoint.is_running() is True
    assert callback_hits == [("py_robot", 5, payload)], callback_hits

    endpoint.stop()
    endpoint.close()

    print("python c_endpoint smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
