#pragma once

#include "hakoniwa/pdu/endpoint_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public C facade for hakoniwa::pdu::Endpoint.
 *
 * v1 design goals:
 * - stable C ABI
 * - caller-owned buffers for recv/recv_next
 * - opaque endpoint handle
 * - resolved-key-first API
 */

#ifndef HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX
#define HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX 128
#endif

#ifndef HAKO_PDU_C_ENDPOINT_PDU_NAME_MAX
#define HAKO_PDU_C_ENDPOINT_PDU_NAME_MAX 128
#endif

typedef struct hako_pdu_endpoint_handle hako_pdu_endpoint_handle_t;

typedef struct {
    char robot[HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX];
    uint32_t channel_id;
} hako_pdu_resolved_key_t;

typedef struct {
    char robot[HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX];
    char pdu[HAKO_PDU_C_ENDPOINT_PDU_NAME_MAX];
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

HakoPduErrorType hako_pdu_endpoint_open(
    hako_pdu_endpoint_handle_t* endpoint,
    const char* config_path);

HakoPduErrorType hako_pdu_endpoint_create_pdu_lchannels(
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
    uint64_t* out_timestamp_ns,
    size_t* received_size);

size_t hako_pdu_endpoint_get_pdu_size(
    const hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_key_t* key);

int32_t hako_pdu_endpoint_get_pdu_channel_id(
    const hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_key_t* key);

HakoPduErrorType hako_pdu_endpoint_get_pdu_name(
    const hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_resolved_key_t* key,
    char* out_name,
    size_t out_name_size);

#ifdef __cplusplus
}
#endif
