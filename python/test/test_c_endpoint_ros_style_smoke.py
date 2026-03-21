#!/usr/bin/env python3
import sys
import time
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduKey

    endpoint = Endpoint("py_c_endpoint_ros_style_smoke", "inout")
    endpoint.open(str(repo_root / "test/test_pdu_def_endpoint.json"))

    callback_hits = []

    def on_recv(event):
        callback_hits.append((event.key.robot, event.key.channel_id, event.payload))

    key = PduKey(robot="TestRobot", pdu="TestPDU")
    endpoint.on_recv_by_name(key, on_recv)
    endpoint.start()
    endpoint.post_start()
    endpoint.start_dispatch()

    payload = b"\x10\x20\x30\x40\x50\x60\x70\x80"
    endpoint.send_by_name(key, payload)

    deadline = time.time() + 2.0
    while time.time() < deadline and not callback_hits:
        time.sleep(0.01)

    assert callback_hits == [("TestRobot", 123, payload)], callback_hits
    recv_payload = endpoint.recv_by_name(key, 16)
    assert recv_payload == payload, (recv_payload, payload)

    endpoint.stop()
    endpoint.close()

    print("python c_endpoint ros-style smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
