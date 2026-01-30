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


def iter_pdudef_files(paths):
    for p in paths:
        path = Path(p)
        if path.is_dir():
            for child in sorted(path.rglob("*.json")):
                if child.name.endswith(".schema.json"):
                    continue
                name = child.name.lower()
                if "pdudef" in name or "pdu_def" in name:
                    yield child
        else:
            yield path


def resolve_ref_path(base_dir: Path, raw_value: str) -> Path:
    p = Path(raw_value)
    if p.is_absolute():
        return p
    return (base_dir / p).resolve()


def build_validator(schema):
    try:
        validator_cls = jsonschema.validators.validator_for(schema)
        validator_cls.check_schema(schema)
        return validator_cls(schema)
    except Exception:
        return jsonschema.Draft7Validator(schema)


def validate_with_schema(validator, data, json_path: Path):
    errors = sorted(validator.iter_errors(data), key=lambda e: e.path)
    messages = []
    for e in errors:
        loc = "/".join([str(x) for x in e.path]) or "(root)"
        messages.append(f"{json_path}: {loc}: {e.message}")
    return messages


def validate_pdutypes_file(pdutypes_validator, pdutypes_path: Path):
    try:
        data = load_json(pdutypes_path)
    except json.JSONDecodeError as e:
        return [f"{pdutypes_path}: JSON parse error: {e}"]
    except OSError as e:
        return [f"{pdutypes_path}: read error: {e}"]
    return validate_with_schema(pdutypes_validator, data, pdutypes_path)


def validate_pdudef(pdudef_validator, pdutypes_validator, pdudef_path: Path):
    try:
        data = load_json(pdudef_path)
    except json.JSONDecodeError as e:
        return [f"{pdudef_path}: JSON parse error: {e}"], True
    except OSError as e:
        return [f"{pdudef_path}: read error: {e}"], True

    messages = validate_with_schema(pdudef_validator, data, pdudef_path)
    had_schema_error = bool(messages)

    # If compact format, validate referenced pdutypes files and ids.
    if isinstance(data, dict) and "paths" in data:
        base_dir = pdudef_path.parent
        paths = data.get("paths") or []
        id_map = {}
        for entry in paths:
            if not isinstance(entry, dict):
                messages.append(f"{pdudef_path}: paths entry is not an object: {entry}")
                continue
            pid = entry.get("id")
            ppath = entry.get("path")
            if not isinstance(pid, str) or not pid:
                messages.append(f"{pdudef_path}: paths entry missing id: {entry}")
                continue
            if pid in id_map:
                messages.append(f"{pdudef_path}: duplicate paths id: {pid}")
                continue
            if not isinstance(ppath, str) or not ppath:
                messages.append(f"{pdudef_path}: paths entry missing path for id {pid}")
                continue
            resolved = resolve_ref_path(base_dir, ppath)
            id_map[pid] = resolved
            if not resolved.exists():
                messages.append(f"{pdudef_path}: missing pdutypes file: {ppath}")
                continue
            messages.extend(validate_pdutypes_file(pdutypes_validator, resolved))

        robots = data.get("robots") or []
        for robot in robots:
            if not isinstance(robot, dict):
                continue
            rid = robot.get("pdutypes_id")
            if isinstance(rid, str) and rid:
                if rid not in id_map:
                    messages.append(f"{pdudef_path}: robots.pdutypes_id not found in paths: {rid}")

    return messages, had_schema_error


def main():
    parser = argparse.ArgumentParser(
        description="Validate pdudef.json (legacy or compact) and referenced pdutypes files."
    )
    parser.add_argument(
        "paths",
        nargs="+",
        help="pdudef.json file(s) or directory paths (dirs scanned for *pdudef*.json).",
    )
    parser.add_argument(
        "--schema-pdudef",
        default="config/schema/pdudef.schema.json",
        help="Path to pdudef schema.",
    )
    parser.add_argument(
        "--schema-pdutypes",
        default="config/schema/pdutypes.schema.json",
        help="Path to pdutypes schema.",
    )
    args = parser.parse_args()

    pdudef_schema_path = Path(args.schema_pdudef)
    pdutypes_schema_path = Path(args.schema_pdutypes)
    try:
        pdudef_schema = load_json(pdudef_schema_path)
    except json.JSONDecodeError as e:
        print(f"{pdudef_schema_path}: schema JSON parse error: {e}", file=sys.stderr)
        return 2
    except OSError as e:
        print(f"{pdudef_schema_path}: schema read error: {e}", file=sys.stderr)
        return 2

    try:
        pdutypes_schema = load_json(pdutypes_schema_path)
    except json.JSONDecodeError as e:
        print(f"{pdutypes_schema_path}: schema JSON parse error: {e}", file=sys.stderr)
        return 2
    except OSError as e:
        print(f"{pdutypes_schema_path}: schema read error: {e}", file=sys.stderr)
        return 2

    pdudef_validator = build_validator(pdudef_schema)
    pdutypes_validator = build_validator(pdutypes_schema)

    had_error = False
    for json_path in iter_pdudef_files(args.paths):
        msgs, _ = validate_pdudef(pdudef_validator, pdutypes_validator, json_path)
        if msgs:
            had_error = True
            for msg in msgs:
                print(msg, file=sys.stderr)
        else:
            print(f"{json_path}: OK")

    return 1 if had_error else 0


if __name__ == "__main__":
    raise SystemExit(main())
