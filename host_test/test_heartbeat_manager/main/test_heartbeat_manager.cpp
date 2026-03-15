#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_message_codec.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"

#include "heartbeat_manager.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

// ---------------------------------------------------------------------------
// Testable subclass — exposes the protected send_heartbeat()
// ---------------------------------------------------------------------------
class TestableHeartbeatManager : public HeartbeatManager
{
public:
    using HeartbeatManager::HeartbeatManager;
    void force_send_heartbeat() { send_heartbeat(); }
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class HeartbeatManagerTest : public ::testing::Test
{
protected:
    static constexpr NodeId MY_ID = 2;
    static constexpr NodeType PEER = 0x02;

    // Fake timer handle — any non-null pointer works as a stand-in
    TimerHandle_t fake_timer_ = reinterpret_cast<TimerHandle_t>(0xDEAD);

    NiceMock<MockTxManager> tx_mgr_;
    NiceMock<MockPeerManager> peer_mgr_;
    NiceMock<MockMessageCodec> codec_;
    NiceMock<MockFreeRTOSHAL> hal_freertos_;
    NiceMock<MockTimerHAL> hal_timer_;

    // Default encoded payload — non-empty so queue_packet is reached
    static constexpr size_t DUMMY_ENCODED_SIZE = 3;

    // Helper: build a minimal valid RxPacket for handle_request
    RxPacket make_heartbeat_packet(NodeId sender_id, uint8_t len_override = 0)
    {
        RxPacket pkt{};
        pkt.len = (len_override > 0) ? len_override : sizeof(HeartbeatMessage);

        MessageHeader hdr{};
        hdr.sender_node_id = sender_id;
        hdr.msg_type = MessageType::HEARTBEAT;
        memcpy(pkt.src_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);

        // codec_.decode_header will return this header
        ON_CALL(codec_, decode_header(_, _)).WillByDefault(Return(std::optional<MessageHeader>{hdr}));

        return pkt;
    }

    // Convenience: make codec_.encode() return a non-zero size
    void stub_encode_ok() { ON_CALL(codec_, encode(_, _, _, _, _)).WillByDefault(Return(DUMMY_ENCODED_SIZE)); }

    // Convenience: make codec_.encode() return 0 (failure)
    void stub_encode_fail() { ON_CALL(codec_, encode(_, _, _, _, _)).WillByDefault(Return(0)); }

    // Convenience: set up hal_freertos_ so init() succeeds for a PEER node
    void stub_timer_init_ok()
    {
        ON_CALL(hal_freertos_, timer_create(_, _, _, _, _)).WillByDefault(Return(fake_timer_));
        ON_CALL(hal_freertos_, timer_start(fake_timer_, _)).WillByDefault(Return(pdPASS));
    }

    // Convenience: stub deinit calls so the destructor doesn't assert
    void stub_timer_deinit_ok()
    {
        ON_CALL(hal_freertos_, timer_stop(fake_timer_, _)).WillByDefault(Return(pdPASS));
        ON_CALL(hal_freertos_, timer_delete(fake_timer_, _)).WillByDefault(Return(pdPASS));
    }
};

// ===========================================================================
// init()
// ===========================================================================

TEST_F(HeartbeatManagerTest, Init_Hub_DoesNotCreateTimer)
{
    HeartbeatManager hub(ReservedIds::HUB, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);

    EXPECT_CALL(hal_freertos_, timer_create(_, _, _, _, _)).Times(0);

    esp_err_t ret = hub.init(1000, ReservedTypes::HUB);

    EXPECT_EQ(ESP_OK, ret);
}

TEST_F(HeartbeatManagerTest, Init_Peer_CreatesAndStartsTimer)
{
    stub_timer_init_ok();
    stub_timer_deinit_ok();

    EXPECT_CALL(hal_freertos_, timer_create(_, _, _, _, _)).WillOnce(Return(fake_timer_));
    EXPECT_CALL(hal_freertos_, timer_start(fake_timer_, _)).WillOnce(Return(pdPASS));

    HeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);

    esp_err_t ret = node.init(1000, PEER);

    EXPECT_EQ(ESP_OK, ret);
}

TEST_F(HeartbeatManagerTest, Init_Peer_TimerCreateFails_ReturnsEspFail)
{
    ON_CALL(hal_freertos_, timer_create(_, _, _, _, _)).WillByDefault(Return(nullptr));

    HeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);

    esp_err_t ret = node.init(1000, PEER);

    EXPECT_EQ(ESP_FAIL, ret);
}

