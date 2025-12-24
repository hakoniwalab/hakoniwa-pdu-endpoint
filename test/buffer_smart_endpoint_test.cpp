#include <gtest/gtest.h>
#include "hakoniwa/pdu/smart_endpoint/buffer_smart_endpoint.hpp"
#include <vector>
#include <string>

class BufferSmartEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(BufferSmartEndpointTest, LatestModeTest) {
    hakoniwa::pdu::BufferSmartEndpoint endpoint;

    // Test opening with latest_config.json
    ASSERT_EQ(endpoint.open("test/latest_config.json"), HAKO_PDU_ERR_OK);
    EXPECT_EQ(endpoint.get_name(), "test_latest_buffer");

    std::string robot_name = "test_robot";
    hako_pdu_uint32_t channel_id = 1;
    std::vector<std::byte> write_data1 = {(std::byte)0x01, (std::byte)0x02};
    std::vector<std::byte> write_data2 = {(std::byte)0x03, (std::byte)0x04, (std::byte)0x05};

    // Write first data
    ASSERT_EQ(endpoint.write(robot_name, channel_id, write_data1), HAKO_PDU_ERR_OK);

    // Write second data, should overwrite the first
    ASSERT_EQ(endpoint.write(robot_name, channel_id, write_data2), HAKO_PDU_ERR_OK);

    // Read the data back
    std::string read_robot_name = "test_robot";
    hako_pdu_uint32_t read_channel_id = 1;
    std::vector<std::byte> read_buffer(10);
    hako_pdu_uint32_t read_len = 0;

    ASSERT_EQ(endpoint.read(read_robot_name, read_channel_id, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data2.size());
    for (size_t i = 0; i < read_len; ++i) {
        EXPECT_EQ(read_buffer[i], write_data2[i]);
    }
    
    // Read again, should still be there
    ASSERT_EQ(endpoint.read(read_robot_name, read_channel_id, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data2.size());

    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(BufferSmartEndpointTest, QueueModeTest) {
    hakoniwa::pdu::BufferSmartEndpoint endpoint;

    // Test opening with queue_config.json
    ASSERT_EQ(endpoint.open("test/queue_config.json"), HAKO_PDU_ERR_OK);
    EXPECT_EQ(endpoint.get_name(), "test_queue_buffer");

    std::string robot_name = "test_robot";
    hako_pdu_uint32_t channel_id = 1;
    std::vector<std::byte> write_data1 = {(std::byte)0x11};
    std::vector<std::byte> write_data2 = {(std::byte)0x22};
    std::vector<std::byte> write_data3 = {(std::byte)0x33};
    std::vector<std::byte> write_data4 = {(std::byte)0x44};

    // Write three items, should all be in the queue (depth is 3)
    ASSERT_EQ(endpoint.write(robot_name, channel_id, write_data1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.write(robot_name, channel_id, write_data2), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.write(robot_name, channel_id, write_data3), HAKO_PDU_ERR_OK);

    // Write a fourth, the first one should be dropped
    ASSERT_EQ(endpoint.write(robot_name, channel_id, write_data4), HAKO_PDU_ERR_OK);
    
    std::string read_robot_name = "test_robot";
    hako_pdu_uint32_t read_channel_id = 1;
    std::vector<std::byte> read_buffer(10);
    hako_pdu_uint32_t read_len = 0;

    // Read first item (should be write_data2)
    ASSERT_EQ(endpoint.read(read_robot_name, read_channel_id, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data2.size());
    EXPECT_EQ(read_buffer[0], write_data2[0]);

    // Read second item (should be write_data3)
    ASSERT_EQ(endpoint.read(read_robot_name, read_channel_id, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data3.size());
    EXPECT_EQ(read_buffer[0], write_data3[0]);

    // Read third item (should be write_data4)
    ASSERT_EQ(endpoint.read(read_robot_name, read_channel_id, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data4.size());
    EXPECT_EQ(read_buffer[0], write_data4[0]);

    // Read again, should be empty
    ASSERT_EQ(endpoint.read(read_robot_name, read_channel_id, read_buffer, read_len), HAKO_PDU_ERR_NO_ENTRY);
    EXPECT_EQ(read_len, 0);

    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}


TEST_F(BufferSmartEndpointTest, StoreFrameTest) {
    hakoniwa::pdu::BufferSmartEndpoint endpoint;
    ASSERT_EQ(endpoint.open("test/latest_config.json"), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::PduMeta meta = {};
    meta.robot = "frame_robot";
    meta.channel_id = 42;
    std::vector<std::byte> body_data = {(std::byte)0xAA, (std::byte)0xBB};
    hakoniwa::pdu::PduFrameView frame = {meta, body_data, {}};

    ASSERT_EQ(endpoint.store_frame(frame), HAKO_PDU_ERR_OK);

    std::string read_robot_name = "frame_robot";
    hako_pdu_uint32_t read_channel_id = 42;
    std::vector<std::byte> read_buffer(10);
    hako_pdu_uint32_t read_len = 0;

    ASSERT_EQ(endpoint.read(read_robot_name, read_channel_id, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, body_data.size());
    EXPECT_EQ(read_buffer[0], body_data[0]);
    EXPECT_EQ(read_buffer[1], body_data[1]);
    
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(BufferSmartEndpointTest, InvalidConfigTest) {
    hakoniwa::pdu::BufferSmartEndpoint endpoint;

    // Non-existent file
    ASSERT_EQ(endpoint.open("non_existent_config.json"), HAKO_PDU_ERR_FILE_NOT_FOUND);

    // Invalid JSON
    ASSERT_EQ(endpoint.open("CMakeLists.txt"), HAKO_PDU_ERR_INVALID_JSON);
}
