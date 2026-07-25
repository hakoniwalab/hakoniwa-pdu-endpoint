#include <gtest/gtest.h>

#include "hakoniwa/pdu/cache/cache_config.hpp"
#include "hakoniwa/pdu/cache/cache_config_json.hpp"

#include <filesystem>
#include <string>

namespace {

using hakoniwa::pdu::CacheConfig;
using hakoniwa::pdu::CacheMode;
using hakoniwa::pdu::load_cache_config;
using hakoniwa::pdu::make_latest_cache;
using hakoniwa::pdu::make_queue_cache;
using hakoniwa::pdu::save_cache_config;
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

TEST(CacheConfigTest, QueueConfigRoundTripsThroughJsonFile)
{
    const auto expected = make_queue_cache("events", 16);
    const auto path = std::filesystem::temp_directory_path() / "hako_cache_config_roundtrip.json";
    std::filesystem::remove(path);

    ASSERT_EQ(save_cache_config(expected, path.string()), HAKO_PDU_ERR_OK);

    CacheConfig actual;
    ASSERT_EQ(load_cache_config(path.string(), actual), HAKO_PDU_ERR_OK);
    EXPECT_EQ(actual.name, expected.name);
    EXPECT_EQ(actual.mode, expected.mode);
    EXPECT_EQ(actual.depth, expected.depth);

    std::filesystem::remove(path);
}

TEST(CacheConfigTest, LatestConfigRoundTripsThroughJsonFile)
{
    const auto expected = make_latest_cache("vehicle_state");
    const auto path = std::filesystem::temp_directory_path() / "hako_cache_config_latest_roundtrip.json";
    std::filesystem::remove(path);

    ASSERT_EQ(save_cache_config(expected, path.string()), HAKO_PDU_ERR_OK);

    CacheConfig actual;
    ASSERT_EQ(load_cache_config(path.string(), actual), HAKO_PDU_ERR_OK);
    EXPECT_EQ(actual.name, expected.name);
    EXPECT_EQ(actual.mode, expected.mode);

    std::filesystem::remove(path);
}

} // namespace
