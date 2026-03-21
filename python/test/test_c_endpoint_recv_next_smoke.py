#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduResolvedKey

    latest_endpoint = Endpoint("py_c_endpoint_recv_next_latest", "inout")
    latest_endpoint.open(str(repo_root / "test/test_endpoint_buffer.json"))
    latest_endpoint.start()

    key_a = PduResolvedKey(robot="py_latest_a", channel_id=101)
    key_b = PduResolvedKey(robot="py_latest_b", channel_id=102)

    latest_endpoint.send(key_a, b"\x01")
    latest_endpoint.send(key_b, b"\x02")
    latest_endpoint.send(key_a, b"\x03")

    record = latest_endpoint.recv_next(16)
    assert (record.key.robot, record.key.channel_id, record.payload) == ("py_latest_a", 101, b"\x03"), record

    record = latest_endpoint.recv_next(16)
    assert (record.key.robot, record.key.channel_id, record.payload) == ("py_latest_b", 102, b"\x02"), record

    try:
        latest_endpoint.recv_next(16)
        raise AssertionError("expected recv_next() to fail with no pending latest entries")
    except Exception as exc:
        assert "err=7" in str(exc), exc

    latest_endpoint.stop()
    latest_endpoint.close()

    queue_endpoint = Endpoint("py_c_endpoint_recv_next_queue", "inout")
    queue_endpoint.open(str(repo_root / "test/test_endpoint_queue.json"))
    queue_endpoint.start()

    key_c = PduResolvedKey(robot="py_queue_a", channel_id=201)
    key_d = PduResolvedKey(robot="py_queue_b", channel_id=202)

    queue_endpoint.send(key_c, b"\x0a")
    queue_endpoint.send(key_d, b"\x0b")
    queue_endpoint.send(key_c, b"\x0c")

    record = queue_endpoint.recv_next(16)
    assert (record.key.robot, record.key.channel_id, record.payload) == ("py_queue_a", 201, b"\x0a"), record

    record = queue_endpoint.recv_next(16)
    assert (record.key.robot, record.key.channel_id, record.payload) == ("py_queue_b", 202, b"\x0b"), record

    record = queue_endpoint.recv_next(16)
    assert (record.key.robot, record.key.channel_id, record.payload) == ("py_queue_a", 201, b"\x0c"), record

    try:
        queue_endpoint.recv_next(16)
        raise AssertionError("expected recv_next() to fail with no pending queue entries")
    except Exception as exc:
        assert "err=7" in str(exc), exc

    queue_endpoint.stop()
    queue_endpoint.close()

    print("python c_endpoint recv_next smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
