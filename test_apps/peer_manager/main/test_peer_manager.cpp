#include <atomic>
#include <stdio.h>

#include "esp_log_level.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_now.h"
#include "esp_system.h"

#include "mock_storage.hpp"
#include "peer_manager.hpp"
#include "unity.h"

// --- Wrapper / Mock implementation ---
extern "C" {
esp_err_t __real_esp_now_add_peer(const esp_now_peer_info_t *peer);
esp_err_t __real_esp_now_del_peer(const uint8_t *peer_addr);
esp_err_t __real_esp_now_mod_peer(const esp_now_peer_info_t *peer);

// Global mock control
static esp_err_t s_mock_add_ret = ESP_OK;
static esp_err_t s_mock_del_ret = ESP_OK;
static esp_err_t s_mock_mod_ret = ESP_OK;

esp_err_t __wrap_esp_now_add_peer(const esp_now_peer_info_t *peer)
{
    return s_mock_add_ret;
}
esp_err_t __wrap_esp_now_del_peer(const uint8_t *peer_addr)
{
    return s_mock_del_ret;
}
esp_err_t __wrap_esp_now_mod_peer(const esp_now_peer_info_t *peer)
{
    return s_mock_mod_ret;
}

// Helper functions to mimic CMock
void esp_now_add_peer_IgnoreAndReturn(esp_err_t ret)
{
    s_mock_add_ret = ret;
}
void esp_now_del_peer_IgnoreAndReturn(esp_err_t ret)
{
    s_mock_del_ret = ret;
}
void esp_now_mod_peer_IgnoreAndReturn(esp_err_t ret)
{
    s_mock_mod_ret = ret;
}
}
// -------------------------------------

enum class TestNodeId : NodeId
{
    TEST_HUB      = 1,
    TEST_SENSOR_A = 10,
    TEST_SENSOR_B = 11,
    NON_EXISTENT  = 90
};

enum class TestNodeType : NodeType
{
    HUB    = 1,
    SENSOR = 2
};

TEST_CASE("LOG on", "[wifi][log]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
}

TEST_CASE("LOG off", "[wifi][log]")
{
    esp_log_level_set("*", ESP_LOG_NONE);
}

// Helper for passing args to tasks
struct TestTaskArgs
{
    RealPeerManager *pm;
    int start_id;
    int count;
    bool is_add; // true for add, false for remove
    int delay_ticks;
};

void task_add_peers(void *pvParameters)
{
    TestTaskArgs *args = (TestTaskArgs *)pvParameters;
    for (int i = 0; i < args->count; i++) {
        uint8_t mac[6] = {0x00, 0x01, 0x02, 0x03, 0x04, (uint8_t)(args->start_id + i)};
        args->pm->add((NodeId)(args->start_id + i), mac, 1, (NodeType)TestNodeType::SENSOR);
        if (args->delay_ticks > 0)
            vTaskDelay(args->delay_ticks);
    }
    vTaskDelete(NULL);
}

void task_remove_peers(void *pvParameters)
{
    TestTaskArgs *args = (TestTaskArgs *)pvParameters;
    for (int i = 0; i < args->count; i++) {
        args->pm->remove((NodeId)(args->start_id + i));
        if (args->delay_ticks > 0)
            vTaskDelay(args->delay_ticks);
    }
    vTaskDelete(NULL);
}

// TEST 1: Limit adds to avoid MAX_PEERS overflow
// MAX_PEERS is 19. We will add 18 peers total.
// Task 1: 9 peers (100-108)
// Task 2: 9 peers (109-117)
TEST_CASE("PeerManager is thread-safe for concurrent adds", "[peer_manager][concurrency]")
{
    // Setup mocks
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    TestTaskArgs args1 = {&pm, 100, 9, true, 2}; // 9 peers
    TestTaskArgs args2 = {&pm, 109, 9, true, 2}; // 9 peers

    // Create two tasks adding different peers
    TaskHandle_t t1, t2;
    xTaskCreate(task_add_peers, "add_1", 4096, &args1, 5, &t1);
    xTaskCreate(task_add_peers, "add_2", 4096, &args2, 5, &t2);

    vTaskDelay(pdMS_TO_TICKS(300));

    // Verify
    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(18, peers.size());

    uint8_t mac_check[6];
    for (int i = 0; i < 9; i++) {
        TEST_ASSERT_TRUE(pm.find_mac((NodeId)(100 + i), mac_check)); // From Task 1
        TEST_ASSERT_TRUE(pm.find_mac((NodeId)(109 + i), mac_check)); // From Task 2
    }
}

