#include "event_broker.h"

#include "../../services/policy/policy.h"

#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

namespace sentinel::core::event {

namespace {
std::string required_topic_capability(const std::string& topic) {
    return std::string("event:") + topic;
}

bool is_valid_topic(const std::string& topic) {
    return !topic.empty();
}

struct SubscriptionRecord {
    std::string topic;
    std::string capability_token;
    std::string subscriber_id;
    std::string granted_capability;
    RuntimeEventCallback callback;
};

class EventBrokerImpl : public IEventBroker {
public:
    bool subscribe(const std::string& topic,
                   const std::string& capability_token,
                   RuntimeEventCallback callback,
                   std::string& error) override {
        if (!is_valid_topic(topic)) {
            error = "invalid-topic";
            return false;
        }
        if (!callback) {
            error = "invalid-callback";
            return false;
        }

        auto& policy_service = sentinel::services::policy::get_policy_service_interface();
        const auto verification = policy_service.capability_engine().verify_token(
            capability_token,
            required_topic_capability(topic));
        if (!verification.is_valid) {
            error = "subscription-capability-denied";
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.push_back(SubscriptionRecord{
            topic,
            capability_token,
            verification.extension_id,
            verification.capability_scope,
            std::move(callback)
        });
        error.clear();
        return true;
    }

    bool publish(const std::string& topic,
                 const std::string& capability_token,
                 const std::string& payload,
                 std::string& error) override {
        if (!is_valid_topic(topic)) {
            error = "invalid-topic";
            return false;
        }

        auto& policy_service = sentinel::services::policy::get_policy_service_interface();
        const auto verification = policy_service.capability_engine().verify_token(
            capability_token,
            required_topic_capability(topic));
        if (!verification.is_valid) {
            error = "publish-capability-denied";
            return false;
        }

        std::vector<SubscriptionRecord> matching_subscriptions;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& subscription : subscriptions_) {
                if (subscription.topic == topic) {
                    matching_subscriptions.push_back(subscription);
                }
            }
        }

        RuntimeEvent event{topic, payload, verification.extension_id, verification.capability_scope};
        for (const auto& subscription : matching_subscriptions) {
            const auto subscription_verification = policy_service.capability_engine().verify_token(
                subscription.capability_token,
                required_topic_capability(subscription.topic));
            if (!subscription_verification.is_valid) {
                continue;
            }

            try {
                subscription.callback(event);
            } catch (const std::exception& e) {
                std::cerr << "Event broker callback failed: " << e.what() << std::endl;
            }
        }

        error.clear();
        return true;
    }

private:
    mutable std::mutex mutex_;
    std::vector<SubscriptionRecord> subscriptions_;
};

EventBrokerImpl*& broker_instance() {
    static EventBrokerImpl* instance = nullptr;
    return instance;
}

EventBrokerImpl& ensure_broker() {
    auto*& instance = broker_instance();
    if (instance == nullptr) {
        instance = new EventBrokerImpl();
    }
    return *instance;
}
}  // namespace

IEventBroker& get_event_broker_interface() {
    return ensure_broker();
}

void initialize_event_broker() {
    (void)ensure_broker();
}

}  // namespace sentinel::core::event