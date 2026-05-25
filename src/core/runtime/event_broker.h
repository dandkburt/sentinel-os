#pragma once

#include <functional>
#include <string>

namespace sentinel::core::event {

struct RuntimeEvent {
    std::string topic;
    std::string payload;
    std::string publisher_id;
    std::string granted_capability;
};

using RuntimeEventCallback = std::function<void(const RuntimeEvent&)>;

class IEventBroker {
public:
    virtual ~IEventBroker() = default;

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

}  // namespace sentinel::core::event