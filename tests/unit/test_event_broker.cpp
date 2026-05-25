#include <gtest/gtest.h>

#include "event_broker.h"
#include "policy.h"

#include <mutex>
#include <vector>

using namespace sentinel::core::event;
using namespace sentinel::services::policy;

class EventBrokerRealTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize_policy_service();
        initialize_event_broker();
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
