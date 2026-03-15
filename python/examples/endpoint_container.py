#!/usr/bin/env python3
import json
import sys
import tempfile
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.c_endpoint import PduResolvedKey
    from hakoniwa_pdu_endpoint.endpoint_container import EndpointContainer

    tmp_dir = Path(tempfile.mkdtemp(prefix="hako_pdu_py_example_container_"))
    container_config = tmp_dir / "endpoint_container.json"
    endpoint_config = str(repo_root / "config/sample/endpoint_internal_cache.json")
    container_config.write_text(
        json.dumps(
            [
                {
                    "nodeId": "py_example_node",
                    "endpoints": [
                        {"id": "ep1", "config_path": endpoint_config},
                        {"id": "ep2", "config_path": endpoint_config},
                    ],
                }
            ]
        )
    )

    container = EndpointContainer("py_example_node", str(container_config))
    container.initialize()
    container.start("ep1")

    endpoint = container.ref("ep1")
    key = PduResolvedKey(robot="py_container_example_robot", channel_id=9)
    endpoint.send(key, b"\xaa\xbb")
    print("container ids:", container.list_endpoint_ids())
    print("received:", list(endpoint.recv(key, 16)))

    container.stop("ep1")
    container.stop("ep2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
