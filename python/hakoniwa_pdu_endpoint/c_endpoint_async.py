from dataclasses import dataclass
from queue import Empty, Queue
import threading
from typing import Callable, Dict, List, Optional, Tuple

from .c_endpoint import Endpoint, PduKey, PduResolvedKey


@dataclass
class PduEvent:
    key: PduResolvedKey
    payload: bytes


def _resolved_key_id(key: PduResolvedKey) -> Tuple[str, int]:
    return (key.robot, key.channel_id)


class EndpointAsync:
    """
    Higher-level Python wrapper for callback delivery.

    The underlying C callback only captures and copies events. Python handlers
    run later on a Python-owned dispatch thread.
    """

    def __init__(self, endpoint: Endpoint):
        self._endpoint = endpoint
        self._queue: Queue[PduEvent] = Queue()
        self._handlers: Dict[Tuple[str, int], List[Callable[[PduEvent], None]]] = {}
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()

    def _enqueue_event(self, key: PduResolvedKey, payload: bytes) -> None:
        self._queue.put(PduEvent(key=key, payload=payload))

    def on_recv(self, key: PduResolvedKey, handler: Callable[[PduEvent], None]) -> None:
        key_id = _resolved_key_id(key)
        with self._lock:
            if key_id not in self._handlers:
                self._handlers[key_id] = []
                self._endpoint.subscribe_on_recv_callback(key, self._enqueue_event)
            self._handlers[key_id].append(handler)

    def on_recv_by_name(self, key: PduKey, handler: Callable[[PduEvent], None]) -> None:
        resolved_key = PduResolvedKey(
            robot=key.robot,
            channel_id=self._endpoint.get_pdu_channel_id(key),
        )
        self.on_recv(resolved_key, handler)

    def _dispatch_loop(self) -> None:
        while self._running:
            try:
                event = self._queue.get(timeout=0.1)
            except Empty:
                continue
            key_id = _resolved_key_id(event.key)
            with self._lock:
                handlers = list(self._handlers.get(key_id, ()))
            for handler in handlers:
                handler(event)

    def start_dispatch(self) -> None:
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(
            target=self._dispatch_loop,
            name="hakoniwa-pdu-endpoint-dispatch",
            daemon=True,
        )
        self._thread.start()

    def stop_dispatch(self) -> None:
        if not self._running:
            return
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None

    def close(self) -> None:
        self.stop_dispatch()
        self._endpoint.close()

    def open(self, config_path: str) -> None:
        self._endpoint.open(config_path)

    def create_pdu_lchannels(self, config_path: str) -> None:
        self._endpoint.create_pdu_lchannels(config_path)

    def start(self) -> None:
        self._endpoint.start()

    def post_start(self) -> None:
        self._endpoint.post_start()

    def stop(self) -> None:
        self._endpoint.stop()

    def is_running(self) -> bool:
        return self._endpoint.is_running()

    def process_recv_events(self) -> None:
        self._endpoint.process_recv_events()

    def send(self, key: PduResolvedKey, payload: bytes) -> None:
        self._endpoint.send(key, payload)

    def send_by_name(self, key: PduKey, payload: bytes) -> None:
        self._endpoint.send_by_name(key, payload)

    def recv(self, key: PduResolvedKey, buffer_size: int) -> bytes:
        return self._endpoint.recv(key, buffer_size)

    def recv_by_name(self, key: PduKey, buffer_size: int) -> bytes:
        return self._endpoint.recv_by_name(key, buffer_size)

    def recv_next(self, buffer_size: int):
        return self._endpoint.recv_next(buffer_size)

