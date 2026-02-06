#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:  # pragma: no cover - runtime dependency check
    print("Missing dependency: jsonschema. Install with: pip install jsonschema", file=sys.stderr)
    sys.exit(2)


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def iter_json_files(paths):
    for p in paths:
        path = Path(p)
        if path.is_dir():
            for child in sorted(path.rglob("*.json")):
                yield child
        else:
            yield path


def resolve_ref_path(base_dir: Path, raw_value: str) -> Path:
    p = Path(raw_value)
    if p.is_absolute():
        return p
    return (base_dir / p).resolve()


def iter_ref_paths_from_json(data):
    # Endpoint config (single object or array of objects).
    if isinstance(data, dict):
        for key in ("cache", "comm", "pdu_def_path"):
            if key in data and data[key] is not None:
                yield f"/{key}", key, data[key]
        if "endpoints" in data and isinstance(data["endpoints"], list):
            for idx, entry in enumerate(data["endpoints"]):
                if isinstance(entry, dict) and "config_path" in entry:
                    yield f"/endpoints/{idx}/config_path", "config_path", entry["config_path"]
    elif isinstance(data, list):
        for idx, item in enumerate(data):
            if isinstance(item, dict):
                for key in ("cache", "comm", "pdu_def_path"):
                    if key in item and item[key] is not None:
                        yield f"/{idx}/{key}", key, item[key]
                if "endpoints" in item and isinstance(item["endpoints"], list):
                    for eidx, entry in enumerate(item["endpoints"]):
                        if isinstance(entry, dict) and "config_path" in entry:
                            yield f"/{idx}/endpoints/{eidx}/config_path", "config_path", entry["config_path"]


def json_pointer_from_path(path_seq):
    if not path_seq:
        return "/"
    def esc(seg):
        return str(seg).replace("~", "~0").replace("/", "~1")
    return "/" + "/".join(esc(p) for p in path_seq)


def explain_field(key: str) -> str:
    explanations = {
        "cache": "Cache is required because data lifetime and overwrite semantics must be explicit.",
        "comm": "Comm is required because delivery semantics and failure modes must be explicit. Use null for cache-only endpoints.",
        "pdu_def_path": "PDU definitions provide shared meaning of bytes (name → channel_id/size).",
        "config_path": "Container entries must point to a concrete endpoint config to keep semantics explicit.",
        "direction": "Direction defines data flow semantics and must be explicit.",
        "role": "Role defines client/server behavior and connection responsibility.",
        "local": "Local binding makes the listening side explicit.",
        "remote": "Remote address defines the target side explicitly.",
        "pdu_key": "UDP framing requires an explicit PDU key to identify data.",
        "expected_clients": "Expected clients gates readiness and makes connection semantics explicit.",
    }
    return explanations.get(key, "This field is part of explicit simulation semantics and must not be implicit.")


def validate_file(schema, schema_path: Path, json_path: Path, check_paths: bool):
    try:
        data = load_json(json_path)
    except json.JSONDecodeError as e:
        return False, f"{json_path}: JSON parse error: {e}"
    except OSError as e:
        return False, f"{json_path}: read error: {e}"

    validator = jsonschema.Draft7Validator(schema)
    errors = sorted(validator.iter_errors(data), key=lambda e: e.path)
    messages = []
    for e in errors:
        pointer = json_pointer_from_path(e.path)
        rule = e.validator or "validation"
        missing_key = None
        if rule == "required" and isinstance(e.message, str):
            if "'" in e.message:
                missing_key = e.message.split("'")[1]
        if missing_key:
            missing_ptr = pointer.rstrip("/") + "/" + missing_key
            reason = explain_field(missing_key)
            messages.append(
                f"{json_path}: {missing_ptr}: rule=required: {e.message} Reason: {reason}"
            )
        else:
            key_hint = e.path[-1] if e.path else None
            reason = explain_field(str(key_hint)) if key_hint is not None else "This field is part of explicit simulation semantics and must not be implicit."
            messages.append(
                f"{json_path}: {pointer}: rule={rule}: {e.message} Reason: {reason}"
            )

    if check_paths:
        base_dir = json_path.parent
        for pointer, key, ref in iter_ref_paths_from_json(data):
            if not isinstance(ref, str):
                messages.append(f"{json_path}: {pointer}: rule=type: invalid path reference (not string). Reason: {explain_field(key)}")
                continue
            resolved = resolve_ref_path(base_dir, ref)
            if not resolved.exists():
                messages.append(
                    f"{json_path}: {pointer}: rule=exists: missing referenced file '{ref}' "
                    f"(resolved: '{resolved}'). Reason: {explain_field(key)} "
                    f"Suggested fix: update '{key}' or create the file at the resolved path."
                )

    if not messages:
        return True, f"{json_path}: OK ({schema_path})"
    return False, "\n".join(messages)


def main():
    parser = argparse.ArgumentParser(
        description="Validate JSON files against a JSON Schema (Draft-07)."
    )
    parser.add_argument(
        "--schema",
        required=True,
        help="Path to JSON schema file.",
    )
    parser.add_argument(
        "paths",
        nargs="+",
        help="JSON file(s) or directory paths (directories are scanned recursively).",
    )
    parser.add_argument(
        "--check-paths",
        action="store_true",
        help="Check existence of referenced JSON files (cache/comm/pdu_def_path/config_path).",
    )
    args = parser.parse_args()

    schema_path = Path(args.schema)
    try:
        schema = load_json(schema_path)
    except json.JSONDecodeError as e:
        print(f"{schema_path}: schema JSON parse error: {e}", file=sys.stderr)
        return 2
    except OSError as e:
        print(f"{schema_path}: schema read error: {e}", file=sys.stderr)
        return 2

    had_error = False
    for json_path in iter_json_files(args.paths):
        ok, msg = validate_file(schema, schema_path, json_path, args.check_paths)
        if ok:
            print(msg)
        else:
            had_error = True
            print(msg, file=sys.stderr)

    return 1 if had_error else 0


if __name__ == "__main__":
    raise SystemExit(main())
