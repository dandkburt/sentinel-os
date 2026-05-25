#include "event_broker.h"

#include "../../services/policy/policy.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
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

struct DeliveryTarget {
    std::string topic;
    std::string capability_token;
    RuntimeEventCallback callback;
};

struct PendingEvent {
    uint64_t sequence = 0;
    RuntimeEvent event;
    std::vector<DeliveryTarget> targets;
};

struct BrokerShard {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<PendingEvent> queue;
    std::unordered_map<std::string, uint64_t> next_topic_sequence;
    bool processing = false;
};

class EventBrokerImpl : public IEventBroker {
public:
    EventBrokerImpl() {
        start_workers();
    }

    ~EventBrokerImpl() override {
        stop_workers();
    }

    EventDeliveryGuarantee delivery_guarantee() const override {
        return EventDeliveryGuarantee::AtMostOnce;
    }

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
            telemetry_capability_rejected_subscribes_.fetch_add(1, std::memory_order_relaxed);
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
            telemetry_capability_rejected_publishes_.fetch_add(1, std::memory_order_relaxed);
            error = "publish-capability-denied";
            return false;
        }

        std::vector<DeliveryTarget> matching_targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& subscription : subscriptions_) {
                if (subscription.topic == topic) {
                    matching_targets.push_back(DeliveryTarget{
                        subscription.topic,
                        subscription.capability_token,
                        subscription.callback
                    });
                }
            }
        }

        auto& shard = shards_[select_shard(topic)];
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            if (shard.queue.size() >= queue_capacity_.load(std::memory_order_relaxed)) {
                telemetry_dropped_publishes_queue_full_.fetch_add(1, std::memory_order_relaxed);
                error = "event-queue-full";
                return false;
            }

            PendingEvent pending_event;
            const uint64_t sequence = shard.next_topic_sequence[topic]++;
            pending_event.sequence = sequence;
            pending_event.event = RuntimeEvent{
                pending_event.sequence,
                topic,
                payload,
                verification.extension_id,
                verification.capability_scope
            };
            pending_event.targets = std::move(matching_targets);

            shard.queue.push_back(std::move(pending_event));
        }

        shard.cv.notify_one();
        telemetry_accepted_publishes_.fetch_add(1, std::memory_order_relaxed);
        error.clear();
        return true;
    }

    void reset_for_tests() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscriptions_.clear();
            queue_capacity_.store(kDefaultQueueCapacity, std::memory_order_relaxed);
        }

        for (auto& shard : shards_) {
            std::lock_guard<std::mutex> lock(shard.mutex);
            shard.queue.clear();
            shard.next_topic_sequence.clear();
            shard.processing = false;
            shard.cv.notify_all();
        }
    }

    void set_queue_capacity_for_tests(std::size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_capacity_.store(capacity == 0 ? 1 : capacity, std::memory_order_relaxed);
    }

    void flush_for_tests() {
        for (;;) {
            bool empty = true;
            for (auto& shard : shards_) {
                std::lock_guard<std::mutex> lock(shard.mutex);
                if (shard.processing || !shard.queue.empty()) {
                    empty = false;
                    break;
                }
            }

            if (empty) {
                return;
            }

            std::this_thread::yield();
        }
    }

    EventBrokerTelemetry telemetry_snapshot_for_tests() const {
        return EventBrokerTelemetry{
            telemetry_accepted_publishes_.load(std::memory_order_relaxed),
            telemetry_delivered_callbacks_.load(std::memory_order_relaxed),
            telemetry_dropped_publishes_queue_full_.load(std::memory_order_relaxed),
            telemetry_capability_rejected_publishes_.load(std::memory_order_relaxed),
            telemetry_capability_rejected_subscribes_.load(std::memory_order_relaxed)
        };
    }

    void reset_telemetry_for_tests() {
        telemetry_accepted_publishes_.store(0, std::memory_order_relaxed);
        telemetry_delivered_callbacks_.store(0, std::memory_order_relaxed);
        telemetry_dropped_publishes_queue_full_.store(0, std::memory_order_relaxed);
        telemetry_capability_rejected_publishes_.store(0, std::memory_order_relaxed);
        telemetry_capability_rejected_subscribes_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kDefaultWorkerCount = 4;
    static constexpr std::size_t kDefaultQueueCapacity = 32;

    void start_workers() {
        workers_.reserve(kDefaultWorkerCount);
        for (std::size_t shard_index = 0; shard_index < kDefaultWorkerCount; ++shard_index) {
            workers_.emplace_back([this, shard_index]() {
                worker_loop(shard_index);
            });
        }
    }

    void stop_workers() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_.store(true, std::memory_order_release);
        }

        for (auto& shard : shards_) {
            shard.cv.notify_all();
        }

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    std::size_t select_shard(const std::string& topic) const {
        return std::hash<std::string>{}(topic) % shards_.size();
    }

    void worker_loop(std::size_t shard_index) {
        auto& shard = shards_[shard_index];
        for (;;) {
            PendingEvent next_event;
            {
                std::unique_lock<std::mutex> lock(shard.mutex);
                shard.cv.wait(lock, [this, &shard]() {
                    return stopping_.load(std::memory_order_acquire) || !shard.queue.empty();
                });

                if (stopping_.load(std::memory_order_acquire) && shard.queue.empty()) {
                    return;
                }

                next_event = std::move(shard.queue.front());
                shard.queue.pop_front();
                shard.processing = true;
            }

            deliver_event(next_event);

            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                shard.processing = false;
            }
            shard.cv.notify_all();
        }
    }

    void deliver_event(const PendingEvent& pending_event) {
        auto& policy_service = sentinel::services::policy::get_policy_service_interface();
        for (const auto& target : pending_event.targets) {
            const auto subscription_verification = policy_service.capability_engine().verify_token(
                target.capability_token,
                required_topic_capability(target.topic));
            if (!subscription_verification.is_valid) {
                continue;
            }

            try {
                telemetry_delivered_callbacks_.fetch_add(1, std::memory_order_relaxed);
                target.callback(pending_event.event);
            } catch (const std::exception& e) {
                std::cerr << "Event broker callback failed: " << e.what() << std::endl;
            }
        }
    }

    mutable std::mutex mutex_;
    std::vector<SubscriptionRecord> subscriptions_;
    std::array<BrokerShard, kDefaultWorkerCount> shards_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> queue_capacity_{kDefaultQueueCapacity};
    std::atomic<uint64_t> telemetry_accepted_publishes_{0};
    std::atomic<uint64_t> telemetry_delivered_callbacks_{0};
    std::atomic<uint64_t> telemetry_dropped_publishes_queue_full_{0};
    std::atomic<uint64_t> telemetry_capability_rejected_publishes_{0};
    std::atomic<uint64_t> telemetry_capability_rejected_subscribes_{0};
};

EventBrokerImpl& ensure_broker() {
    static EventBrokerImpl instance;
    return instance;
}
}  // namespace

IEventBroker& get_event_broker_interface() {
    return ensure_broker();
}

void initialize_event_broker() {
    (void)ensure_broker();
}

void reset_event_broker_for_tests() {
    ensure_broker().reset_for_tests();
}

void set_event_broker_queue_capacity_for_tests(std::size_t capacity) {
    ensure_broker().set_queue_capacity_for_tests(capacity);
}

void flush_event_broker_for_tests() {
    ensure_broker().flush_for_tests();
}

EventBrokerTelemetry get_event_broker_telemetry_snapshot_for_tests() {
    return ensure_broker().telemetry_snapshot_for_tests();
}

void reset_event_broker_telemetry_for_tests() {
    ensure_broker().reset_telemetry_for_tests();
}

}  // namespace sentinel::core::event