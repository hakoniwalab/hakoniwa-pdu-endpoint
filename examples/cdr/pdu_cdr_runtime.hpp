#ifndef HAKO_PDU_CDR_RUNTIME_HPP_
#define HAKO_PDU_CDR_RUNTIME_HPP_

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastcdr/config.h>

namespace hako::pdu::cdr
{

static inline int get_serialized_data_length(eprosima::fastcdr::Cdr& cdr)
{
#if FASTCDR_VERSION_MAJOR == 1
    return static_cast<int>(cdr.getSerializedDataLength());
#else
    return static_cast<int>(cdr.get_serialized_data_length());
#endif
}

template <typename Container>
static inline auto resize_if_possible(Container& container, size_t size)
    -> decltype(container.resize(size), void())
{
    container.resize(size);
}

template <typename Container>
static inline void resize_if_possible(Container&, size_t)
{
    // std::array and C-style fixed arrays do not need resizing.
}

template <typename T>
static inline void serialize_fixed_array(eprosima::fastcdr::Cdr& cdr, const T& array_value, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        cdr << array_value[i];
    }
}

template <typename T>
static inline void deserialize_fixed_array(eprosima::fastcdr::Cdr& cdr, T& array_value, size_t size)
{
    resize_if_possible(array_value, size);
    for (size_t i = 0; i < size; ++i) {
        cdr >> array_value[i];
    }
}

template <typename T>
static inline void serialize_sequence_length(eprosima::fastcdr::Cdr& cdr, const T& array_value)
{
    if (array_value.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        throw std::runtime_error("CDR sequence too large");
    }
    cdr << static_cast<uint32_t>(array_value.size());
}

template <typename T>
static inline void serialize_sequence(eprosima::fastcdr::Cdr& cdr, const T& array_value)
{
    serialize_sequence_length(cdr, array_value);
    for (const auto& elem : array_value) {
        cdr << elem;
    }
}

template <typename T>
static inline void deserialize_sequence(eprosima::fastcdr::Cdr& cdr, T& array_value)
{
    uint32_t length = 0;
    cdr >> length;
    array_value.resize(length);
    for (uint32_t i = 0; i < length; ++i) {
        cdr >> array_value[i];
    }
}

template <typename T>
static inline void serialize_bool_sequence(eprosima::fastcdr::Cdr& cdr, const T& array_value)
{
    serialize_sequence_length(cdr, array_value);
    for (const auto& elem : array_value) {
        cdr << static_cast<bool>(elem != 0);
    }
}

template <typename T>
static inline void deserialize_bool_sequence(eprosima::fastcdr::Cdr& cdr, T& array_value)
{
    uint32_t length = 0;
    cdr >> length;
    array_value.resize(length);
    for (uint32_t i = 0; i < length; ++i) {
        bool elem = false;
        cdr >> elem;
        array_value[i] = elem ? 1 : 0;
    }
}

template <typename T>
static inline void serialize_bool_fixed_array(eprosima::fastcdr::Cdr& cdr, const T& array_value, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        cdr << static_cast<bool>(array_value[i] != 0);
    }
}

template <typename T>
static inline void deserialize_bool_fixed_array(eprosima::fastcdr::Cdr& cdr, T& array_value, size_t size)
{
    resize_if_possible(array_value, size);
    for (size_t i = 0; i < size; ++i) {
        bool elem = false;
        cdr >> elem;
        array_value[i] = elem ? 1 : 0;
    }
}

static inline eprosima::fastcdr::Cdr create_dds_cdr(eprosima::fastcdr::FastBuffer& fastbuffer)
{
#if FASTCDR_VERSION_MAJOR == 1
    return eprosima::fastcdr::Cdr(
        fastbuffer,
        eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
        eprosima::fastcdr::Cdr::DDS_CDR);
#else
    return eprosima::fastcdr::Cdr(
        fastbuffer,
        eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
        eprosima::fastcdr::CdrVersion::DDS_CDR);
#endif
}

} // namespace hako::pdu::cdr

#endif /* HAKO_PDU_CDR_RUNTIME_HPP_ */
