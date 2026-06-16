#include "hakoniwa/pdu/c_endpoint.h"

#include "hakoniwa/pdu/endpoint.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

struct hako_pdu_endpoint_handle {
    std::unique_ptr<hakoniwa::pdu::Endpoint> impl;
};

namespace {

using hakoniwa::pdu::Endpoint;
using hakoniwa::pdu::PduKey;
using hakoniwa::pdu::PduRecord;
using hakoniwa::pdu::PduResolvedKey;

Endpoint* unwrap(hako_pdu_endpoint_handle_t* endpoint) noexcept
{
    if (endpoint == nullptr || !endpoint->impl) {
        return nullptr;
    }
    return endpoint->impl.get();
}

const Endpoint* unwrap_const(const hako_pdu_endpoint_handle_t* endpoint) noexcept
{
    if (endpoint == nullptr || !endpoint->impl) {
        return nullptr;
    }
    return endpoint->impl.get();
}

bool parse_robot_name(const char robot_buf[HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX], std::string& out)
{
    const auto len = strnlen(robot_buf, HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX);
    if (len == HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX) {
        return false;
    }
    out.assign(robot_buf, len);
    return !out.empty();
}

bool parse_pdu_name(const char pdu_buf[HAKO_PDU_C_ENDPOINT_PDU_NAME_MAX], std::string& out)
{
    const auto len = strnlen(pdu_buf, HAKO_PDU_C_ENDPOINT_PDU_NAME_MAX);
    if (len == HAKO_PDU_C_ENDPOINT_PDU_NAME_MAX) {
        return false;
    }
    out.assign(pdu_buf, len);
    return !out.empty();
}

bool to_cpp_key(const hako_pdu_resolved_key_t* key, PduResolvedKey& out)
{
    if (key == nullptr) {
        return false;
    }
    if (!parse_robot_name(key->robot, out.robot)) {
        return false;
    }
    out.channel_id = static_cast<HakoPduChannelIdType>(key->channel_id);
    return true;
}

bool to_cpp_key(const hako_pdu_key_t* key, PduKey& out)
{
    if (key == nullptr) {
        return false;
    }
    if (!parse_robot_name(key->robot, out.robot)) {
        return false;
    }
    if (!parse_pdu_name(key->pdu, out.pdu)) {
        return false;
    }
    return true;
}

HakoPduErrorType from_cpp_key(const PduResolvedKey& key, hako_pdu_resolved_key_t* out_key)
{
    if (out_key == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (key.robot.size() >= HAKO_PDU_C_ENDPOINT_ROBOT_NAME_MAX) {
        return HAKO_PDU_ERR_NO_SPACE;
    }
    std::memset(out_key->robot, 0, sizeof(out_key->robot));
    std::memcpy(out_key->robot, key.robot.data(), key.robot.size());
    out_key->channel_id = static_cast<uint32_t>(key.channel_id);
    return HAKO_PDU_ERR_OK;
}

std::span<const std::byte> to_const_byte_span(const void* data, size_t size)
{
    const auto* ptr = reinterpret_cast<const std::byte*>(data);
    return std::span<const std::byte>(ptr, size);
}

std::span<std::byte> to_mutable_byte_span(void* data, size_t size)
{
    auto* ptr = reinterpret_cast<std::byte*>(data);
    return std::span<std::byte>(ptr, size);
}

HakoPduErrorType copy_string_to_buffer(const std::string& value, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    if (value.size() + 1 > out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return HAKO_PDU_ERR_NO_SPACE;
    }
    std::memset(out, 0, out_size);
    if (!value.empty()) {
        std::memcpy(out, value.data(), value.size());
    }
    return HAKO_PDU_ERR_OK;
}

template <typename TKey>
HakoPduErrorType subscribe_callback_impl(
    Endpoint* impl,
    const TKey& cpp_key,
    hako_pdu_on_recv_cb callback,
    void* user_data)
{
    if (impl == nullptr || callback == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    impl->subscribe_on_recv_callback(
        cpp_key,
        [callback, user_data](const PduResolvedKey& key, std::span<const std::byte> data) {
            hako_pdu_resolved_key_t c_key{};
            if (from_cpp_key(key, &c_key) != HAKO_PDU_ERR_OK) {
                return;
            }
            callback(
                user_data,
                &c_key,
                data.empty() ? nullptr : static_cast<const void*>(data.data()),
                data.size());
        });
    return HAKO_PDU_ERR_OK;
}

} // namespace

extern "C" {

hako_pdu_endpoint_handle_t* hako_pdu_endpoint_create(
    const char* name,
    HakoPduEndpointDirectionType direction)
{
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    try {
        auto* handle = new hako_pdu_endpoint_handle{};
        handle->impl = std::make_unique<Endpoint>(std::string(name), direction);
        return handle;
    } catch (const std::bad_alloc&) {
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

void hako_pdu_endpoint_destroy(hako_pdu_endpoint_handle_t* endpoint)
{
    if (endpoint == nullptr) {
        return;
    }
    delete endpoint;
}

HakoPduErrorType hako_pdu_endpoint_open(
    hako_pdu_endpoint_handle_t* endpoint,
    const char* config_path)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || config_path == nullptr || config_path[0] == '\0') {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->open(config_path);
}

HakoPduErrorType hako_pdu_endpoint_open_with_asset(
    hako_pdu_endpoint_handle_t* endpoint,
    const char* config_path,
    const char* asset_name)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || config_path == nullptr || config_path[0] == '\0'
        || asset_name == nullptr || asset_name[0] == '\0') {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->open(config_path, asset_name);
}

HakoPduErrorType hako_pdu_endpoint_create_pdu_lchannels(
    hako_pdu_endpoint_handle_t* endpoint,
    const char* config_path)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || config_path == nullptr || config_path[0] == '\0') {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->create_pdu_lchannels(config_path);
}

HakoPduErrorType hako_pdu_endpoint_close(hako_pdu_endpoint_handle_t* endpoint)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->close();
}

HakoPduErrorType hako_pdu_endpoint_start(hako_pdu_endpoint_handle_t* endpoint)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->start();
}

