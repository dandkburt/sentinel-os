#include <gtest/gtest.h>

#include "event_broker.h"
#include "policy.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace sentinel::core::event;
using namespace sentinel::services::policy;

class EventBrokerRealTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize_policy_service();
        initialize_event_broker();
        reset_event_broker_for_tests();
        reset_event_broker_telemetry_for_tests();
        set_event_broker_queue_capacity_for_tests(32);
    }

    void TearDown() override {
        flush_event_broker_for_tests();
        reset_event_broker_telemetry_for_tests();
        reset_event_broker_for_tests();
    }
};

TEST_F(EventBrokerRealTest, SubscribeAndPublishWithMatchingCapability) {
    auto& broker = get_event_broker_interface();
    auto& policy_service = get_policy_service_interface();

    const std::string token = policy_service.capability_engine().issue_token("broker_ext", {"event:alerts"});
    ASSERT_FALSE(token.empty());

    std::mutex events_mutex;
    std::vector<std::string> received_payloads;

    std::string error;
    ASSERT_TRUE(broker.subscribe("alerts", token, [&](const RuntimeEvent& event) {
        std::lock_guard<std::mutex> lock(events_mutex);
        received_payloads.push_back(event.payload);
    }, error));
    EXPECT_TRUE(error.empty());

    ASSERT_TRUE(broker.publish("alerts", token, "hello-world", error));
    EXPECT_TRUE(error.empty());

    flush_event_broker_for_tests();

    std::lock_guard<std::mutex> lock(events_mutex);
    ASSERT_EQ(received_payloads.size(), 1u);
    EXPECT_EQ(received_payloads[0], "hello-world");
}

TEST_F(EventBrokerRealTest, RejectsCapabilityMismatches) {
    auto& broker = get_event_broker_interface();
    auto& policy_service = get_policy_service_interface();

    const std::string alerts_token = policy_service.capability_engine().issue_token("alerts_ext", {"event:alerts"});
    const std::string metrics_token = policy_service.capability_engine().issue_token("metrics_ext", {"event:metrics"});
    ASSERT_FALSE(alerts_token.empty());
    ASSERT_FALSE(metrics_token.empty());

    std::string error;
    EXPECT_FALSE(broker.subscribe("metrics", alerts_token, [](const RuntimeEvent&) {}, error));
    EXPECT_EQ(error, "subscription-capability-denied");

    EXPECT_FALSE(broker.publish("alerts", metrics_token, "denied", error));
    EXPECT_EQ(error, "publish-capability-denied");
}

TEST_F(EventBrokerRealTest, DeliveryPreservesPublishOrderPerTopic) {
    auto& broker = get_event_broker_interface();
    auto& policy_service = get_policy_service_interface();

    const std::string token = policy_service.capability_engine().issue_token("order_ext", {"event:ordered"});
    ASSERT_FALSE(token.empty());

    std::mutex events_mutex;
    std::vector<std::string> received_payloads;

    std::string error;
    ASSERT_TRUE(broker.subscribe("ordered", token, [&](const RuntimeEvent& event) {
        std::lock_guard<std::mutex> lock(events_mutex);
        received_payloads.push_back(event.payload);
    }, error));
    EXPECT_TRUE(error.empty());

    ASSERT_TRUE(broker.publish("ordered", token, "one", error));
    ASSERT_TRUE(broker.publish("ordered", token, "two", error));
    ASSERT_TRUE(broker.publish("ordered", token, "three", error));
    EXPECT_TRUE(error.empty());

    flush_event_broker_for_tests();

    std::lock_guard<std::mutex> lock(events_mutex);
    ASSERT_EQ(received_payloads.size(), 3u);
    EXPECT_EQ(received_payloads[0], "one");
    EXPECT_EQ(received_payloads[1], "two");
    EXPECT_EQ(received_payloads[2], "three");
}

TEST_F(EventBrokerRealTest, BoundedQueueAppliesBackpressure) {
    auto& broker = get_event_broker_interface();
    auto& policy_service = get_policy_service_interface();

    const std::string token = policy_service.capability_engine().issue_token("pressure_ext", {"event:pressure"});
    ASSERT_FALSE(token.empty());

    set_event_broker_queue_capacity_for_tests(1);

    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool release_callbacks = false;
    std::size_t callbacks_seen = 0;

    std::string error;
    ASSERT_TRUE(broker.subscribe("pressure", token, [&](const RuntimeEvent&) {
        std::unique_lock<std::mutex> lock(gate_mutex);
        ++callbacks_seen;
        gate_cv.wait(lock, [&release_callbacks]() { return release_callbacks; });
    }, error));
    EXPECT_TRUE(error.empty());

    ASSERT_TRUE(broker.publish("pressure", token, "first", error));
    ASSERT_TRUE(broker.publish("pressure", token, "second", error));
    EXPECT_FALSE(broker.publish("pressure", token, "third", error));
    EXPECT_EQ(error, "event-queue-full");

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_callbacks = true;
    }
    gate_cv.notify_all();
    flush_event_broker_for_tests();

    std::lock_guard<std::mutex> lock(gate_mutex);
    EXPECT_EQ(callbacks_seen, 2u);
}

