#pragma once

#include <cstdint>
#include <functional>
#include <cstddef>
#include <string>

namespace sentinel::core::event {

enum class EventDeliveryGuarantee {
    AtMostOnce = 0,
};

struct RuntimeEvent {
    uint64_t sequence = 0;
    std::string topic;
    std::string payload;
    std::string publisher_id;
    std::string granted_capability;
};

using RuntimeEventCallback = std::function<void(const RuntimeEvent&)>;

class IEventBroker {
public:
    virtual ~IEventBroker() = default;

    virtual EventDeliveryGuarantee delivery_guarantee() const = 0;

    virtual bool subscribe(const std::string& topic,
                           const std::string& capability_token,
                           RuntimeEventCallback callback,
                           std::string& error) = 0;

    virtual bool publish(const std::string& topic,
                         const std::string& capability_token,
                         const std::string& payload,
                         std::string& error) = 0;
};

IEventBroker& get_event_broker_interface();
void initialize_event_broker();

/// @brief Test hook: clear broker state and restore defaults.
void reset_event_broker_for_tests();

/// @brief Test hook: set the per-shard queue capacity.
void set_event_broker_queue_capacity_for_tests(std::size_t capacity);

/// @brief Test hook: block until queued events are fully delivered.
void flush_event_broker_for_tests();

}  // namespace sentinel::core::event