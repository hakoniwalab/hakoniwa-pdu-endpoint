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
                yield data[key]
        if "endpoints" in data and isinstance(data["endpoints"], list):
            for entry in data["endpoints"]:
                if isinstance(entry, dict) and "config_path" in entry:
                    yield entry["config_path"]
    elif isinstance(data, list):
        for item in data:
            if isinstance(item, dict):
                for key in ("cache", "comm", "pdu_def_path"):
                    if key in item and item[key] is not None:
                        yield item[key]
                if "endpoints" in item and isinstance(item["endpoints"], list):
                    for entry in item["endpoints"]:
                        if isinstance(entry, dict) and "config_path" in entry:
                            yield entry["config_path"]


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
        loc = "/".join([str(x) for x in e.path]) or "(root)"
        messages.append(f"{json_path}: {loc}: {e.message}")

    if check_paths:
        base_dir = json_path.parent
        for ref in iter_ref_paths_from_json(data):
            if not isinstance(ref, str):
                messages.append(f"{json_path}: invalid path reference (not string): {ref}")
                continue
            resolved = resolve_ref_path(base_dir, ref)
            if not resolved.exists():
                messages.append(f"{json_path}: missing referenced file: {ref}")

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
