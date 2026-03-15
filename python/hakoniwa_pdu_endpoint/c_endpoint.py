from dataclasses import dataclass
from pathlib import Path
import ctypes
import sys


def _preload_runtime_libs() -> None:
    hakoniwa_lib_dir = Path("/usr/local/hakoniwa/lib")
    for lib_name in ("libconductor.dylib", "libassets.dylib", "libshakoc.dylib"):
        lib_path = hakoniwa_lib_dir / lib_name
        if lib_path.exists():
            ctypes.CDLL(str(lib_path), mode=ctypes.RTLD_GLOBAL)


_preload_runtime_libs()

_repo_root = Path(__file__).resolve().parents[2]
_python_build_root = _repo_root / "build" / "python"
if _python_build_root.exists():
    sys.path.insert(0, str(_python_build_root))

from ._c_endpoint_ffi import ffi, lib


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

    def __del__(self):
        if getattr(self, "_handle", ffi.NULL) != ffi.NULL:
            lib.hako_pdu_endpoint_destroy(self._handle)
            self._handle = ffi.NULL

    def open(self, config_path: str) -> None:
        _check(lib.hako_pdu_endpoint_open(self._handle, str(Path(config_path)).encode("utf-8")), "open")

    def create_pdu_lchannels(self, config_path: str) -> None:
        _check(
            lib.hako_pdu_endpoint_create_pdu_lchannels(self._handle, str(Path(config_path)).encode("utf-8")),
            "create_pdu_lchannels",
        )

    def close(self) -> None:
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

    def recv(self, key: PduResolvedKey, buffer_size: int) -> bytes:
        c_key = _to_c_key(key)
        buffer = ffi.new(f"unsigned char[{buffer_size}]")
        received_size = ffi.new("size_t*")
        _check(
            lib.hako_pdu_endpoint_recv(self._handle, c_key, buffer, buffer_size, received_size),
            "recv",
        )
        return bytes(ffi.buffer(buffer, received_size[0]))

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
