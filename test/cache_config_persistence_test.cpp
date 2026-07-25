#include <gtest/gtest.h>

#include "hakoniwa/pdu/cache/cache_config_json.hpp"

#include <filesystem>

namespace {

using hakoniwa::pdu::CacheConfig;
using hakoniwa::pdu::CacheMode;
using hakoniwa::pdu::load_cache_config;
using hakoniwa::pdu::make_latest_cache;
using hakoniwa::pdu::make_queue_cache;
using hakoniwa::pdu::save_cache_config;

TEST(CacheConfigPersistenceTest, QueueRoundTrip)
{
    const auto expected = make_queue_cache("events", 16);
    const auto path = std::filesystem::temp_directory_path() / "hako_cache_config_queue.json";
    std::filesystem::remove(path);

    ASSERT_EQ(save_cache_config(expected, path.string()), HAKO_PDU_ERR_OK);
    CacheConfig actual;
    ASSERT_EQ(load_cache_config(path.string(), actual), HAKO_PDU_ERR_OK);

    EXPECT_EQ(actual.name, expected.name);
    EXPECT_EQ(actual.mode, CacheMode::Queue);
    EXPECT_EQ(actual.depth, expected.depth);
    std::filesystem::remove(path);
}

TEST(CacheConfigPersistenceTest, LatestRoundTrip)
{
    const auto expected = make_latest_cache("vehicle_state");
    const auto path = std::filesystem::temp_directory_path() / "hako_cache_config_latest.json";
    std::filesystem::remove(path);

    ASSERT_EQ(save_cache_config(expected, path.string()), HAKO_PDU_ERR_OK);
    CacheConfig actual;
    ASSERT_EQ(load_cache_config(path.string(), actual), HAKO_PDU_ERR_OK);

    EXPECT_EQ(actual.name, expected.name);
    EXPECT_EQ(actual.mode, CacheMode::Latest);
    std::filesystem::remove(path);
}

} // namespace
