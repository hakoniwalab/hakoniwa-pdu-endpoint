#!/usr/bin/env python3
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def scalar(value):
    value = value.strip()
    if value in {"true", "True"}:
        return True
    if value in {"false", "False"}:
        return False
    if value in {"null", "Null", "~"}:
        return None
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
        return value[1:-1]
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    return value


def split_key_value(text):
    if ":" not in text:
        raise ValueError(f"invalid YAML entry: {text}")
    key, value = text.split(":", 1)
    return key.strip(), value.strip()


def strip_comment(line):
    quote = None
    for i, ch in enumerate(line):
        if ch in {"'", '"'}:
            quote = ch if quote is None else None if quote == ch else quote
        if ch == "#" and quote is None:
            return line[:i]
    return line


def preprocess_yaml(text):
    rows = []
    for raw in text.splitlines():
        line = strip_comment(raw).rstrip()
        if not line.strip():
            continue
        rows.append((len(line) - len(line.lstrip(" ")), line.lstrip(" ")))
    return rows


def parse_yaml_block(rows, index, indent):
    if index >= len(rows):
        return {}, index
    if rows[index][0] < indent:
        return {}, index
    if rows[index][1].startswith("- "):
        return parse_yaml_list(rows, index, indent)
    return parse_yaml_dict(rows, index, indent)


def parse_yaml_dict(rows, index, indent):
    result = {}
    while index < len(rows):
        row_indent, text = rows[index]
        if row_indent < indent:
            break
        if row_indent > indent:
            raise ValueError(f"unexpected indentation near: {text}")
        if text.startswith("- "):
            break
        key, value = split_key_value(text)
        index += 1
        if value:
            result[key] = scalar(value)
        else:
            result[key], index = parse_yaml_block(rows, index, indent + 2)
    return result, index


def parse_yaml_list(rows, index, indent):
    result = []
    while index < len(rows):
        row_indent, text = rows[index]
        if row_indent < indent:
            break
        if row_indent != indent or not text.startswith("- "):
            break
        item_text = text[2:].strip()
        index += 1
        if not item_text:
            item, index = parse_yaml_block(rows, index, indent + 2)
            result.append(item)
            continue
        if ":" not in item_text:
            result.append(scalar(item_text))
            continue
        key, value = split_key_value(item_text)
        item = {}
        if value:
            item[key] = scalar(value)
        else:
            item[key], index = parse_yaml_block(rows, index, indent + 4)
        while index < len(rows) and rows[index][0] == indent + 2 and not rows[index][1].startswith("- "):
            extra, index = parse_yaml_dict(rows, index, indent + 2)
            item.update(extra)
        result.append(item)
    return result, index


def load_recipe(path):
    if not path:
        return {}
    recipe_path = Path(path)
    text = recipe_path.read_text(encoding="utf-8")
    if recipe_path.suffix == ".json":
        return json.loads(text)
    try:
        import yaml
        loaded = yaml.safe_load(text)
        return loaded or {}
    except ModuleNotFoundError:
        rows = preprocess_yaml(text)
        loaded, index = parse_yaml_block(rows, 0, 0)
        if index != len(rows):
            raise ValueError(f"failed to parse full recipe: {path}")
        return loaded or {}


def as_abs(root, value):
    path = Path(value)
    if path.is_absolute():
        return str(path)
    return str((Path(root) / path).resolve())


def config_ref(from_config, to_config):
    return os.path.relpath(to_config, start=from_config.parent)


def normalize_type_hash_dir(value):
    if not value:
        return None
    root = Path(value)
    if (root / "pdu" / "type_hash").is_dir():
        return root / "pdu" / "type_hash"
    return root


def type_hash_path(type_hash_dir, ros_type):
    parts = ros_type.split("/")
    if len(parts) != 3 or parts[1] != "msg":
        raise ValueError(f"unsupported ROS message type for registry lookup: {ros_type}")
    return type_hash_dir / parts[0] / f"{parts[2]}.json"


