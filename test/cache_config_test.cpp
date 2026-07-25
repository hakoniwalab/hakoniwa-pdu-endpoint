#include <gtest/gtest.h>

#include "hakoniwa/pdu/cache/cache_config.hpp"

#include <string>

namespace {

using hakoniwa::pdu::CacheMode;
using hakoniwa::pdu::make_latest_cache;
using hakoniwa::pdu::make_queue_cache;
using hakoniwa::pdu::validate_cache_config;

TEST(CacheConfigTest, LatestFactoryCreatesValidConfig)
{
    const auto config = make_latest_cache("vehicle_state");

    EXPECT_EQ(config.name, "vehicle_state");
    EXPECT_EQ(config.mode, CacheMode::Latest);
    EXPECT_EQ(validate_cache_config(config), HAKO_PDU_ERR_OK);
}

TEST(CacheConfigTest, QueueFactoryCreatesValidConfig)
{
    const auto config = make_queue_cache("events", 16);

    EXPECT_EQ(config.name, "events");
    EXPECT_EQ(config.mode, CacheMode::Queue);
    EXPECT_EQ(config.depth, 16U);
    EXPECT_EQ(validate_cache_config(config), HAKO_PDU_ERR_OK);
}

TEST(CacheConfigTest, RejectsInvalidName)
{
    EXPECT_EQ(validate_cache_config(make_latest_cache("")), HAKO_PDU_ERR_INVALID_CONFIG);
    EXPECT_EQ(
        validate_cache_config(make_latest_cache(std::string(257, 'x'))),
        HAKO_PDU_ERR_INVALID_CONFIG);
}

TEST(CacheConfigTest, RejectsQueueDepthOutsideSchemaRange)
{
    EXPECT_EQ(validate_cache_config(make_queue_cache("events", 0)), HAKO_PDU_ERR_INVALID_CONFIG);
    EXPECT_EQ(validate_cache_config(make_queue_cache("events", 1025)), HAKO_PDU_ERR_INVALID_CONFIG);
}

} // namespace
