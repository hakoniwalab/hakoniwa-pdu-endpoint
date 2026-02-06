#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def build_tcp(args):
    comm = {
        "protocol": "tcp",
        "name": args.name,
        "direction": args.direction,
        "role": args.role,
    }
    if args.role == "server":
        comm["local"] = {"address": args.address, "port": args.port}
    else:
        comm["remote"] = {"address": args.remote_address, "port": args.remote_port}
    return comm


def build_tcp_mux(args):
    comm = {
        "protocol": "tcp",
        "name": args.name,
        "direction": args.direction,
        "local": {"address": args.address, "port": args.port},
        "expected_clients": args.expected_clients,
    }
    return comm


def build_websocket(args):
    comm = {
        "protocol": "websocket",
        "name": args.name,
        "direction": args.direction,
        "role": args.role,
    }
    if args.role == "server":
        comm["local"] = {"port": args.port}
    else:
        comm["remote"] = {
            "host": args.remote_address,
            "port": args.remote_port,
            "path": args.ws_path,
        }
    return comm


def build_udp(args):
    comm = {
        "protocol": "udp",
        "name": args.name,
        "direction": args.direction,
        "pdu_key": {"robot": args.robot, "channel_id": args.channel_id},
    }
    if args.direction in ("in", "inout"):
        comm["local"] = {"address": args.address, "port": args.port}
    if args.direction in ("out", "inout"):
        comm["remote"] = {"address": args.remote_address, "port": args.remote_port}
    return comm


def build_shm(args):
    comm = {
        "protocol": "shm",
        "name": args.name,
        "impl_type": args.shm_impl,
        "io": {
            "robots": [
                {
                    "name": args.robot,
                    "pdu": [
                        {
                            "name": args.shm_pdu,
                            "notify_on_recv": args.shm_notify_on_recv,
                        }
                    ],
                }
            ]
        },
    }
    if args.shm_impl == "poll":
        comm["asset_name"] = args.shm_asset_name
    return comm


def apply_preset(args):
    presets = {
        "tcp_basic_server": {
            "protocol": "tcp",
            "direction": "inout",
            "role": "server",
            "address": "0.0.0.0",
            "port": 54001,
            "cache": "config/sample/cache/queue.json",
        },
        "tcp_basic_client": {
            "protocol": "tcp",
            "direction": "inout",
            "role": "client",
            "remote_address": "127.0.0.1",
            "remote_port": 54001,
            "cache": "config/sample/cache/queue.json",
        },
        "udp_oneway": {
            "protocol": "udp",
            "direction": "out",
            "remote_address": "127.0.0.1",
            "remote_port": 9001,
            "robot": "ExampleRobot",
            "channel_id": 0,
            "cache": "config/sample/cache/buffer.json",
        },
        "internal_cache": {
            "internal_cache": True,
            "cache": "config/sample/cache/buffer.json",
        },
        "tcp_mux_basic": {
            "protocol": "tcp",
            "direction": "inout",
            "tcp_mux": True,
            "expected_clients": 2,
            "address": "0.0.0.0",
            "port": 54001,
            "cache": "config/sample/cache/buffer.json",
        },
    }
    preset = presets.get(args.preset)
    if not preset:
        return
    for key, value in preset.items():
        setattr(args, key, value)


