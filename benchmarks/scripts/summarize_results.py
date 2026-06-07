#!/usr/bin/env python3
"""Summarize benchmark log directories into CSV and JSON result files."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any, Dict, Iterable


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


def read_summary(path: Path, summary_type: str) -> Dict[str, str]:
    summary: Dict[str, str] | None = None
    for record_type, values in iter_records(path):
        if record_type == summary_type:
            summary = values
    if summary is None:
        raise RuntimeError(f"{summary_type} not found: {path}")
    return summary


def ns_to_ms(delta_ns: int) -> float:
    return delta_ns / 1_000_000.0


def load_pdu_types(path: Path) -> Dict[str, Dict[str, Any]]:
    with path.open("r", encoding="utf-8") as f:
        items = json.load(f)
    return {item["name"]: item for item in items}


def summarize_pair(environment: str, pdu_name: str, pdu_types: Dict[str, Dict[str, Any]], pub_log: Path, sub_log: Path) -> Dict[str, Any]:
    pub = read_summary(pub_log, "BENCH_PUB_SUMMARY")
    sub = read_summary(sub_log, "BENCH_SUB_SUMMARY")
    if pub["protocol"] != sub["protocol"]:
        raise RuntimeError(f"Protocol mismatch: pub={pub['protocol']} sub={sub['protocol']}")

    sent = int(pub["sent"])
    received = int(sub["received"])
    pdu_size = int(pdu_types.get(pdu_name, {}).get("pdu_size", 0))
    total_bytes = sent * pdu_size

    send_duration_ms = float(pub["send_duration_ms"])
    recv_span_ms = float(sub["recv_span_ms"])
    send_start_ns = int(pub["send_start_ns"])
    send_end_ns = int(pub["send_end_ns"])
    first_recv_ns = int(sub["first_recv_ns"])
    last_recv_ns = int(sub["last_recv_ns"])

    first_receive_latency_ms = ns_to_ms(first_recv_ns - send_start_ns) if first_recv_ns else 0.0
    end_to_end_ms = ns_to_ms(last_recv_ns - send_start_ns)
    tail_after_send_ms = ns_to_ms(last_recv_ns - send_end_ns)
    loss_count = sent - received
    loss_rate = (loss_count / sent) if sent > 0 else 0.0
    send_throughput_mbps = (total_bytes / 1_000_000.0) / (send_duration_ms / 1000.0) if send_duration_ms > 0 else 0.0
    end_to_end_throughput_mbps = (total_bytes / 1_000_000.0) / (end_to_end_ms / 1000.0) if end_to_end_ms > 0 else 0.0

    return {
        "environment": environment,
        "pdu_name": pdu_name,
        "protocol": pub["protocol"],
        "pdu_size": pdu_size,
        "total_bytes": total_bytes,
        "expected": int(pub["expected"]),
        "sent": sent,
        "received": received,
        "completed": 1 if sub["completed"] == "1" else 0,
        "loss_count": loss_count,
        "loss_rate": loss_rate,
        "send_duration_ms": send_duration_ms,
        "recv_span_ms": recv_span_ms,
        "first_receive_latency_ms": first_receive_latency_ms,
        "end_to_end_ms": end_to_end_ms,
        "tail_after_send_ms": tail_after_send_ms,
        "publisher_send_MBps": send_throughput_mbps,
        "end_to_end_MBps": end_to_end_throughput_mbps,
        "pub_log": str(pub_log),
        "sub_log": str(sub_log),
    }


def discover_results(logs_dir: Path, pdu_types: Dict[str, Dict[str, Any]]) -> list[Dict[str, Any]]:
    rows: list[Dict[str, Any]] = []
    child_dirs = sorted(path for path in logs_dir.iterdir() if path.is_dir())
    nested_layout = any((path / "disturb").is_dir() or (path / "hako_camera_data").is_dir() or (path / "lidar_points").is_dir() for path in child_dirs)

    if nested_layout:
        for env_dir in child_dirs:
            environment = env_dir.name
            for pdu_dir in sorted(path for path in env_dir.iterdir() if path.is_dir()):
                pdu_name = pdu_dir.name
                for pub_log in sorted(pdu_dir.glob("benchmark-*_pub.log")):
                    protocol = pub_log.name.removeprefix("benchmark-").removesuffix("_pub.log")
                    sub_log = pdu_dir / f"benchmark-{protocol}_sub.log"
                    if not sub_log.exists():
                        continue
                    rows.append(summarize_pair(environment, pdu_name, pdu_types, pub_log, sub_log))
    else:
        for pdu_dir in child_dirs:
            pdu_name = pdu_dir.name
            for pub_log in sorted(pdu_dir.glob("benchmark-*_pub.log")):
                protocol = pub_log.name.removeprefix("benchmark-").removesuffix("_pub.log")
                sub_log = pdu_dir / f"benchmark-{protocol}_sub.log"
                if not sub_log.exists():
                    continue
                rows.append(summarize_pair("default", pdu_name, pdu_types, pub_log, sub_log))
    return rows


def write_csv(path: Path, rows: list[Dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "environment",
        "pdu_name",
        "protocol",
        "pdu_size",
        "total_bytes",
        "expected",
        "sent",
        "received",
        "completed",
        "loss_count",
        "loss_rate",
        "send_duration_ms",
        "recv_span_ms",
        "first_receive_latency_ms",
        "end_to_end_ms",
        "tail_after_send_ms",
        "publisher_send_MBps",
        "end_to_end_MBps",
        "pub_log",
        "sub_log",
    ]
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize benchmark log directories.")
    parser.add_argument("--logs-dir", type=Path, default=Path("benchmarks/logs"))
    parser.add_argument("--pdu-types", type=Path, default=Path("benchmarks/configs/pdu/pdutypes.json"))
    parser.add_argument("--out-csv", type=Path, default=Path("benchmarks/results/summary.csv"))
    parser.add_argument("--out-json", type=Path, default=Path("benchmarks/results/summary.json"))
    args = parser.parse_args()

    pdu_types = load_pdu_types(args.pdu_types)
    rows = discover_results(args.logs_dir, pdu_types)

    write_csv(args.out_csv, rows)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    with args.out_json.open("w", encoding="utf-8") as f:
        json.dump(rows, f, indent=2)
        f.write("\n")

    print(f"wrote {args.out_csv}")
    print(f"wrote {args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
