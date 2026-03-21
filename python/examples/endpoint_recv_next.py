#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey

    print("latest runtime receive semantics")
    latest_endpoint = Endpoint("py_example_recv_next_latest", "inout")
    latest_endpoint.open(str(repo_root / "test/test_endpoint_buffer.json"))
    latest_endpoint.start()

    latest_key_a = PduResolvedKey(robot="py_example_latest_a", channel_id=11)
    latest_key_b = PduResolvedKey(robot="py_example_latest_b", channel_id=12)
    latest_endpoint.send(latest_key_a, b"\x01")
    latest_endpoint.send(latest_key_b, b"\x02")
    latest_endpoint.send(latest_key_a, b"\x03")

    while True:
        try:
            record = latest_endpoint.recv_next(16)
        except Exception:
            break
        print("latest recv_next:", record.key.robot, record.key.channel_id, list(record.payload))

    latest_endpoint.stop()
    latest_endpoint.close()

    print("queue runtime receive semantics")
    queue_endpoint = Endpoint("py_example_recv_next_queue", "inout")
    queue_endpoint.open(str(repo_root / "test/test_endpoint_queue.json"))
    queue_endpoint.start()

    queue_key_a = PduResolvedKey(robot="py_example_queue_a", channel_id=21)
    queue_key_b = PduResolvedKey(robot="py_example_queue_b", channel_id=22)
    queue_endpoint.send(queue_key_a, b"\x0a")
    queue_endpoint.send(queue_key_b, b"\x0b")
    queue_endpoint.send(queue_key_a, b"\x0c")

    while True:
        try:
            record = queue_endpoint.recv_next(16)
        except Exception:
            break
        print("queue recv_next:", record.key.robot, record.key.channel_id, list(record.payload))

    queue_endpoint.stop()
    queue_endpoint.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
