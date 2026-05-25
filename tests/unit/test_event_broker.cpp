#include <gtest/gtest.h>

#include "event_broker.h"
#include "policy.h"

#include <condition_variable>
#include <mutex>
#include <vector>

using namespace sentinel::core::event;
using namespace sentinel::services::policy;

class EventBrokerRealTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize_policy_service();
        initialize_event_broker();
        reset_event_broker_for_tests();
        set_event_broker_queue_capacity_for_tests(32);
    }

    void TearDown() override {
        flush_event_broker_for_tests();
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
