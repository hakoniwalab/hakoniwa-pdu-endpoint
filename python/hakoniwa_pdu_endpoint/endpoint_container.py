import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

from .c_endpoint import Endpoint


@dataclass
class EndpointEntry:
    endpoint_id: str
    config_path: str
    direction: Optional[str] = None
    mode: Optional[str] = None


class EndpointContainer:
    def __init__(self, node_id: str, container_config_path: str):
        self._node_id = node_id
        self._container_config_path = str(container_config_path)
        self._entries: List[EndpointEntry] = []
        self._cache: Dict[str, Endpoint] = {}
        self._initialized = False
        self._last_error = ""

    @property
    def node_id(self) -> str:
        return self._node_id

    @property
    def last_error(self) -> str:
        return self._last_error

    def _resolve_under_base(self, base_dir: Path, maybe_rel: str) -> str:
        p = Path(maybe_rel)
        if p.is_absolute():
            return str(p.resolve())
        return str((base_dir / p).resolve())

    def _load_entries(self) -> None:
        self._last_error = ""
        self._entries = []

        config_path = Path(self._container_config_path)
        if not config_path.exists():
            raise FileNotFoundError(f"Failed to open container config: {self._container_config_path}")

        root = json.loads(config_path.read_text())
        if not isinstance(root, list):
            raise ValueError("Container config must be a JSON array")

        matched_entry = None
        for entry in root:
            if not isinstance(entry, dict):
                raise ValueError("Invalid endpoint entry (not an object)")
            if entry.get("nodeId") == self._node_id:
                matched_entry = entry
                break

        if matched_entry is None:
            raise KeyError(f"No endpoint entry found for nodeId: {self._node_id}")

        base_dir = config_path.parent
        for ep in matched_entry.get("endpoints", []):
            if not isinstance(ep, dict):
                raise ValueError("Invalid endpoint entry (not an object)")
            endpoint_id = ep.get("id")
            config_rel = ep.get("config_path")
            if not endpoint_id or not isinstance(endpoint_id, str):
                raise ValueError("Endpoint entry missing string field 'id'")
            if not config_rel or not isinstance(config_rel, str):
                raise ValueError(f"Endpoint entry missing string field 'config_path'. id={endpoint_id}")
            self._entries.append(
                EndpointEntry(
                    endpoint_id=endpoint_id,
                    config_path=self._resolve_under_base(base_dir, config_rel),
                    direction=ep.get("direction"),
                    mode=ep.get("mode"),
                )
            )

    def _get_or_create(self, entry: EndpointEntry) -> Endpoint:
        endpoint = self._cache.get(entry.endpoint_id)
        if endpoint is None:
            direction = entry.direction or "inout"
            endpoint = Endpoint(entry.endpoint_id, direction)
            self._cache[entry.endpoint_id] = endpoint
        return endpoint

    def create_pdu_lchannels(self) -> None:
        self._load_entries()
        try:
            for entry in self._entries:
                endpoint = self._get_or_create(entry)
                endpoint.create_pdu_lchannels(entry.config_path)
        except Exception as exc:
            self._last_error = str(exc)
            self._cache.clear()
            raise

    def initialize(self) -> None:
        if self._initialized:
            self._last_error = "EndpointContainer is already initialized."
            raise RuntimeError(self._last_error)
        self._load_entries()
        try:
            for entry in self._entries:
                endpoint = self._get_or_create(entry)
                endpoint.open(entry.config_path)
        except Exception as exc:
            self._last_error = str(exc)
            for endpoint in self._cache.values():
                try:
                    endpoint.close()
                except Exception:
                    pass
            self._cache.clear()
            raise
        self._initialized = True

    def start_all(self) -> None:
        self._ensure_initialized()
        for endpoint in self._cache.values():
            endpoint.start()

    def post_start_all(self) -> None:
        self._ensure_initialized()
        for endpoint in self._cache.values():
            endpoint.post_start()

    def stop_all(self) -> None:
        self._ensure_initialized()
        first_error = None
        for endpoint_id, endpoint in list(self._cache.items()):
            try:
                endpoint.stop()
                endpoint.close()
            except Exception as exc:
                if first_error is None:
                    first_error = exc
                    self._last_error = f"stop_all failed at endpoint id={endpoint_id}: {exc}"
        self._cache.clear()
        self._initialized = False
        if first_error is not None:
            raise first_error

    def is_running_all(self) -> bool:
        if not self._initialized:
            return False
        return all(endpoint.is_running() for endpoint in self._cache.values())

    def start(self, endpoint_id: str) -> None:
        self._ensure_initialized()
        self.ref(endpoint_id).start()

    def post_start(self, endpoint_id: str) -> None:
        self._ensure_initialized()
        self.ref(endpoint_id).post_start()

    def stop(self, endpoint_id: str) -> None:
        self._ensure_initialized()
        endpoint = self._cache.pop(endpoint_id, None)
        if endpoint is None:
            return
        endpoint.stop()
        endpoint.close()

    def ref(self, endpoint_id: str) -> Endpoint:
        self._ensure_initialized()
        endpoint = self._cache.get(endpoint_id)
        if endpoint is None:
            self._last_error = f"ref: endpoint not found in container. id={endpoint_id}"
            raise KeyError(self._last_error)
        return endpoint

    def list_endpoint_ids(self) -> List[str]:
        return [entry.endpoint_id for entry in self._entries]

    def _ensure_initialized(self) -> None:
        if not self._initialized:
            self._last_error = "EndpointContainer is not initialized."
            raise RuntimeError(self._last_error)
