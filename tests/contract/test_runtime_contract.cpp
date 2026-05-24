#include <gtest/gtest.h>

#include "runtime_c_api.h"

#include <array>
#include <cstring>

TEST(RuntimeContract, InitializeAndShutdown)
{
    const int init_status = sentinel_runtime_initialize();
    EXPECT_EQ(init_status, 0);

    const int shutdown_status = sentinel_runtime_shutdown();
    EXPECT_EQ(shutdown_status, 0);
}

TEST(RuntimeContract, ReadyStateTransitions)
{
    const int pre_ready = sentinel_runtime_is_ready();
    EXPECT_EQ(pre_ready, 0);

    EXPECT_EQ(sentinel_runtime_initialize(), 0);
    const int post_init_ready = sentinel_runtime_is_ready();
    EXPECT_EQ(post_init_ready, 1);

    EXPECT_EQ(sentinel_runtime_shutdown(), 0);
    const int post_shutdown_ready = sentinel_runtime_is_ready();
    EXPECT_EQ(post_shutdown_ready, 0);
}

TEST(RuntimeContract, GetVersionString)
{
    EXPECT_EQ(sentinel_runtime_initialize(), 0);

    std::array<char, 32> buffer{};
    const int required_chars = sentinel_runtime_get_version(buffer.data(), buffer.size());

    EXPECT_GT(required_chars, 0);
    EXPECT_STREQ(buffer.data(), "0.1.0");
    EXPECT_EQ(required_chars, static_cast<int>(std::strlen("0.1.0")));

    EXPECT_EQ(sentinel_runtime_shutdown(), 0);
}
