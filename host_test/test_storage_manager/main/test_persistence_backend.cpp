#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mock_nvs_hal.hpp"

#include "persistence_backends.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// class NvsBackendTest : public ::testing::Test
// {
// protected:
//     NiceMock<MockNvsHAL> mock_rtc_hal;
//     NiceMock<MockNvsHAL> mock_nvs_hal;

//     NvsBackend backend;

//     void SetUp() override { backend = NvsBackend(&mock_nvs_hal); }
// };
