// host_test/common/mock_storage_manager.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_storage_manager.hpp"

class MockStorageManager : public IStorageManager
{
public:
    MOCK_METHOD(esp_err_t, load, (uint8_t &, etl::ivector<PersistentPeer> &), (override));
    MOCK_METHOD(esp_err_t, save, (uint8_t, const etl::ivector<PersistentPeer> &, bool), (override));
};
