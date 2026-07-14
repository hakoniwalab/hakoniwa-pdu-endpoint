#!/usr/bin/env python3
import argparse
import sys
import time
from pathlib import Path
from queue import Empty, Queue


def add_paths(repo_root: Path) -> None:
    sys.path.insert(0, str(repo_root / "python"))


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    repo_root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(
        description="Mac-side native Zenoh demo: publish command and receive debuginfo."
    )
    parser.add_argument(
        "config",
        nargs="?",
        type=Path,
        default=script_dir / "config/endpoint_mac.json",
        help="Endpoint config path.",
    )
    parser.add_argument("--count", type=int, default=10, help="Number of command samples to send.")
    parser.add_argument("--interval", type=float, default=0.1, help="Seconds between command samples.")
    parser.add_argument("--initial-delay", type=float, default=0.0, help="Seconds to wait before first send.")
    parser.add_argument("--start", type=int, default=1, help="Initial command value.")
    args = parser.parse_args()

    add_paths(repo_root)

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduKey, PduResolvedKey
    from zenoh_demo_pdu import make_uint16_payload, read_uint16_payload

    command_key = PduResolvedKey(robot="demo", channel_id=0)
    debuginfo_key = PduResolvedKey(robot="demo", channel_id=1)
    debuginfo_queue: Queue[int] = Queue()

    endpoint = Endpoint("py_zenoh_pub_demo", "inout")

    def on_debuginfo(_key, payload: bytes) -> None:
        try:
            debuginfo_queue.put(read_uint16_payload(payload))
        except Exception as err:
            print(f"recv debuginfo decode_error bytes={len(payload)} error={err}")

    endpoint.open(str(args.config))
    command_pdu_size = endpoint.get_pdu_size(PduKey(robot="demo", pdu="command"))
    endpoint.subscribe_on_recv_callback(command_key, lambda _key, _payload: None)
    endpoint.subscribe_on_recv_callback(debuginfo_key, on_debuginfo)
    endpoint.start()

    try:
        if args.initial_delay > 0.0:
            time.sleep(args.initial_delay)
        for offset in range(args.count):
            value = (args.start + offset) & 0xFFFF
            payload = make_uint16_payload(value, command_pdu_size)
            endpoint.send(command_key, payload)
            print(f"send command={value}")

            deadline = time.time() + args.interval
            while True:
                timeout = max(0.0, deadline - time.time())
                try:
                    debuginfo = debuginfo_queue.get(timeout=timeout)
                    print(f"recv debuginfo={debuginfo}")
                except Empty:
                    break
                if time.time() >= deadline:
                    break
    finally:
        endpoint.stop()
        endpoint.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