def main():
    parser = argparse.ArgumentParser(
        description="Generate minimal Endpoint + Comm config files."
    )
    parser.add_argument("--preset", choices=["tcp_basic_server", "tcp_basic_client", "udp_oneway", "internal_cache", "tcp_mux_basic"], help="Use a fixed preset (explicitly defined, no inference).")
    parser.add_argument("--protocol", choices=["tcp", "udp", "websocket", "shm"])
    parser.add_argument("--direction", choices=["in", "out", "inout"])
    parser.add_argument("--role", choices=["server", "client"], help="Required for tcp/websocket")
    parser.add_argument("--name", required=True, help="Base name for endpoint and comm")

    parser.add_argument("--address", default="0.0.0.0", help="Local bind address")
    parser.add_argument("--port", type=int, default=54001, help="Local port")
    parser.add_argument("--remote-address", default="127.0.0.1", help="Remote address")
    parser.add_argument("--remote-port", type=int, default=54001, help="Remote port")
    parser.add_argument("--ws-path", default="/", help="WebSocket path")

    parser.add_argument("--robot", default="ExampleRobot", help="UDP/SHM robot name")
    parser.add_argument("--channel-id", type=int, default=0, help="UDP pdu_key.channel_id")

    parser.add_argument("--shm-impl", default="poll", choices=["poll", "callback"], help="SHM impl_type")
    parser.add_argument("--shm-asset-name", default="Asset", help="SHM asset_name (poll only)")
    parser.add_argument("--shm-pdu", default="Pdu", help="SHM PDU name")
    parser.add_argument("--shm-notify-on-recv", action="store_true", help="SHM notify_on_recv")

    parser.add_argument("--cache", default="config/sample/cache/queue.json", help="Cache config path")
    parser.add_argument("--internal-cache", action="store_true", help="Generate endpoint with comm: null")
    parser.add_argument("--tcp-mux", action="store_true", help="Generate TCP mux comm (server-only)")
    parser.add_argument("--expected-clients", type=int, default=2, help="Expected clients for TCP mux")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    parser.add_argument("--endpoint-out", help="Endpoint output path (overrides --out-dir)")
    parser.add_argument("--comm-out", help="Comm output path (overrides --out-dir)")
    args = parser.parse_args()

    if args.preset:
        apply_preset(args)
        print(f"Using preset: {args.preset}")
        if args.protocol is None and not args.internal_cache:
            parser.error("--preset requires protocol to be set by preset")
    if args.protocol is None and not args.internal_cache:
        parser.error("--protocol is required unless --internal-cache is used")
    if args.direction is None and not args.internal_cache:
        parser.error("--direction is required unless --internal-cache is used")
    if args.internal_cache and args.tcp_mux:
        parser.error("--internal-cache and --tcp-mux are mutually exclusive")
    if args.tcp_mux and args.protocol != "tcp":
        parser.error("--tcp-mux requires --protocol tcp")
    if args.protocol in ("tcp", "websocket") and not args.role and not args.tcp_mux:
        parser.error("--role is required for tcp/websocket")

    if args.internal_cache:
        comm = None
    elif args.tcp_mux:
        comm = build_tcp_mux(args)
    elif args.protocol == "tcp":
        comm = build_tcp(args)
    elif args.protocol == "websocket":
        comm = build_websocket(args)
    elif args.protocol == "shm":
        comm = build_shm(args)
    else:
        comm = build_udp(args)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    endpoint_path = Path(args.endpoint_out) if args.endpoint_out else out_dir / f"endpoint_{args.name}.json"
    comm_path = Path(args.comm_out) if args.comm_out else out_dir / f"comm_{args.name}.json"

    endpoint = {
        "name": args.name,
        "cache": args.cache,
        "comm": None if comm is None else (str(comm_path.relative_to(out_dir)) if not comm_path.is_absolute() else str(comm_path)),
    }

    endpoint_path.write_text(json.dumps(endpoint, indent=2) + "\n", encoding="utf-8")
    if comm is not None:
        comm_path.write_text(json.dumps(comm, indent=2) + "\n", encoding="utf-8")

    print(f"Wrote {endpoint_path}")
    if comm is not None:
        print(f"Wrote {comm_path}")
    print("Notes:")
    if comm is None:
        print("- comm is null (internal cache only). Add comm settings if you need network transport.")
    else:
        if args.protocol in ("tcp", "udp", "websocket"):
            print("- Consider adding comm_raw_version if you need v1 framing.")
            print("- Tune comm options (timeouts/buffers) for your environment.")
        if args.protocol == "udp" and args.direction == "inout" and "remote" not in comm:
            print("- UDP inout may require a remote target; add remote if needed.")
        if args.protocol in ("tcp", "websocket") and args.role == "client":
            print("- Verify remote address/port settings for client mode.")
        if args.protocol == "shm":
            print("- Provide full SHM robot/PDU definitions and align with your asset configuration.")
    print("- Add pdu_def_path to the endpoint if you need name-based PDU access.")
    print("- Validate configs with validate_json --check-paths.")


if __name__ == "__main__":
    raise SystemExit(main())
