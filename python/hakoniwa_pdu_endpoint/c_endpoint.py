from dataclasses import dataclass
from pathlib import Path
import ctypes
import importlib.util
import os
from queue import Empty, Queue
import sys
import threading
from typing import Callable, Dict, List, Optional, Tuple


_ENV_SHARED_LIB = "HAKO_PDU_ENDPOINT_SHARED_LIB"
_ENV_LIB_DIR = "HAKO_PDU_ENDPOINT_LIB_DIR"
_repo_root = Path(__file__).resolve().parents[2]
_package_dir = Path(__file__).resolve().parent


def _candidate_native_lib_names() -> List[str]:
    if sys.platform == "win32":
        return [
            "hakoniwa_pdu_endpoint.dll",
            "hakoniwa_pdu_endpoint-windows-x64.dll",
        ]
    if sys.platform == "darwin":
        return ["libhakoniwa_pdu_endpoint.dylib"]
    return ["libhakoniwa_pdu_endpoint.so"]


def _candidate_ffi_suffixes() -> List[str]:
    suffixes = list(dict.fromkeys(importlib.machinery.EXTENSION_SUFFIXES))
    if sys.platform != "win32":
        suffixes.append(".so")
    return suffixes


def _candidate_python_build_roots() -> List[Path]:
    roots: List[Path] = []
    for build_dir_name in ("build", "build-win", "build-win2", "build-shared"):
        root = _repo_root / build_dir_name / "python"
        if root.exists():
            roots.append(root)
    return roots


def _candidate_native_lib_dirs() -> List[Path]:
    dirs: List[Path] = []
    dirs.append(_package_dir)
    env_lib_dir = os.environ.get(_ENV_LIB_DIR)
    if env_lib_dir:
        dirs.append(Path(env_lib_dir).expanduser())

    env_shared_lib = os.environ.get(_ENV_SHARED_LIB)
    if env_shared_lib:
        dirs.append(Path(env_shared_lib).expanduser().resolve().parent)

    for candidate in (
        _repo_root / "build" / "src",
        _repo_root / "build-shared" / "src",
        _repo_root / "build-win" / "src" / "Release",
        _repo_root / "build-win2" / "src" / "Release",
        Path("/usr/local/hakoniwa/lib"),
    ):
        if candidate.exists():
            dirs.append(candidate)

    seen = set()
    unique_dirs: List[Path] = []
    for candidate in dirs:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        unique_dirs.append(resolved)
    return unique_dirs


def _candidate_ffi_dirs() -> List[Path]:
    dirs: List[Path] = []
    dirs.append(_package_dir)

    env_lib_dir = os.environ.get(_ENV_LIB_DIR)
    if env_lib_dir:
        dirs.append(Path(env_lib_dir).expanduser().resolve())

    env_shared_lib = os.environ.get(_ENV_SHARED_LIB)
    if env_shared_lib:
        dirs.append(Path(env_shared_lib).expanduser().resolve().parent)

    for root in _candidate_python_build_roots():
        dirs.append((root / "hakoniwa_pdu_endpoint").resolve())

    seen = set()
    unique_dirs: List[Path] = []
    for candidate in dirs:
        if candidate in seen:
            continue
        seen.add(candidate)
        unique_dirs.append(candidate)
    return unique_dirs


def _add_runtime_search_dirs() -> None:
    if sys.platform != "win32":
        return
    for lib_dir in _candidate_native_lib_dirs():
        try:
            os.add_dll_directory(str(lib_dir))
        except (FileNotFoundError, OSError):
            continue


def _preload_runtime_libs() -> None:
    _add_runtime_search_dirs()

    env_shared_lib = os.environ.get(_ENV_SHARED_LIB)
    if env_shared_lib:
        lib_path = Path(env_shared_lib).expanduser().resolve()
        if not lib_path.exists():
            raise FileNotFoundError(f"{_ENV_SHARED_LIB} points to a missing file: {lib_path}")
        load_kwargs = {}
        if hasattr(ctypes, "RTLD_GLOBAL") and sys.platform != "win32":
            load_kwargs["mode"] = ctypes.RTLD_GLOBAL
        ctypes.CDLL(str(lib_path), **load_kwargs)
        return

    if sys.platform == "darwin":
        for lib_dir in _candidate_native_lib_dirs():
            for lib_name in ("libconductor.dylib", "libassets.dylib", "libshakoc.dylib"):
                lib_path = lib_dir / lib_name
                if lib_path.exists():
                    ctypes.CDLL(str(lib_path), mode=ctypes.RTLD_GLOBAL)


