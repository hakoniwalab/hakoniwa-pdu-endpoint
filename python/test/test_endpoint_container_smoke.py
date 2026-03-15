#!/usr/bin/env python3
import json
import sys
import tempfile
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    python_root = repo_root / "python"
    sys.path.insert(0, str(python_root))

    from hakoniwa_pdu_endpoint.endpoint_container import EndpointContainer
    from hakoniwa_pdu_endpoint.c_endpoint import PduResolvedKey

    tmp_dir = Path(tempfile.mkdtemp(prefix="hako_pdu_py_container_"))
    container_config = tmp_dir / "endpoint_container.json"
    container_config.write_text(
        json.dumps(
            [
                {
                    "nodeId": "node_1",
                    "endpoints": [
                        {
                            "id": "endpoint_a",
                            "config_path": str(repo_root / "config" / "sample" / "endpoint_internal_cache.json"),
                        },
                        {
                            "id": "endpoint_b",
                            "config_path": str(repo_root / "config" / "sample" / "endpoint_internal_cache.json"),
                        },
                    ],
                }
            ]
        )
    )

    container = EndpointContainer(
        node_id="node_1",
        container_config_path=str(container_config),
    )
    container.initialize()

    ids = container.list_endpoint_ids()
    assert ids == ["endpoint_a", "endpoint_b"], ids

    endpoint = container.ref("endpoint_b")
    key = PduResolvedKey(robot="py_container_robot", channel_id=3)
    payload = b"\x11\x22\x33\x44"

    container.start("endpoint_b")
    assert endpoint.is_running() is True
    endpoint.send(key, payload)
    recv_payload = endpoint.recv(key, 16)
    assert recv_payload == payload, (recv_payload, payload)

    container.stop("endpoint_b")
    try:
        container.ref("endpoint_b")
    except KeyError:
        pass
    else:
        raise AssertionError("endpoint_b should have been removed from the cache")

    container.stop("endpoint_a")

    print("python endpoint_container smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