// TEST 2: Concurrent Add and Remove keeping within limits
// Pre-fill: 9 peers (10-18)
// Task 1: Add 9 new peers (100-108) -> Total hits 18
// Task 2: Remove the 9 old peers (10-18)
// Result: Should have 9 peers (only the new ones)
TEST_CASE("PeerManager is thread-safe for concurrent add/remove", "[peer_manager][concurrency]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Pre-populate 9 peers
    for (int i = 0; i < 9; i++) {
        uint8_t mac[6] = {0x00, 0x01, 0x02, 0x03, 0x04, (uint8_t)(10 + i)};
        pm.add((NodeId)(10 + i), mac, 1, (NodeType)TestNodeType::SENSOR);
    }
    TEST_ASSERT_EQUAL(9, pm.get_all().size());

    // Task 1 adds 9 NEW peers (100-108)
    TestTaskArgs args_add = {&pm, 100, 9, true, 2};
    // Task 2 removes the 9 OLD peers (10-18)
    TestTaskArgs args_remove = {&pm, 10, 9, false, 2};

    TaskHandle_t t1, t2;
    xTaskCreate(task_add_peers, "add", 4096, &args_add, 5, &t1);
    xTaskCreate(task_remove_peers, "rem", 4096, &args_remove, 5, &t2);

    vTaskDelay(pdMS_TO_TICKS(300));

    auto peers = pm.get_all();
    // Final count should be 9 (Added 9, Removed 9, started with 9)
    TEST_ASSERT_EQUAL(9, peers.size());

    uint8_t mac_check[6];
    for (int i = 0; i < 9; i++) {
        TEST_ASSERT_TRUE(pm.find_mac((NodeId)(100 + i), mac_check)); // New should be present
        TEST_ASSERT_FALSE(pm.find_mac((NodeId)(10 + i), mac_check)); // Old should be removed
    }
}

