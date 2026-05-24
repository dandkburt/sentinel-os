#include "runtime_c_api.h"

#include "bootstrap.h"

#include <cstring>
#include <string>

int sentinel_runtime_initialize(void)
{
    sentinel::core::initialize_runtime();
    auto& runtime = sentinel::core::get_runtime_interface();
    const auto result = runtime.bootstrap().initialize();
    if (result.is_success()) {
        return 0;
    }
    return static_cast<int>(result.status);
}

int sentinel_runtime_shutdown(void)
{
    auto& runtime = sentinel::core::get_runtime_interface();
    const auto result = runtime.bootstrap().shutdown();
    if (result.is_success()) {
        return 0;
    }
    return static_cast<int>(result.status);
}

int sentinel_runtime_is_ready(void)
{
    auto& runtime = sentinel::core::get_runtime_interface();
    return runtime.bootstrap().is_ready() ? 1 : 0;
}

int sentinel_runtime_get_version(char* buffer, size_t buffer_size)
{
    auto& runtime = sentinel::core::get_runtime_interface();
    const std::string version = runtime.bootstrap().get_version();

    if (buffer != nullptr && buffer_size > 0) {
        const size_t copy_len = (version.size() < (buffer_size - 1)) ? version.size() : (buffer_size - 1);
        std::memcpy(buffer, version.c_str(), copy_len);
        buffer[copy_len] = '\0';
    }

    return static_cast<int>(version.size());
}
