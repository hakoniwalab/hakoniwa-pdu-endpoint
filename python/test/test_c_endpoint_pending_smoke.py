from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "python"))

from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, EndpointError, PduResolvedKey  # noqa: E402


def expect_no_entry(func) -> None:
    try:
        func()
    except EndpointError as ex:
        if ex.err_code == 7:
            return
        raise
    raise AssertionError("expected HAKO_PDU_ERR_NO_ENTRY")


def main() -> None:
    endpoint = Endpoint("python_pending_smoke", "inout")
    endpoint.open("test/test_endpoint_buffer.json")
    endpoint.start()

    key_a = PduResolvedKey(robot="python_pending_a", channel_id=91)
    key_b = PduResolvedKey(robot="python_pending_b", channel_id=92)

    endpoint.set_recv_event(key_a)
    endpoint.set_recv_event(key_b)

    endpoint.send(key_a, b"\x01")
    endpoint.send(key_b, b"\x02")
    endpoint.send(key_a, b"\x03")

    pending = endpoint.get_pending_count()
    assert pending == 2, pending

    record = endpoint.recv_next(16)
    assert record.key == key_a
    assert record.payload == b"\x03"

    record = endpoint.recv_next(16)
    assert record.key == key_b
    assert record.payload == b"\x02"

    expect_no_entry(lambda: endpoint.recv_next(16))

    endpoint.stop()
    endpoint.close()


if __name__ == "__main__":
    main()