TEST_F(HeartbeatManagerTest, Init_Peer_TimerStartFails_ReturnsEspFail)
{
    stub_timer_deinit_ok();

    ON_CALL(hal_freertos_, timer_create(_, _, _, _, _)).WillByDefault(Return(fake_timer_));
    ON_CALL(hal_freertos_, timer_start(fake_timer_, _)).WillByDefault(Return(pdFAIL));

    HeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);

    esp_err_t ret = node.init(1000, PEER);

    EXPECT_EQ(ESP_FAIL, ret);
}

TEST_F(HeartbeatManagerTest, Init_Peer_ZeroInterval_DoesNotCreateTimer)
{
    HeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);

    EXPECT_CALL(hal_freertos_, timer_create(_, _, _, _, _)).Times(0);

    esp_err_t ret = node.init(0, PEER);

    EXPECT_EQ(ESP_OK, ret);
}

TEST_F(HeartbeatManagerTest, Deinit_StopTimerFails_ReturnError)
{
    stub_timer_init_ok();
    stub_timer_deinit_ok();

    HeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);

    EXPECT_CALL(hal_freertos_, timer_stop(fake_timer_, _)).WillOnce(Return(pdFAIL));
    EXPECT_EQ(ESP_OK, node.init(1000, PEER));

    EXPECT_EQ(ESP_FAIL, node.deinit());
}

TEST_F(HeartbeatManagerTest, Deinit_DeleteTimerFails_ReturnError)
{
    stub_timer_init_ok();
    stub_timer_deinit_ok();

    HeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);

    EXPECT_CALL(hal_freertos_, timer_delete(fake_timer_, _)).WillOnce(Return(pdFAIL));
    EXPECT_EQ(ESP_OK, node.init(1000, PEER));

    EXPECT_EQ(ESP_FAIL, node.deinit());
}

// ===========================================================================
// handle_request()
// ===========================================================================

TEST_F(HeartbeatManagerTest, HandleRequest_DecodeHeaderFails_NothingQueued)
{
    HeartbeatManager hub(ReservedIds::HUB, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    hub.init(0, ReservedTypes::HUB);

    ON_CALL(codec_, decode_header(_, _)).WillByDefault(Return(std::nullopt));

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    EXPECT_CALL(peer_mgr_, update_last_seen(_, _)).Times(0);

    RxPacket pkt{};
    pkt.len = sizeof(HeartbeatMessage);
    hub.handle_request(pkt);
}

TEST_F(HeartbeatManagerTest, HandleRequest_MalformedLength_NothingQueued)
{
    HeartbeatManager hub(ReservedIds::HUB, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    hub.init(0, ReservedTypes::HUB);

    // decode_header succeeds but packet length is too short
    RxPacket pkt = make_heartbeat_packet(MY_ID, sizeof(HeartbeatMessage) - 1);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    EXPECT_CALL(peer_mgr_, update_last_seen(_, _)).Times(0);

    hub.handle_request(pkt);
}

TEST_F(HeartbeatManagerTest, HandleRequest_Valid_UpdatesLastSeenAndQueuesResponse)
{
    stub_encode_ok();

    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(5000000)); // 5 s

    HeartbeatManager hub(ReservedIds::HUB, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    hub.init(0, ReservedTypes::HUB);

    RxPacket pkt = make_heartbeat_packet(MY_ID);

    EXPECT_CALL(peer_mgr_, update_last_seen(MY_ID, 5000 /* now_ms */));
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);

    hub.handle_request(pkt);
}

TEST_F(HeartbeatManagerTest, HandleRequest_Valid_ResponseHeaderIsCorrect)
{
    stub_encode_ok();
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));

    HeartbeatManager hub(ReservedIds::HUB, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    hub.init(0, ReservedTypes::HUB);

    RxPacket pkt = make_heartbeat_packet(MY_ID);

    MessageHeader captured_hdr{};
    ON_CALL(codec_, encode(_, _, _, _, _)).WillByDefault([&](const MessageHeader &hdr, const void *, size_t, uint8_t *, size_t) {
        captured_hdr = hdr;
        return DUMMY_ENCODED_SIZE;
    });

    hub.handle_request(pkt);

    EXPECT_EQ(MessageType::HEARTBEAT_RESPONSE, captured_hdr.msg_type);
    EXPECT_EQ(ReservedIds::HUB, captured_hdr.sender_node_id);
    EXPECT_EQ(MY_ID, captured_hdr.dest_node_id);
}

TEST_F(HeartbeatManagerTest, HandleRequest_EncodeFails_NothingQueued)
{
    stub_encode_fail();
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));

    HeartbeatManager hub(ReservedIds::HUB, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    hub.init(0, ReservedTypes::HUB);

    RxPacket pkt = make_heartbeat_packet(MY_ID);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);

    hub.handle_request(pkt);
}

// ===========================================================================
// handle_response()
// ===========================================================================

