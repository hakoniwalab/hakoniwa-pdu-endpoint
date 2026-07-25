#include <gtest/gtest.h>

#include "hakoniwa/pdu/cache/cache_buffer.hpp"
#include "hakoniwa/pdu/cache/cache_queue.hpp"
#include "hakoniwa/pdu/cache/cache_config.hpp"

#include <array>

namespace {

using hakoniwa::pdu::PduLatestBuffer;
using hakoniwa::pdu::PduLatestQueue;
using hakoniwa::pdu::PduResolvedKey;
using hakoniwa::pdu::make_latest_cache;
using hakoniwa::pdu::make_queue_cache;

TEST(CacheRuntimeConfigTest, LatestAcceptsOnlyLatestConfig)
{
    PduLatestBuffer cache;
    EXPECT_EQ(cache.configure(make_latest_cache("latest")), HAKO_PDU_ERR_OK);
    EXPECT_EQ(cache.configure(make_queue_cache("queue", 2)), HAKO_PDU_ERR_INVALID_CONFIG);
}

TEST(CacheRuntimeConfigTest, QueueAcceptsOnlyQueueConfig)
{
    PduLatestQueue cache;
    EXPECT_EQ(cache.configure(make_queue_cache("queue", 2)), HAKO_PDU_ERR_OK);
    EXPECT_EQ(cache.configure(make_latest_cache("latest")), HAKO_PDU_ERR_INVALID_CONFIG);
}

TEST(CacheRuntimeConfigTest, QueueDepthComesFromMemoryConfig)
{
    PduLatestQueue cache;
    ASSERT_EQ(cache.configure(make_queue_cache("queue", 2)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(cache.start(), HAKO_PDU_ERR_OK);

    const PduResolvedKey key{"robot", 1};
    const std::array<std::byte, 1> first{std::byte{1}};
    const std::array<std::byte, 1> second{std::byte{2}};
    const std::array<std::byte, 1> third{std::byte{3}};
    ASSERT_EQ(cache.write(key, first), HAKO_PDU_ERR_OK);
    ASSERT_EQ(cache.write(key, second), HAKO_PDU_ERR_OK);
    ASSERT_EQ(cache.write(key, third), HAKO_PDU_ERR_OK);

    std::array<std::byte, 1> out{};
    size_t received = 0;
    ASSERT_EQ(cache.read(key, out, received), HAKO_PDU_ERR_OK);
    EXPECT_EQ(out[0], std::byte{2});
    ASSERT_EQ(cache.read(key, out, received), HAKO_PDU_ERR_OK);
    EXPECT_EQ(out[0], std::byte{3});
}

} // namespace