_preload_runtime_libs()

_python_build_roots = _candidate_python_build_roots()
for _python_build_root in reversed(_python_build_roots):
    sys.path.insert(0, str(_python_build_root))

try:
    from ._c_endpoint_ffi import ffi, lib
except ModuleNotFoundError:
    _ffi_candidates: List[Path] = []
    for package_dir in _candidate_ffi_dirs():
        for suffix in _candidate_ffi_suffixes():
            _ffi_candidates.extend(sorted(package_dir.glob(f"_c_endpoint_ffi*{suffix}")))
    if not _ffi_candidates:
        raise
    _ffi_path = _ffi_candidates[0]
    _ffi_spec = importlib.util.spec_from_file_location("hakoniwa_pdu_endpoint._c_endpoint_ffi", _ffi_path)
    if _ffi_spec is None or _ffi_spec.loader is None:
        raise
    _ffi_module = importlib.util.module_from_spec(_ffi_spec)
    _ffi_spec.loader.exec_module(_ffi_module)
    ffi = _ffi_module.ffi
    lib = _ffi_module.lib


class EndpointError(RuntimeError):
    def __init__(self, err_code: int, func_name: str):
        super().__init__(f"{func_name} failed: err={err_code}")
        self.err_code = err_code
        self.func_name = func_name


@dataclass
class PduResolvedKey:
    robot: str
    channel_id: int


@dataclass
class PduKey:
    robot: str
    pdu: str


@dataclass
class PduRecord:
    key: PduResolvedKey
    timestamp_ns: int
    payload: bytes


@dataclass
class PduEvent:
    key: PduResolvedKey
    payload: bytes


def _check(err: int, func_name: str) -> None:
    if err != 0:
        raise EndpointError(err, func_name)


def _to_c_key(key: PduResolvedKey):
    c_key = ffi.new("hako_pdu_resolved_key_t*")
    robot_bytes = key.robot.encode("utf-8")
    if len(robot_bytes) >= 128:
        raise ValueError("robot name too long for c_endpoint")
    ffi.memmove(c_key[0].robot, robot_bytes, len(robot_bytes))
    c_key[0].robot[len(robot_bytes)] = b"\0"
    c_key[0].channel_id = key.channel_id
    return c_key


def _to_c_pdu_key(key: PduKey):
    c_key = ffi.new("hako_pdu_key_t*")
    robot_bytes = key.robot.encode("utf-8")
    pdu_bytes = key.pdu.encode("utf-8")
    if len(robot_bytes) >= 128:
        raise ValueError("robot name too long for c_endpoint")
    if len(pdu_bytes) >= 128:
        raise ValueError("pdu name too long for c_endpoint")
    ffi.memmove(c_key[0].robot, robot_bytes, len(robot_bytes))
    ffi.memmove(c_key[0].pdu, pdu_bytes, len(pdu_bytes))
    c_key[0].robot[len(robot_bytes)] = b"\0"
    c_key[0].pdu[len(pdu_bytes)] = b"\0"
    return c_key


def _make_recv_callback(callback):
    @ffi.callback("void(void*, const hako_pdu_resolved_key_t*, const void*, size_t)")
    def _cb(_user_data, c_key_ptr, c_data, c_size):
        recv_key = PduResolvedKey(
            robot=ffi.string(c_key_ptr.robot).decode("utf-8"),
            channel_id=int(c_key_ptr.channel_id),
        )
        if c_data == ffi.NULL or c_size == 0:
            payload = b""
        else:
            payload = bytes(ffi.buffer(c_data, c_size))
        callback(recv_key, payload)

    return _cb


def _resolved_key_id(key: PduResolvedKey) -> Tuple[str, int]:
    return (key.robot, key.channel_id)