HakoPduErrorType hako_pdu_endpoint_post_start(hako_pdu_endpoint_handle_t* endpoint)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->post_start();
}

HakoPduErrorType hako_pdu_endpoint_stop(hako_pdu_endpoint_handle_t* endpoint)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->stop();
}

HakoPduErrorType hako_pdu_endpoint_is_running(
    hako_pdu_endpoint_handle_t* endpoint,
    hako_pdu_bool_t* out_running)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || out_running == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    bool running = false;
    const auto err = impl->is_running(running);
    if (err != HAKO_PDU_ERR_OK) {
        return err;
    }
    *out_running = running ? HAKO_PDU_TRUE : HAKO_PDU_FALSE;
    return HAKO_PDU_ERR_OK;
}

void hako_pdu_endpoint_process_recv_events(hako_pdu_endpoint_handle_t* endpoint)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr) {
        return;
    }
    impl->process_recv_events();
}

HakoPduErrorType hako_pdu_endpoint_send(
    hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_resolved_key_t* key,
    const void* data,
    size_t size)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || key == nullptr || (data == nullptr && size != 0U)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduResolvedKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->send(cpp_key, to_const_byte_span(data, size));
}

HakoPduErrorType hako_pdu_endpoint_subscribe_on_recv_callback(
    hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_resolved_key_t* key,
    hako_pdu_on_recv_cb callback,
    void* user_data)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || key == nullptr || callback == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduResolvedKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return subscribe_callback_impl(impl, cpp_key, callback, user_data);
}

HakoPduErrorType hako_pdu_endpoint_send_by_name(
    hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_key_t* key,
    const void* data,
    size_t size)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || key == nullptr || (data == nullptr && size != 0U)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->send(cpp_key, to_const_byte_span(data, size));
}