def read_type_hash(type_hash_dir, ros_type):
    if not type_hash_dir:
        return ""
    path = type_hash_path(type_hash_dir, ros_type)
    if not path.exists():
        raise FileNotFoundError(f"type hash metadata not found: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("type_name") not in {None, ros_type}:
        raise ValueError(f"type hash metadata type mismatch: {path}: {data.get('type_name')} != {ros_type}")
    value = data.get("hash_string")
    if not value:
        raise ValueError(f"hash_string is missing: {path}")
    return value


def command_type_hash(ros_type):
    if shutil.which("ros2-type-hash"):
        try:
            return subprocess.check_output(["ros2-type-hash", ros_type], text=True, stderr=subprocess.DEVNULL).strip()
        except subprocess.CalledProcessError:
            pass
    if shutil.which("ros2"):
        try:
            out = subprocess.check_output(["ros2", "interface", "type_hash", ros_type], text=True, stderr=subprocess.DEVNULL)
            match = re.search(r"RIHS[0-9A-Za-z_]+", out)
            if match:
                return match.group(0)
        except subprocess.CalledProcessError:
            pass
    return ""


def resolve_type_hash(mapping, args, type_hash_dir, allow_single_override):
    ros2 = mapping["ros2"]
    if ros2.get("type_hash"):
        if args.direction != "in" and ros2["type_hash"] == "*":
            raise ValueError("publisher-capable config requires a concrete type_hash")
        return ros2["type_hash"]
    if args.type_hash and allow_single_override:
        if args.direction != "in" and args.type_hash == "*":
            raise ValueError("publisher-capable config requires a concrete type_hash")
        return args.type_hash
    ros_type = ros2.get("message_type") or args.ros_type
    value = read_type_hash(type_hash_dir, ros_type) if type_hash_dir else ""
    if not value:
        value = command_type_hash(ros_type)
    if not value and args.direction == "in" and os.environ.get("HAKO_RMW_ZENOH_ALLOW_HASH_WILDCARD") == "1":
        return "*"
    if not value:
        raise RuntimeError(
            f"failed to resolve type_hash for {ros_type}; pass --type-hash-dir, --type-hash, "
            "or set ros2.type_hash in the recipe"
        )
    if args.direction != "in" and value == "*":
        raise ValueError("publisher-capable config requires a concrete type_hash")
    return value


def mapping_from_args(args):
    return {
        "endpoint": {
            "robot": args.robot,
            "pdu": args.pdu,
            "notify_on_recv": args.direction != "out",
        },
        "ros2": {
            "topic": args.topic,
            "message_type": args.ros_type,
        },
    }


def parse_args():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--root-dir", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--direction", required=True, choices=["in", "out", "inout"])
    parser.add_argument("--type-hash", default="")
    parser.add_argument("--type-hash-dir", default="")
    parser.add_argument("--topic", required=True)
    parser.add_argument("--ros-type", required=True)
    parser.add_argument("--robot", required=True)
    parser.add_argument("--pdu", required=True)
    parser.add_argument("--domain-id", required=True)
    parser.add_argument("--zenoh-endpoint", required=True)
    parser.add_argument("--recipe", default="")
    return parser.parse_args()


def main():
    args = parse_args()
    recipe = load_recipe(args.recipe)
    root = recipe.get("root_dir", args.root_dir)
    direction = recipe.get("direction", args.direction)
    if direction not in {"in", "out", "inout"}:
        raise ValueError("direction must be in, out, or inout")
    args.direction = direction
    domain_id = int(recipe.get("domain_id", args.domain_id))
    role = {"in": "sub", "out": "pub", "inout": "pubsub"}[direction]

    zenoh = recipe.get("zenoh", {})
    zenoh_endpoint = os.environ.get("HAKO_RMW_ZENOH_ROUTER_ENDPOINT") or zenoh.get("endpoint") or args.zenoh_endpoint
    type_hash_dir = normalize_type_hash_dir(recipe.get("type_hash_dir") or args.type_hash_dir)

    mappings = recipe.get("mappings") or [mapping_from_args(args)]
    if not isinstance(mappings, list) or not mappings:
        raise ValueError("recipe mappings must be a non-empty list")

    comm_mappings = []
    for mapping in mappings:
        endpoint = mapping.get("endpoint", {})
        ros2 = mapping.get("ros2", {})
        if not endpoint.get("robot") or not endpoint.get("pdu") or not ros2.get("topic"):
            raise ValueError("each mapping requires endpoint.robot, endpoint.pdu, and ros2.topic")
        type_hash = resolve_type_hash(mapping, args, type_hash_dir, len(mappings) == 1)
        ros2_out = {
            "topic": ros2["topic"],
            "type_hash": type_hash,
        }
        if ros2.get("type"):
            ros2_out["type"] = ros2["type"]
        if ros2.get("gid"):
            ros2_out["gid"] = ros2["gid"]
        comm_mappings.append({
            "endpoint": {
                "robot": endpoint["robot"],
                "pdu": endpoint["pdu"],
                "notify_on_recv": bool(endpoint.get("notify_on_recv", direction != "out")),
            },
            "ros2": ros2_out,
        })

    out_dir = Path(args.out_dir)
    endpoint_path = out_dir / f"endpoint_rmw_zenoh_{role}.json"
    comm_path = out_dir / f"rmw_zenoh_{role}_comm.json"
    zenoh_path = out_dir / f"zenoh_client_{role}.json5"

    zenoh_config = {
        "mode": "client",
        "connect": {
            "endpoints": [
                zenoh_endpoint,
            ],
        },
    }

    comm = {
        "protocol": "rmw_zenoh",
        "name": recipe.get("name", f"rmw_zenoh_{direction}_manual"),
        "direction": direction,
        "rmw_zenoh": {
            "config_path": config_ref(comm_path, zenoh_path),
            "domain_id": domain_id,
            "mappings": comm_mappings,
        },
    }

    endpoint = {
        "name": recipe.get("endpoint_name", f"sample_rmw_zenoh_{direction}_endpoint_manual"),
        "pdu_def_path": as_abs(root, recipe.get("pdu_def_path", "config/sample/comm/storage_example/pdudef.json")),
        "cache": as_abs(root, recipe.get("cache", "config/sample/cache/buffer.json")),
        "comm": config_ref(endpoint_path, comm_path),
    }

    for path, data in [
        (zenoh_path, zenoh_config),
        (comm_path, comm),
        (endpoint_path, endpoint),
    ]:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    print(endpoint_path)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"make-rmw-zenoh-config: {e}", file=sys.stderr)
        raise SystemExit(1)
