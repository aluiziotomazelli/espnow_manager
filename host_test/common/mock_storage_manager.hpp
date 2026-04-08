// host_test/common/mock_storage_manager.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_storage_manager.hpp"

class MockStorageManager : public IStorageManager
{
public:
    MOCK_METHOD(esp_err_t, load_channel, (uint8_t&), (override));
    MOCK_METHOD(esp_err_t, store_channel, (uint8_t), (override));
    MOCK_METHOD(esp_err_t, load_peers, (etl::ivector<PersistentPeer>&), (override));
    MOCK_METHOD(esp_err_t, store_peers, (const etl::ivector<PersistentPeer>&, bool), (override));
    MOCK_METHOD(esp_err_t, load_stats, (etl::ivector<PeerStatisticsPersist>&), (override));
    MOCK_METHOD(esp_err_t, store_stats, (const etl::ivector<PeerStatisticsPersist>&), (override));
};