class Endpoint:
    def __init__(self, name: str, direction: str):
        direction_map = {
            "in": lib.HAKO_PDU_ENDPOINT_DIRECTION_IN,
            "out": lib.HAKO_PDU_ENDPOINT_DIRECTION_OUT,
            "inout": lib.HAKO_PDU_ENDPOINT_DIRECTION_INOUT,
        }
        if direction not in direction_map:
            raise ValueError("direction must be 'in', 'out', or 'inout'")
        handle = lib.hako_pdu_endpoint_create(name.encode("utf-8"), direction_map[direction])
        if handle == ffi.NULL:
            raise EndpointError(2, "hako_pdu_endpoint_create")
        self._handle = handle
        self._callbacks = []
        self._event_queue: Queue[PduEvent] = Queue()
        self._handlers: Dict[Tuple[str, int], List[Callable[[PduEvent], None]]] = {}
        self._dispatch_running = False
        self._dispatch_thread: Optional[threading.Thread] = None
        self._dispatch_lock = threading.Lock()

    def __del__(self):
        if getattr(self, "_handle", ffi.NULL) != ffi.NULL:
            lib.hako_pdu_endpoint_destroy(self._handle)
            self._handle = ffi.NULL

    def open(self, config_path: str, asset_name: Optional[str] = None) -> None:
        config = str(Path(config_path)).encode("utf-8")
        if asset_name is None:
            _check(lib.hako_pdu_endpoint_open(self._handle, config), "open")
            return
        if asset_name == "":
            raise ValueError("asset_name must not be empty")
        _check(
            lib.hako_pdu_endpoint_open_with_asset(self._handle, config, asset_name.encode("utf-8")),
            "open_with_asset",
        )

    def create_pdu_lchannels(self, config_path: str) -> None:
        _check(
            lib.hako_pdu_endpoint_create_pdu_lchannels(self._handle, str(Path(config_path)).encode("utf-8")),
            "create_pdu_lchannels",
        )

    def close(self) -> None:
        self.stop_dispatch()
        _check(lib.hako_pdu_endpoint_close(self._handle), "close")

    def start(self) -> None:
        _check(lib.hako_pdu_endpoint_start(self._handle), "start")

    def post_start(self) -> None:
        _check(lib.hako_pdu_endpoint_post_start(self._handle), "post_start")

    def stop(self) -> None:
        _check(lib.hako_pdu_endpoint_stop(self._handle), "stop")

    def is_running(self) -> bool:
        out_running = ffi.new("hako_pdu_bool_t*")
        _check(lib.hako_pdu_endpoint_is_running(self._handle, out_running), "is_running")
        return bool(out_running[0])

    def process_recv_events(self) -> None:
        lib.hako_pdu_endpoint_process_recv_events(self._handle)

    def send(self, key: PduResolvedKey, payload: bytes) -> None:
        c_key = _to_c_key(key)
        _check(lib.hako_pdu_endpoint_send(self._handle, c_key, payload, len(payload)), "send")

    def subscribe_on_recv_callback(self, key: PduResolvedKey, callback) -> None:
        c_key = _to_c_key(key)
        _cb = _make_recv_callback(callback)
        self._callbacks.append(_cb)
        _check(
            lib.hako_pdu_endpoint_subscribe_on_recv_callback(self._handle, c_key, _cb, ffi.NULL),
            "subscribe_on_recv_callback",
        )

    def send_by_name(self, key: PduKey, payload: bytes) -> None:
        c_key = _to_c_pdu_key(key)
        _check(
            lib.hako_pdu_endpoint_send_by_name(self._handle, c_key, payload, len(payload)),
            "send_by_name",
        )

    def subscribe_on_recv_callback_by_name(self, key: PduKey, callback) -> None:
        c_key = _to_c_pdu_key(key)
        _cb = _make_recv_callback(callback)
        self._callbacks.append(_cb)
        _check(
            lib.hako_pdu_endpoint_subscribe_on_recv_callback_by_name(self._handle, c_key, _cb, ffi.NULL),
            "subscribe_on_recv_callback_by_name",
        )

    def _enqueue_event(self, key: PduResolvedKey, payload: bytes) -> None:
        self._event_queue.put(PduEvent(key=key, payload=payload))

    def on_recv(self, key: PduResolvedKey, handler: Callable[[PduEvent], None]) -> None:
        key_id = _resolved_key_id(key)
        with self._dispatch_lock:
            if key_id not in self._handlers:
                self._handlers[key_id] = []
                self.subscribe_on_recv_callback(key, self._enqueue_event)
            self._handlers[key_id].append(handler)

    def on_recv_by_name(self, key: PduKey, handler: Callable[[PduEvent], None]) -> None:
        resolved_key = PduResolvedKey(
            robot=key.robot,
            channel_id=self.get_pdu_channel_id(key),
        )
        self.on_recv(resolved_key, handler)

    def _dispatch_loop(self) -> None:
        while self._dispatch_running:
            try:
                event = self._event_queue.get(timeout=0.1)
            except Empty:
                continue
            key_id = _resolved_key_id(event.key)
            with self._dispatch_lock:
                handlers = list(self._handlers.get(key_id, ()))
            for handler in handlers:
                handler(event)

    def start_dispatch(self) -> None:
        if self._dispatch_running:
            return
        self._dispatch_running = True
        self._dispatch_thread = threading.Thread(
            target=self._dispatch_loop,
            name="hakoniwa-pdu-endpoint-dispatch",
            daemon=True,
        )
        self._dispatch_thread.start()

    def stop_dispatch(self) -> None:
        if not self._dispatch_running:
            return
        self._dispatch_running = False
        if self._dispatch_thread is not None:
            self._dispatch_thread.join(timeout=1.0)
            self._dispatch_thread = None

    def recv(self, key: PduResolvedKey, buffer_size: int) -> bytes:
        c_key = _to_c_key(key)
        buffer = ffi.new(f"unsigned char[{buffer_size}]")
        received_size = ffi.new("size_t*")
        _check(
            lib.hako_pdu_endpoint_recv(self._handle, c_key, buffer, buffer_size, received_size),
            "recv",
        )
        return bytes(ffi.buffer(buffer, received_size[0]))

    def set_recv_event(self, key: PduResolvedKey) -> None:
        c_key = _to_c_key(key)
        _check(
            lib.hako_pdu_endpoint_set_recv_event(self._handle, c_key),
            "set_recv_event",
        )

    def get_pending_count(self) -> int:
        out_count = ffi.new("size_t*")
        _check(
            lib.hako_pdu_endpoint_get_pending_count(self._handle, out_count),
            "get_pending_count",
        )
        return int(out_count[0])

    def recv_by_name(self, key: PduKey, buffer_size: int) -> bytes:
        c_key = _to_c_pdu_key(key)
        buffer = ffi.new(f"unsigned char[{buffer_size}]")
        received_size = ffi.new("size_t*")
        _check(
            lib.hako_pdu_endpoint_recv_by_name(self._handle, c_key, buffer, buffer_size, received_size),
            "recv_by_name",
        )
        return bytes(ffi.buffer(buffer, received_size[0]))

    def recv_next(self, buffer_size: int) -> PduRecord:
        buffer = ffi.new(f"unsigned char[{buffer_size}]")
        out_key = ffi.new("hako_pdu_resolved_key_t*")
        out_timestamp_ns = ffi.new("unsigned long long*")
        received_size = ffi.new("size_t*")
        _check(
            lib.hako_pdu_endpoint_recv_next(
                self._handle,
                buffer,
                buffer_size,
                out_key,
                out_timestamp_ns,
                received_size,
            ),
            "recv_next",
        )
        robot = ffi.string(out_key[0].robot).decode("utf-8")
        payload = bytes(ffi.buffer(buffer, received_size[0]))
        return PduRecord(
            key=PduResolvedKey(robot=robot, channel_id=out_key[0].channel_id),
            timestamp_ns=out_timestamp_ns[0],
            payload=payload,
        )

    def get_pdu_size(self, key: PduKey) -> int:
        c_key = _to_c_pdu_key(key)
        return int(lib.hako_pdu_endpoint_get_pdu_size(self._handle, c_key))

    def get_pdu_channel_id(self, key: PduKey) -> int:
        c_key = _to_c_pdu_key(key)
        return int(lib.hako_pdu_endpoint_get_pdu_channel_id(self._handle, c_key))

    def get_pdu_name(self, key: PduResolvedKey, buffer_size: int = 128) -> str:
        c_key = _to_c_key(key)
        buffer = ffi.new(f"char[{buffer_size}]")
        _check(
            lib.hako_pdu_endpoint_get_pdu_name(self._handle, c_key, buffer, buffer_size),
            "get_pdu_name",
        )
        return ffi.string(buffer).decode("utf-8")