void task_stress(void *pvParameters)
{
    RealPeerManager *pm = (RealPeerManager *)pvParameters;
    uint8_t mac[]       = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Use a small pool of IDs to force collisions and churn
    // Pool size 30, MAX_PEERS is 19.
    // This forces LRU evictions if many adds happen.
    int id_base  = 100;
    int id_range = 30;

    for (int i = 0; i < 50; i++) {
        int op    = esp_random() % 3;
        NodeId id = (NodeId)(esp_random() % id_range + id_base);
        mac[5]    = (uint8_t)id;

        switch (op) {
        case 0: // Add
            pm->add(id, mac, 1, (NodeType)TestNodeType::SENSOR);
            break;
        case 1: // Remove
            pm->remove(id);
            break;
        case 2: // Find
            pm->find_mac(id, nullptr);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    vTaskDelete(NULL);
}

TEST_CASE("PeerManager stress test", "[peer_manager][stress]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    const int NUM_TASKS = 4;
    for (int i = 0; i < NUM_TASKS; i++) {
        xTaskCreate(task_stress, "stress", 4096, &pm, 5, NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(300));

    // Verify robustness
    auto peers = pm.get_all();
    TEST_ASSERT_LESS_OR_EQUAL(19, peers.size()); // Should never exceed MAX_PEERS

    // Optional: Check for duplicates?
    // With thread safety, there should be no duplicate NodeIds
    for (size_t i = 0; i < peers.size(); i++) {
        for (size_t j = i + 1; j < peers.size(); j++) {
            TEST_ASSERT_NOT_EQUAL(peers[i].node_id, peers[j].node_id);
        }
    }
}

TEST_CASE("PeerManager concurrent add operations", "[peer_manager][concurrency]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    // Structure for tasks
    struct TaskData
    {
        RealPeerManager *pm;
        SemaphoreHandle_t done_signal;
        int id_start;
        int count;
    };

    // Lambda for task
    auto add_peers_task = [](void *param) {
        TaskData *data = static_cast<TaskData *>(param);

        for (int i = 0; i < data->count; i++) {
            uint8_t mac[6] = {
                (uint8_t)(data->id_start >> 8), (uint8_t)(data->id_start & 0xFF), 0x00, 0x00, 0x00, (uint8_t)i};

            data->pm->add(static_cast<NodeId>(data->id_start + i), mac, 1, (NodeType)TestNodeType::SENSOR);

            vTaskDelay(pdMS_TO_TICKS(1)); // Força interleaving
        }

        xSemaphoreGive(data->done_signal);
        vTaskDelete(NULL);
    };

    // Setup
    SemaphoreHandle_t task1_done = xSemaphoreCreateBinary();
    SemaphoreHandle_t task2_done = xSemaphoreCreateBinary();

    TaskData data1 = {&pm, task1_done, 100, 5};
    TaskData data2 = {&pm, task2_done, 200, 5};

    // Create tasks
    xTaskCreate(add_peers_task, "add1", 4096, &data1, 8, NULL);
    xTaskCreate(add_peers_task, "add2", 4096, &data2, 8, NULL);

    // Wait for completion
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(task1_done, pdMS_TO_TICKS(5000)));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(task2_done, pdMS_TO_TICKS(5000)));

    // Cleanup
    vSemaphoreDelete(task1_done);
    vSemaphoreDelete(task2_done);

    // Validation
    auto peers = pm.get_all();
    TEST_ASSERT_EQUAL(10, peers.size()); // 5 + 5

    // Verify that all peers were added
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(pm.find_mac(static_cast<NodeId>(100 + i), nullptr));
        TEST_ASSERT_TRUE(pm.find_mac(static_cast<NodeId>(200 + i), nullptr));
    }
}

TEST_CASE("PeerManager is thread-safe", "[peer_manager][concurrency]")
{
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);

    MockStorage storage;
    RealPeerManager pm(storage);

    SemaphoreHandle_t start_sem = xSemaphoreCreateBinary();
    std::atomic<bool> task1_done(false);
    std::atomic<bool> task2_done(false);

    auto task_add = [](void *arg) {
        struct Args
        {
            RealPeerManager *pm;
            SemaphoreHandle_t start_sem;
            std::atomic<bool> *done;
            int id_offset;
        };
        Args *a = (Args *)arg;

        // Wait for start signal (synchronizes start)
        // This ensures that both tasks try to access the semaphore at the same time
        xSemaphoreTake(a->start_sem, portMAX_DELAY);

        for (int i = 0; i < 8; i++) {
            uint8_t mac[6] = {(uint8_t)a->id_offset, 0, 0, 0, 0, (uint8_t)i};
            a->pm->add((NodeId)(a->id_offset + i), mac, 1, (NodeType)TestNodeType::SENSOR);
        }

        *a->done = true;
        vTaskDelete(NULL);
    };

    // Setup
    struct
    {
        RealPeerManager *pm;
        SemaphoreHandle_t start_sem;
        std::atomic<bool> *done;
        int id_offset;
    } args1 = {&pm, start_sem, &task1_done, 100}, args2 = {&pm, start_sem, &task2_done, 200};

    xTaskCreate(task_add, "task1", 4096, &args1, 5, NULL);
    xTaskCreate(task_add, "task2", 4096, &args2, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(10)); // Let tasks block on the semaphore

    // Release both tasks simultaneously (force race)
    xSemaphoreGive(start_sem);
    xSemaphoreGive(start_sem);

    // Wait for completion
    while (!task1_done || !task2_done) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vSemaphoreDelete(start_sem);

    // Validate result
    TEST_ASSERT_EQUAL(16, pm.get_all().size());

    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(pm.find_mac((NodeId)(100 + i), nullptr));
        TEST_ASSERT_TRUE(pm.find_mac((NodeId)(200 + i), nullptr));
    }
}
