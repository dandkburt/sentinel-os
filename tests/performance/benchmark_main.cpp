#include "event_broker.h"
#include "extension_registry.h"
#include "policy.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
    std::string name;
    std::size_t warmup_iterations = 0;
    std::size_t measured_iterations = 0;
    std::size_t batch_size = 0;
    uint64_t total_operations = 0;
    uint64_t total_nanoseconds = 0;
    double ops_per_second = 0.0;
    double nanoseconds_per_operation = 0.0;
};

std::string escape_json(const std::string& input) {
    std::ostringstream out;
    for (char c : input) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

std::string to_json(const std::vector<BenchmarkResult>& results) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"results\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << "    {\n";
        out << "      \"name\": \"" << escape_json(r.name) << "\",\n";
        out << "      \"warmup_iterations\": " << r.warmup_iterations << ",\n";
        out << "      \"measured_iterations\": " << r.measured_iterations << ",\n";
        out << "      \"batch_size\": " << r.batch_size << ",\n";
        out << "      \"total_operations\": " << r.total_operations << ",\n";
        out << "      \"total_nanoseconds\": " << r.total_nanoseconds << ",\n";
        out << "      \"ops_per_second\": " << std::fixed << std::setprecision(2) << r.ops_per_second << ",\n";
        out << "      \"nanoseconds_per_operation\": " << std::fixed << std::setprecision(2)
            << r.nanoseconds_per_operation << "\n";
        out << "    }";
        if (i + 1 < results.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

BenchmarkResult run_benchmark(const std::string& name,
                              std::size_t warmup_iterations,
                              std::size_t measured_iterations,
                              std::size_t batch_size,
                              const std::function<void()>& operation) {
    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        operation();
    }

    const auto start = Clock::now();
    for (std::size_t i = 0; i < measured_iterations; ++i) {
        operation();
    }
    const auto end = Clock::now();

    const uint64_t total_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    const uint64_t total_ops = static_cast<uint64_t>(measured_iterations * batch_size);
    const double seconds = static_cast<double>(total_ns) / 1'000'000'000.0;
    const double ops_per_second = seconds > 0.0 ? static_cast<double>(total_ops) / seconds : 0.0;
    const double ns_per_op = total_ops > 0 ? static_cast<double>(total_ns) / static_cast<double>(total_ops) : 0.0;

    return BenchmarkResult{name, warmup_iterations, measured_iterations, batch_size, total_ops, total_ns,
                           ops_per_second, ns_per_op};
}

bool deterministic_random_bytes(std::vector<uint8_t>& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>((i * 17u + 3u) & 0xffu);
    }
    return true;
}

std::unordered_map<std::string, std::string> g_virtual_manifest_texts;
std::vector<std::string> g_virtual_manifest_paths;

bool benchmark_manifest_loader(const std::string& manifest_path,
                               std::string& manifest_text,
                               std::string& error) {
    auto it = g_virtual_manifest_texts.find(manifest_path);
    if (it == g_virtual_manifest_texts.end()) {
        error = "source-not-found";
        return false;
    }
    manifest_text = it->second;
    error.clear();
    return true;
}

bool benchmark_manifest_enumerator(const std::string& directory_path,
                                   std::vector<std::string>& manifest_paths,
                                   std::string& error) {
    (void)directory_path;
    manifest_paths = g_virtual_manifest_paths;
    error.clear();
    return true;
}

std::string build_manifest(const std::string& id,
                           const std::string& deps) {
    return "id = " + id + "\n" +
           "version = 1.0.0\n" +
           "name = Benchmark " + id + "\n" +
           "entry_point = ./" + id + ".js\n" +
           "permissions = [event:bench]\n" +
           "dependencies = " + deps + "\n";
}

BenchmarkResult benchmark_event_publish_delivery() {
    using namespace sentinel::core::event;
    using namespace sentinel::services::policy;

    constexpr std::size_t kWarmupIterations = 2;
    constexpr std::size_t kMeasuredIterations = 8;
    constexpr std::size_t kEventsPerIteration = 2000;
    constexpr auto kWaitTimeout = std::chrono::seconds(10);

    initialize_policy_service();
    initialize_event_broker();
    reset_event_broker_for_tests();
    reset_event_broker_telemetry_for_tests();
    set_event_broker_queue_capacity_for_tests(kEventsPerIteration * 2);

    auto& policy = get_policy_service_interface();
    auto& broker = get_event_broker_interface();
    const std::string token = policy.capability_engine().issue_token("bench_event", {"event:bench"});
    if (token.empty()) {
        throw std::runtime_error("event benchmark token issue failed");
    }

    std::mutex delivery_mutex;
    std::condition_variable delivery_cv;
    uint64_t delivered = 0;
    std::string subscribe_error;
    if (!broker.subscribe("bench", token, [&](const RuntimeEvent&) {
            {
                std::lock_guard<std::mutex> lock(delivery_mutex);
                ++delivered;
            }
            delivery_cv.notify_one();
        }, subscribe_error)) {
        throw std::runtime_error("event benchmark subscribe failed: " + subscribe_error);
    }

    auto operation = [&]() {
        uint64_t target = 0;
        {
            std::lock_guard<std::mutex> lock(delivery_mutex);
            target = delivered + kEventsPerIteration;
        }

        for (std::size_t i = 0; i < kEventsPerIteration; ++i) {
            std::string publish_error;
            if (!broker.publish("bench", token, "payload", publish_error)) {
                throw std::runtime_error("event benchmark publish failed: " + publish_error);
            }
        }

        std::unique_lock<std::mutex> lock(delivery_mutex);
        const bool complete = delivery_cv.wait_for(lock, kWaitTimeout, [&]() {
            return delivered >= target;
        });
        if (!complete) {
            throw std::runtime_error("event benchmark delivery timeout");
        }
    };

    BenchmarkResult result = run_benchmark(
        "event.publish_delivery.throughput",
        kWarmupIterations,
        kMeasuredIterations,
        kEventsPerIteration,
        operation);

    flush_event_broker_for_tests();
    return result;
}

BenchmarkResult benchmark_extension_dependency_resolution() {
    using namespace sentinel::core;

    constexpr std::size_t kWarmupIterations = 3;
    constexpr std::size_t kMeasuredIterations = 20;
    constexpr std::size_t kNodeCount = 48;

    initialize_extension_registry();
    reset_extension_registry_for_tests();
    reset_extension_lifecycle_fail_step_for_tests();

    g_virtual_manifest_paths.clear();
    g_virtual_manifest_texts.clear();

    for (std::size_t i = 0; i < kNodeCount; ++i) {
        const std::string id = "bench.ext." + std::to_string(i);
        const std::string path = "mock://bench/" + id + ".manifest";
        g_virtual_manifest_paths.push_back(path);

        std::string deps = "[]";
        if (i > 2) {
            deps = "[bench.ext." + std::to_string(i - 1) + ",bench.ext." + std::to_string(i - 3) + "]";
        } else if (i > 0) {
            deps = "[bench.ext." + std::to_string(i - 1) + "]";
        }

        g_virtual_manifest_texts[path] = build_manifest(id, deps);
    }

    set_extension_manifest_text_loader_for_tests(benchmark_manifest_loader);
    set_extension_manifest_path_enumerator_for_tests(benchmark_manifest_enumerator);

    auto& registry = get_extension_registry_interface();
    std::vector<std::string> non_fatal_errors;
    std::string error;
    if (!registry.discover_extensions_in_directory("mock://bench", non_fatal_errors, error)) {
        throw std::runtime_error("extension benchmark discovery failed: " + error);
    }
    if (!non_fatal_errors.empty()) {
        throw std::runtime_error("extension benchmark non-fatal errors present");
    }

    std::vector<std::string> ordered;
    if (!registry.resolve_extension_dependencies(ordered, error) || ordered.size() != kNodeCount) {
        throw std::runtime_error("extension benchmark dependency baseline failed: " + error);
    }

    auto operation = [&]() {
        std::vector<std::string> order;
        std::string resolve_error;
        if (!registry.resolve_extension_dependencies(order, resolve_error)) {
            throw std::runtime_error("extension benchmark resolve failed: " + resolve_error);
        }
        if (order.size() != kNodeCount) {
            throw std::runtime_error("extension benchmark resolve size mismatch");
        }
    };

    BenchmarkResult result = run_benchmark(
        "extension.dependency_resolution.medium_graph",
        kWarmupIterations,
        kMeasuredIterations,
        kNodeCount,
        operation);

    reset_extension_manifest_text_loader_for_tests();
    reset_extension_manifest_path_enumerator_for_tests();
    reset_extension_registry_for_tests();
    return result;
}

BenchmarkResult benchmark_policy_token_verify() {
    using namespace sentinel::services::policy;

    constexpr std::size_t kWarmupIterations = 4;
    constexpr std::size_t kMeasuredIterations = 32;
    constexpr std::size_t kVerifiesPerIteration = 2000;

    initialize_policy_service();
    set_policy_random_bytes_generator_for_tests(deterministic_random_bytes);

    auto& capability_engine = get_policy_service_interface().capability_engine();
    const std::string token = capability_engine.issue_token(
        "bench_policy",
        {"file:read", "event:bench", "extension:*"},
        0);
    if (token.empty()) {
        throw std::runtime_error("policy benchmark token issue failed");
    }

    auto operation = [&]() {
        for (std::size_t i = 0; i < kVerifiesPerIteration; ++i) {
            const auto result = capability_engine.verify_token(token, "event:bench");
            if (!result.is_valid) {
                throw std::runtime_error("policy benchmark verify failed");
            }
        }
    };

    BenchmarkResult result = run_benchmark(
        "policy.token_verify.throughput",
        kWarmupIterations,
        kMeasuredIterations,
        kVerifiesPerIteration,
        operation);

    reset_policy_random_bytes_generator_for_tests();
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    std::string json_out_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json-out" && i + 1 < argc) {
            json_out_path = argv[++i];
        }
    }

    try {
        std::vector<BenchmarkResult> results;
        results.push_back(benchmark_event_publish_delivery());
        results.push_back(benchmark_extension_dependency_resolution());
        results.push_back(benchmark_policy_token_verify());

        const std::string json = to_json(results);
        if (!json_out_path.empty()) {
            std::ofstream out(json_out_path, std::ios::trunc);
            if (!out.is_open()) {
                std::cerr << "Unable to open benchmark output path: " << json_out_path << std::endl;
                return 1;
            }
            out << json;
        }

        std::cout << json;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Benchmark failure: " << ex.what() << std::endl;
        return 1;
    }
}