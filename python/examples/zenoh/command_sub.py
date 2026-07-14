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
        description="Raspberry Pi-side native Zenoh demo: receive command and publish debuginfo."
    )
    parser.add_argument(
        "config",
        nargs="?",
        type=Path,
        default=script_dir / "config/endpoint_raspberry_pi.json",
        help="Endpoint config path.",
    )
    parser.add_argument("--duration", type=float, default=120.0, help="Seconds to keep the demo running.")
    parser.add_argument("--offset", type=int, default=1000, help="Value added to command when sending debuginfo.")
    args = parser.parse_args()

    add_paths(repo_root)

    from hakoniwa_pdu_endpoint.c_endpoint import Endpoint, PduKey, PduResolvedKey
    from zenoh_demo_pdu import make_uint16_payload, read_uint16_payload

    command_key = PduResolvedKey(robot="demo", channel_id=0)
    debuginfo_key = PduResolvedKey(robot="demo", channel_id=1)
    command_queue: Queue[int] = Queue()

    endpoint = Endpoint("py_zenoh_sub_demo", "inout")

    def on_command(_key, payload: bytes) -> None:
        try:
            command_queue.put(read_uint16_payload(payload))
        except Exception as err:
            print(f"recv command decode_error bytes={len(payload)} error={err}")

    endpoint.open(str(args.config))
    debuginfo_pdu_size = endpoint.get_pdu_size(PduKey(robot="demo", pdu="debuginfo"))
    endpoint.subscribe_on_recv_callback(command_key, on_command)
    endpoint.subscribe_on_recv_callback(debuginfo_key, lambda _key, _payload: None)
    endpoint.start()

    print("waiting for command samples...")
    deadline = time.time() + args.duration
    try:
        while time.time() < deadline:
            try:
                command = command_queue.get(timeout=0.1)
            except Empty:
                continue

            print(f"recv command={command}")
            debuginfo = (command + args.offset) & 0xFFFF
            payload = make_uint16_payload(debuginfo, debuginfo_pdu_size)
            endpoint.send(debuginfo_key, payload)
            print(f"send debuginfo={debuginfo}")
    finally:
        endpoint.stop()
        endpoint.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