TEST_F(EventBrokerRealTest, PerTopicMonotonicSequenceUnderConcurrentPublish) {
    auto& broker = get_event_broker_interface();
    auto& policy_service = get_policy_service_interface();

    const std::string token = policy_service.capability_engine().issue_token("concurrent_ext", {"event:burst"});
    ASSERT_FALSE(token.empty());

    constexpr int kPublisherThreads = 8;
    constexpr int kPublishesPerThread = 50;
    constexpr int kExpectedDelivered = kPublisherThreads * kPublishesPerThread;

    set_event_broker_queue_capacity_for_tests(static_cast<std::size_t>(kExpectedDelivered + 16));

    std::mutex events_mutex;
    std::vector<uint64_t> sequences;
    sequences.reserve(kExpectedDelivered);

    std::string error;
    ASSERT_TRUE(broker.subscribe("burst", token, [&](const RuntimeEvent& event) {
        std::lock_guard<std::mutex> lock(events_mutex);
        sequences.push_back(event.sequence);
    }, error));
    ASSERT_TRUE(error.empty());

    std::atomic<bool> start{false};
    std::atomic<int> publish_failures{0};
    std::vector<std::thread> publishers;
    publishers.reserve(kPublisherThreads);

    for (int t = 0; t < kPublisherThreads; ++t) {
        publishers.emplace_back([&, t]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kPublishesPerThread; ++i) {
                std::string publish_error;
                const std::string payload = std::string("p") + std::to_string(t) + "-" + std::to_string(i);
                if (!broker.publish("burst", token, payload, publish_error) || !publish_error.empty()) {
                    publish_failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& publisher : publishers) {
        publisher.join();
    }
    EXPECT_EQ(publish_failures.load(std::memory_order_relaxed), 0);

    flush_event_broker_for_tests();

    {
        std::lock_guard<std::mutex> lock(events_mutex);
        ASSERT_EQ(sequences.size(), static_cast<std::size_t>(kExpectedDelivered));
        for (std::size_t i = 1; i < sequences.size(); ++i) {
            EXPECT_GT(sequences[i], sequences[i - 1]);
        }
    }
}

TEST_F(EventBrokerRealTest, NoDuplicateDeliveryForAcceptedPublishes) {
    auto& broker = get_event_broker_interface();
    auto& policy_service = get_policy_service_interface();

    const std::string token = policy_service.capability_engine().issue_token("nodup_ext", {"event:nodup"});
    ASSERT_FALSE(token.empty());

    constexpr int kPublisherThreads = 6;
    constexpr int kPublishesPerThread = 40;
    constexpr int kExpectedDelivered = kPublisherThreads * kPublishesPerThread;

    set_event_broker_queue_capacity_for_tests(static_cast<std::size_t>(kExpectedDelivered + 16));

    std::mutex events_mutex;
    std::unordered_set<std::string> payloads;
    std::vector<std::string> duplicates;

    std::string error;
    ASSERT_TRUE(broker.subscribe("nodup", token, [&](const RuntimeEvent& event) {
        std::lock_guard<std::mutex> lock(events_mutex);
        const auto inserted = payloads.insert(event.payload);
        if (!inserted.second) {
            duplicates.push_back(event.payload);
        }
    }, error));
    ASSERT_TRUE(error.empty());

    std::atomic<bool> start{false};
    std::atomic<int> publish_failures{0};
    std::vector<std::thread> publishers;
    publishers.reserve(kPublisherThreads);

    for (int t = 0; t < kPublisherThreads; ++t) {
        publishers.emplace_back([&, t]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kPublishesPerThread; ++i) {
                std::string publish_error;
                const std::string payload = std::string("u") + std::to_string(t) + "-" + std::to_string(i);
                if (!broker.publish("nodup", token, payload, publish_error) || !publish_error.empty()) {
                    publish_failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& publisher : publishers) {
        publisher.join();
    }
    EXPECT_EQ(publish_failures.load(std::memory_order_relaxed), 0);

    flush_event_broker_for_tests();

    std::lock_guard<std::mutex> lock(events_mutex);
    EXPECT_TRUE(duplicates.empty());
    EXPECT_EQ(payloads.size(), static_cast<std::size_t>(kExpectedDelivered));
}

TEST_F(EventBrokerRealTest, TelemetryTracksAcceptedDroppedRejectedAndDelivered) {
    auto& broker = get_event_broker_interface();
    auto& policy_service = get_policy_service_interface();

    const std::string ok_token = policy_service.capability_engine().issue_token("telemetry_ok", {"event:telemetry"});
    const std::string bad_token = policy_service.capability_engine().issue_token("telemetry_bad", {"event:other"});
    ASSERT_FALSE(ok_token.empty());
    ASSERT_FALSE(bad_token.empty());

    set_event_broker_queue_capacity_for_tests(1);

    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool release_callbacks = false;

    std::string error;
    ASSERT_TRUE(broker.subscribe("telemetry", ok_token, [&](const RuntimeEvent&) {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_cv.wait(lock, [&release_callbacks]() { return release_callbacks; });
    }, error));
    ASSERT_TRUE(error.empty());

    EXPECT_FALSE(broker.subscribe("telemetry", bad_token, [](const RuntimeEvent&) {}, error));
    EXPECT_EQ(error, "subscription-capability-denied");

    ASSERT_TRUE(broker.publish("telemetry", ok_token, "a", error));
    ASSERT_TRUE(broker.publish("telemetry", ok_token, "b", error));
    EXPECT_FALSE(broker.publish("telemetry", ok_token, "c", error));
    EXPECT_EQ(error, "event-queue-full");

    EXPECT_FALSE(broker.publish("telemetry", bad_token, "d", error));
    EXPECT_EQ(error, "publish-capability-denied");

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_callbacks = true;
    }
    gate_cv.notify_all();
    flush_event_broker_for_tests();

    const EventBrokerTelemetry telemetry = get_event_broker_telemetry_snapshot_for_tests();
    EXPECT_EQ(telemetry.accepted_publishes, 2u);
    EXPECT_EQ(telemetry.delivered_callbacks, 2u);
    EXPECT_EQ(telemetry.dropped_publishes_queue_full, 1u);
    EXPECT_EQ(telemetry.capability_rejected_publishes, 1u);
    EXPECT_EQ(telemetry.capability_rejected_subscribes, 1u);
}