TEST_F(HeartbeatManagerTest, HandleResponse_NotifiesLinkAlive)
{
    HeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    node.init(0, PEER);

    EXPECT_CALL(tx_mgr_, notify_link_alive()).Times(1);

    node.handle_response(ReservedIds::HUB);
}

// ===========================================================================
// send_heartbeat()  (via TestableHeartbeatManager)
// ===========================================================================

TEST_F(HeartbeatManagerTest, SendHeartbeat_HubUnknown_UsesBroadcastMac)
{
    stub_encode_ok();
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    ON_CALL(peer_mgr_, find_mac(ReservedIds::HUB, _)).WillByDefault(Return(false));

    TestableHeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    node.init(0, PEER);

    // BROADCAST_MAC is defined in protocol_types.hpp
    // const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    TxPacket captured{};
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(::testing::Invoke([&](const TxPacket &pkt) -> int {
        captured = pkt;
        return 0;
    }));

    node.force_send_heartbeat();

    EXPECT_EQ(0, memcmp(BROADCAST_MAC, captured.dest_mac, 6));
}

TEST_F(HeartbeatManagerTest, SendHeartbeat_HubKnown_UsesUnicastMac)
{
    stub_encode_ok();
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));

    const uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    ON_CALL(peer_mgr_, find_mac(ReservedIds::HUB, _)).WillByDefault(::testing::Invoke([&](NodeId, uint8_t *out_mac) {
        memcpy(out_mac, hub_mac, 6);
        return true;
    }));

    TestableHeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    node.init(0, PEER);

    TxPacket captured{};
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(::testing::Invoke([&](const TxPacket &pkt) -> int {
        captured = pkt;
        return 0;
    }));

    node.force_send_heartbeat();

    EXPECT_EQ(0, memcmp(hub_mac, captured.dest_mac, 6));
}

TEST_F(HeartbeatManagerTest, SendHeartbeat_HeaderIsCorrect)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    ON_CALL(peer_mgr_, find_mac(_, _)).WillByDefault(Return(false));

    TestableHeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    node.init(0, PEER);

    MessageHeader captured_hdr{};
    ON_CALL(codec_, encode(_, _, _, _, _)).WillByDefault([&](const MessageHeader &hdr, const void *, size_t, uint8_t *, size_t) {
        captured_hdr = hdr;
        return DUMMY_ENCODED_SIZE;
    });

    node.force_send_heartbeat();

    EXPECT_EQ(MessageType::HEARTBEAT, captured_hdr.msg_type);
    EXPECT_EQ(MY_ID, captured_hdr.sender_node_id);
    EXPECT_EQ(ReservedIds::HUB, captured_hdr.dest_node_id);
}

TEST_F(HeartbeatManagerTest, SendHeartbeat_EncodeFails_NothingQueued)
{
    stub_encode_fail();
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    ON_CALL(peer_mgr_, find_mac(_, _)).WillByDefault(Return(false));

    TestableHeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    node.init(0, PEER);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);

    node.force_send_heartbeat();
}

// ===========================================================================
// update_node_id()
// ===========================================================================

TEST_F(HeartbeatManagerTest, UpdateNodeId_SubsequentHeartbeatUsesNewId)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    ON_CALL(peer_mgr_, find_mac(_, _)).WillByDefault(Return(false));

    TestableHeartbeatManager node(MY_ID, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    node.init(0, PEER);
    node.update_node_id(99);

    MessageHeader captured_hdr{};
    ON_CALL(codec_, encode(_, _, _, _, _)).WillByDefault([&](const MessageHeader &hdr, const void *, size_t, uint8_t *, size_t) {
        captured_hdr = hdr;
        return DUMMY_ENCODED_SIZE;
    });

    node.force_send_heartbeat();

    EXPECT_EQ(99, captured_hdr.sender_node_id);
}

TEST_F(HeartbeatManagerTest, HandleRequest_ResponseContainsCurrentChannel)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));

    HeartbeatManager hub(ReservedIds::HUB, tx_mgr_, peer_mgr_, codec_, hal_freertos_, hal_timer_);
    hub.init(0, ReservedTypes::HUB);
    hub.set_channel(6);

    RxPacket pkt = make_heartbeat_packet(MY_ID);

    uint8_t captured_channel = 0;
    ON_CALL(codec_, encode(_, _, _, _, _))
        .WillByDefault([&](const MessageHeader &, const void *payload, size_t len, uint8_t *, size_t) -> size_t {
            // Reconstruction of the payload into the HeartbeatResponse struct
            HeartbeatResponse response{};
            memcpy(&response.server_time_ms, payload, len);
            captured_channel = response.wifi_channel;
            return DUMMY_ENCODED_SIZE;
        });

    hub.handle_request(pkt);

    EXPECT_EQ(6, captured_channel);
}