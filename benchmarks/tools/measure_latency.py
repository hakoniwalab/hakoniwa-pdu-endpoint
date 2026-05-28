#!/usr/bin/env python3
"""Analyze Hakoniwa PDU endpoint benchmark logs.

This tool reads publisher/subscriber logs containing BENCH_* records and reports
batch-level timing such as send start to final receive time.

Example:
    python3 benchmarks/tools/measure_latency.py \
        --pub-log pub.log \
        --sub-log sub.log
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Optional


@dataclass
class PubSummary:
    protocol: str
    expected: int
    sent: int
    send_start_ns: int
    send_end_ns: int
    send_duration_ms: float


@dataclass
class SubSummary:
    protocol: str
    expected: int
    received: int
    first_recv_ns: int
    last_recv_ns: int
    recv_span_ms: float
    completed: bool


def parse_kv_line(line: str) -> tuple[str, Dict[str, str]] | None:
    line = line.strip()
    if not line.startswith("BENCH_"):
        return None

    parts = line.split()
    if not parts:
        return None

    record_type = parts[0]
    values: Dict[str, str] = {}
    for part in parts[1:]:
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        values[key] = value
    return record_type, values


def iter_records(path: Path) -> Iterable[tuple[str, Dict[str, str]]]:
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            parsed = parse_kv_line(line)
            if parsed is not None:
                yield parsed


def read_pub_summary(path: Path) -> PubSummary:
    summary: Optional[PubSummary] = None
    for record_type, values in iter_records(path):
        if record_type != "BENCH_PUB_SUMMARY":
            continue
        summary = PubSummary(
            protocol=values["protocol"],
            expected=int(values["expected"]),
            sent=int(values["sent"]),
            send_start_ns=int(values["send_start_ns"]),
            send_end_ns=int(values["send_end_ns"]),
            send_duration_ms=float(values["send_duration_ms"]),
        )
    if summary is None:
        raise RuntimeError(f"BENCH_PUB_SUMMARY not found: {path}")
    return summary


def read_sub_summary(path: Path) -> SubSummary:
    summary: Optional[SubSummary] = None
    for record_type, values in iter_records(path):
        if record_type != "BENCH_SUB_SUMMARY":
            continue
        summary = SubSummary(
            protocol=values["protocol"],
            expected=int(values["expected"]),
            received=int(values["received"]),
            first_recv_ns=int(values["first_recv_ns"]),
            last_recv_ns=int(values["last_recv_ns"]),
            recv_span_ms=float(values["recv_span_ms"]),
            completed=(values["completed"] == "1"),
        )
    if summary is None:
        raise RuntimeError(f"BENCH_SUB_SUMMARY not found: {path}")
    return summary


def ns_to_ms(delta_ns: int) -> float:
    return delta_ns / 1_000_000.0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compute batch latency from Hakoniwa PDU endpoint benchmark logs."
    )
    parser.add_argument("--pub-log", required=True, type=Path, help="Publisher log file")
    parser.add_argument("--sub-log", required=True, type=Path, help="Subscriber log file")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Return non-zero when sent/received/completed counts do not match expected values",
    )
    args = parser.parse_args()

    pub = read_pub_summary(args.pub_log)
    sub = read_sub_summary(args.sub_log)

    if pub.protocol != sub.protocol:
        raise RuntimeError(f"Protocol mismatch: pub={pub.protocol} sub={sub.protocol}")

    end_to_end_ms = ns_to_ms(sub.last_recv_ns - pub.send_start_ns)
    first_receive_latency_ms = ns_to_ms(sub.first_recv_ns - pub.send_start_ns) if sub.first_recv_ns else 0.0
    tail_after_send_ms = ns_to_ms(sub.last_recv_ns - pub.send_end_ns)
    loss_count = pub.sent - sub.received
    loss_rate = (loss_count / pub.sent) if pub.sent > 0 else 0.0

    print(f"protocol={pub.protocol}")
    print(f"expected={pub.expected}")
    print(f"sent={pub.sent}")
    print(f"received={sub.received}")
    print(f"completed={1 if sub.completed else 0}")
    print(f"loss_count={loss_count}")
    print(f"loss_rate={loss_rate:.6f}")
    print(f"send_duration_ms={pub.send_duration_ms:.6f}")
    print(f"recv_span_ms={sub.recv_span_ms:.6f}")
    print(f"first_receive_latency_ms={first_receive_latency_ms:.6f}")
    print(f"end_to_end_ms={end_to_end_ms:.6f}")
    print(f"tail_after_send_ms={tail_after_send_ms:.6f}")

    if args.strict:
        if pub.sent != pub.expected or sub.received != sub.expected or not sub.completed:
            return 2
        if pub.expected != sub.expected:
            return 2
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        raise SystemExit(1)
