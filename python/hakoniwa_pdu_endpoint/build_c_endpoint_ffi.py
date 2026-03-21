#!/usr/bin/env python3
from pathlib import Path

from cffi import FFI


ffi = FFI()

ffi.cdef(
    """
    typedef unsigned char hako_pdu_bool_t;
    typedef enum
    {
        HAKO_PDU_ENDPOINT_DIRECTION_IN = 0,
        HAKO_PDU_ENDPOINT_DIRECTION_OUT = 1,
        HAKO_PDU_ENDPOINT_DIRECTION_INOUT = 2
    } HakoPduEndpointDirectionType;

    typedef enum
    {
        HAKO_PDU_ERR_OK = 0,
        HAKO_PDU_ERR_INVALID_ARGUMENT = 1,
        HAKO_PDU_ERR_OUT_OF_MEMORY = 2,
        HAKO_PDU_ERR_IO_ERROR = 3,
        HAKO_PDU_ERR_NO_SPACE = 4,
        HAKO_PDU_ERR_BUSY = 5,
        HAKO_PDU_ERR_TIMEOUT = 6,
        HAKO_PDU_ERR_NO_ENTRY = 7,
        HAKO_PDU_ERR_FILE_NOT_FOUND = 8,
        HAKO_PDU_ERR_INVALID_JSON = 9,
        HAKO_PDU_ERR_INVALID_CONFIG = 10,
        HAKO_PDU_ERR_NOT_RUNNING = 11,
        HAKO_PDU_ERR_UNSUPPORTED = 12,
        HAKO_PDU_ERR_INVALID_PDU_KEY = 13,
        HAKO_PDU_ERR_NOT_OWNER = 14
    } HakoPduErrorType;

    typedef struct hako_pdu_endpoint_handle hako_pdu_endpoint_handle_t;

    typedef struct {
        char robot[128];
        unsigned int channel_id;
    } hako_pdu_resolved_key_t;

    typedef struct {
        char robot[128];
        char pdu[128];
    } hako_pdu_key_t;

    typedef void (*hako_pdu_on_recv_cb)(
        void* user_data,
        const hako_pdu_resolved_key_t* key,
        const void* data,
        size_t size);

    hako_pdu_endpoint_handle_t* hako_pdu_endpoint_create(
        const char* name,
        HakoPduEndpointDirectionType direction);
    void hako_pdu_endpoint_destroy(hako_pdu_endpoint_handle_t* endpoint);

    HakoPduErrorType hako_pdu_endpoint_create_pdu_lchannels(
        hako_pdu_endpoint_handle_t* endpoint,
        const char* config_path);
    HakoPduErrorType hako_pdu_endpoint_open(
        hako_pdu_endpoint_handle_t* endpoint,
        const char* config_path);
    HakoPduErrorType hako_pdu_endpoint_close(
        hako_pdu_endpoint_handle_t* endpoint);
    HakoPduErrorType hako_pdu_endpoint_start(
        hako_pdu_endpoint_handle_t* endpoint);
    HakoPduErrorType hako_pdu_endpoint_post_start(
        hako_pdu_endpoint_handle_t* endpoint);
    HakoPduErrorType hako_pdu_endpoint_stop(
        hako_pdu_endpoint_handle_t* endpoint);
    HakoPduErrorType hako_pdu_endpoint_is_running(
        hako_pdu_endpoint_handle_t* endpoint,
        hako_pdu_bool_t* out_running);
    void hako_pdu_endpoint_process_recv_events(
        hako_pdu_endpoint_handle_t* endpoint);
    HakoPduErrorType hako_pdu_endpoint_send(
        hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_resolved_key_t* key,
        const void* data,
        size_t size);
    HakoPduErrorType hako_pdu_endpoint_subscribe_on_recv_callback(
        hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_resolved_key_t* key,
        hako_pdu_on_recv_cb callback,
        void* user_data);
    HakoPduErrorType hako_pdu_endpoint_send_by_name(
        hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_key_t* key,
        const void* data,
        size_t size);
    HakoPduErrorType hako_pdu_endpoint_subscribe_on_recv_callback_by_name(
        hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_key_t* key,
        hako_pdu_on_recv_cb callback,
        void* user_data);
    HakoPduErrorType hako_pdu_endpoint_recv(
        hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_resolved_key_t* key,
        void* buffer,
        size_t buffer_size,
        size_t* received_size);
    HakoPduErrorType hako_pdu_endpoint_set_recv_event(
        hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_resolved_key_t* key);
    HakoPduErrorType hako_pdu_endpoint_get_pending_count(
        hako_pdu_endpoint_handle_t* endpoint,
        size_t* out_count);
    HakoPduErrorType hako_pdu_endpoint_recv_by_name(
        hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_key_t* key,
        void* buffer,
        size_t buffer_size,
        size_t* received_size);
    HakoPduErrorType hako_pdu_endpoint_recv_next(
        hako_pdu_endpoint_handle_t* endpoint,
        void* buffer,
        size_t buffer_size,
        hako_pdu_resolved_key_t* out_key,
        unsigned long long* out_timestamp_ns,
        size_t* received_size);
    size_t hako_pdu_endpoint_get_pdu_size(
        const hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_key_t* key);
    int hako_pdu_endpoint_get_pdu_channel_id(
        const hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_key_t* key);
    HakoPduErrorType hako_pdu_endpoint_get_pdu_name(
        const hako_pdu_endpoint_handle_t* endpoint,
        const hako_pdu_resolved_key_t* key,
        char* out_name,
        size_t out_name_size);
    """
)


def configure_ffi():
    repo_root = Path(__file__).resolve().parents[2]
    include_dir = repo_root / "include"
    build_dir = repo_root / "build"
    python_build_root = build_dir / "python"
    hakoniwa_lib_dir = Path("/usr/local/hakoniwa/lib")

    ffi.set_source(
        "hakoniwa_pdu_endpoint._c_endpoint_ffi",
        '#include "hakoniwa/pdu/c_endpoint.h"',
        include_dirs=[str(include_dir)],
        libraries=["hakoniwa_pdu_endpoint", "assets", "shakoc"],
        library_dirs=[str(build_dir / "src"), str(hakoniwa_lib_dir)],
        runtime_library_dirs=[str(build_dir / "src"), str(hakoniwa_lib_dir)],
    )
    return python_build_root


if __name__ == "__main__":
    target_dir = configure_ffi()
    target_dir.mkdir(parents=True, exist_ok=True)
    ffi.compile(tmpdir=str(target_dir), verbose=True)