HakoPduErrorType hako_pdu_endpoint_subscribe_on_recv_callback_by_name(
    hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_key_t* key,
    hako_pdu_on_recv_cb callback,
    void* user_data)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || key == nullptr || callback == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduKey cpp_name_key{};
    if (!to_cpp_key(key, cpp_name_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    const auto channel_id = impl->get_pdu_channel_id(cpp_name_key);
    if (channel_id < 0) {
        return HAKO_PDU_ERR_INVALID_PDU_KEY;
    }
    PduResolvedKey cpp_key{cpp_name_key.robot, channel_id};
    return subscribe_callback_impl(impl, cpp_key, callback, user_data);
}

HakoPduErrorType hako_pdu_endpoint_recv(
    hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_resolved_key_t* key,
    void* buffer,
    size_t buffer_size,
    size_t* received_size)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || key == nullptr || buffer == nullptr || received_size == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduResolvedKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    *received_size = 0;
    return impl->recv(cpp_key, to_mutable_byte_span(buffer, buffer_size), *received_size);
}

HakoPduErrorType hako_pdu_endpoint_set_recv_event(
    hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_resolved_key_t* key)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || key == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduResolvedKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    return impl->set_recv_event(cpp_key);
}

HakoPduErrorType hako_pdu_endpoint_get_pending_count(
    hako_pdu_endpoint_handle_t* endpoint,
    size_t* out_count)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || out_count == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    return impl->get_pending_count(*out_count);
}

HakoPduErrorType hako_pdu_endpoint_recv_by_name(
    hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_key_t* key,
    void* buffer,
    size_t buffer_size,
    size_t* received_size)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || key == nullptr || buffer == nullptr || received_size == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    *received_size = 0;
    return impl->recv(cpp_key, to_mutable_byte_span(buffer, buffer_size), *received_size);
}

HakoPduErrorType hako_pdu_endpoint_recv_next(
    hako_pdu_endpoint_handle_t* endpoint,
    void* buffer,
    size_t buffer_size,
    hako_pdu_resolved_key_t* out_key,
    uint64_t* out_timestamp_ns,
    size_t* received_size)
{
    auto* impl = unwrap(endpoint);
    if (impl == nullptr || buffer == nullptr || out_key == nullptr ||
        out_timestamp_ns == nullptr || received_size == nullptr) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    *received_size = 0;
    *out_timestamp_ns = 0;
    std::memset(out_key->robot, 0, sizeof(out_key->robot));
    out_key->channel_id = 0;

    PduRecord record{};
    const auto err = impl->recv_next(record);
    if (err != HAKO_PDU_ERR_OK) {
        return err;
    }

    if (record.payload.size() > buffer_size) {
        *received_size = record.payload.size();
        return HAKO_PDU_ERR_NO_SPACE;
    }

    const auto key_err = from_cpp_key(record.key, out_key);
    if (key_err != HAKO_PDU_ERR_OK) {
        return key_err;
    }

    if (!record.payload.empty()) {
        std::memcpy(buffer, record.payload.data(), record.payload.size());
    }
    *out_timestamp_ns = record.timestamp_ns;
    *received_size = record.payload.size();
    return HAKO_PDU_ERR_OK;
}

size_t hako_pdu_endpoint_get_pdu_size(
    const hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_key_t* key)
{
    const auto* impl = unwrap_const(endpoint);
    if (impl == nullptr || key == nullptr) {
        return 0;
    }
    PduKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return 0;
    }
    return impl->get_pdu_size(cpp_key);
}

int32_t hako_pdu_endpoint_get_pdu_channel_id(
    const hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_key_t* key)
{
    const auto* impl = unwrap_const(endpoint);
    if (impl == nullptr || key == nullptr) {
        return -1;
    }
    PduKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return -1;
    }
    return impl->get_pdu_channel_id(cpp_key);
}

HakoPduErrorType hako_pdu_endpoint_get_pdu_name(
    const hako_pdu_endpoint_handle_t* endpoint,
    const hako_pdu_resolved_key_t* key,
    char* out_name,
    size_t out_name_size)
{
    const auto* impl = unwrap_const(endpoint);
    if (impl == nullptr || key == nullptr || out_name == nullptr || out_name_size == 0) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    PduResolvedKey cpp_key{};
    if (!to_cpp_key(key, cpp_key)) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    const auto name = impl->get_pdu_name(cpp_key);
    if (name.empty()) {
        if (out_name_size > 0) {
            out_name[0] = '\0';
        }
        return HAKO_PDU_ERR_NO_ENTRY;
    }
    return copy_string_to_buffer(name, out_name, out_name_size);
}

} // extern "C"
