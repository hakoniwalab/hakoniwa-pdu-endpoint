#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey

    endpoint = Endpoint("py_example_internal_cache", "inout")
    endpoint.open(str(repo_root / "config/sample/endpoint_internal_cache.json"))
    endpoint.start()

    key = PduResolvedKey(robot="py_example_robot", channel_id=1)
    payload = b"\x01\x02\x03\x04"
    endpoint.send(key, payload)
    recv_payload = endpoint.recv(key, 16)

    print("received:", list(recv_payload))

    endpoint.stop()
    endpoint.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
