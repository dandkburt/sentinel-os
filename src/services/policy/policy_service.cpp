#include "policy.h"
#include <unordered_map>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <set>
#include <vector>
#include <sstream>
#include <cctype>
#include <mutex>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace sentinel::services::policy {

namespace {
constexpr const char* kTokenVersion = "v1";
constexpr const char* kDefaultSigningKey = "sentinel-policy-dev-key";
constexpr const char* kDefaultSigningKeyId = "k1";
constexpr const char* kDefaultPreviousSigningKeyId = "k0";
constexpr const char* kDefaultPolicySecretFilePath = "C:\\ProgramData\\SentinelOS\\policy_secrets.dpapi";
constexpr size_t kMinimumSigningKeyLength = 16;
constexpr size_t kMaximumSigningKeyIdLength = 16;
std::mutex g_signing_key_mutex;
std::string g_signing_key = kDefaultSigningKey;
std::string g_previous_signing_key;
std::string g_signing_key_id = kDefaultSigningKeyId;
std::string g_previous_signing_key_id = kDefaultPreviousSigningKeyId;
std::once_flag g_policy_key_env_init_once;
std::shared_ptr<IPolicySigningSecretProvider> g_policy_signing_secret_provider_for_tests;

bool capability_matches(const std::string& granted_capability, const std::string& required_capability) {
    if (granted_capability == "*") {
        return true;
    }

    if (granted_capability == required_capability) {
        return true;
    }

    const auto wildcard_pos = granted_capability.find(':');
    if (wildcard_pos != std::string::npos &&
        wildcard_pos + 1 < granted_capability.size() &&
        granted_capability[wildcard_pos + 1] == '*') {
        const auto granted_prefix = granted_capability.substr(0, wildcard_pos);
        const auto required_sep = required_capability.find(':');
        if (required_sep != std::string::npos) {
            return required_capability.substr(0, required_sep) == granted_prefix;
        }
    }

    return false;
}

bool extract_json_string_field(const std::string& json, const std::string& key, std::string& out) {
    const std::string quoted_key = "\"" + key + "\"";
    const auto key_pos = json.find(quoted_key);
    if (key_pos == std::string::npos) {
        return false;
    }

    const auto colon_pos = json.find(':', key_pos + quoted_key.size());
    if (colon_pos == std::string::npos) {
        return false;
    }

    const auto value_start = json.find('"', colon_pos + 1);
    if (value_start == std::string::npos) {
        return false;
    }

    const auto value_end = json.find('"', value_start + 1);
    if (value_end == std::string::npos || value_end <= value_start + 1) {
        return false;
    }

    out = json.substr(value_start + 1, value_end - value_start - 1);
    return true;
}

bool action_matches(const std::string& action_pattern, const std::string& action) {
    if (action_pattern == "*") {
        return true;
    }

    if (action_pattern == action) {
        return true;
    }

    const auto wildcard_pos = action_pattern.find(':');
    if (wildcard_pos != std::string::npos &&
        wildcard_pos + 1 < action_pattern.size() &&
        action_pattern[wildcard_pos + 1] == '*') {
        const auto pattern_prefix = action_pattern.substr(0, wildcard_pos);
        const auto action_sep = action.find(':');
        if (action_sep != std::string::npos) {
            return action.substr(0, action_sep) == pattern_prefix;
        }
    }

    return false;
}

bool parse_effect(const std::string& effect_raw, PolicyDecision& effect_out) {
    if (effect_raw == "allow") {
        effect_out = PolicyDecision::Allow;
        return true;
    }
    if (effect_raw == "deny") {
        effect_out = PolicyDecision::Deny;
        return true;
    }
    if (effect_raw == "defer") {
        effect_out = PolicyDecision::Defer;
        return true;
    }
    return false;
}

bool is_safe_token_segment(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    for (char c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool is_valid_signing_key_id(const std::string& key_id) {
    if (!is_safe_token_segment(key_id) || key_id.size() < 2 || key_id.size() > kMaximumSigningKeyIdLength) {
        return false;
    }

    if (key_id[0] != 'k') {
        return false;
    }

    for (size_t i = 1; i < key_id.size(); ++i) {
        const unsigned char uc = static_cast<unsigned char>(key_id[i]);
        if (!std::isdigit(uc)) {
            return false;
        }
    }

    return true;
}

std::string current_signing_key() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    return g_signing_key;
}

std::string current_previous_signing_key() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    return g_previous_signing_key;
}

std::string current_signing_key_id() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    return g_signing_key_id;
}

std::string current_previous_signing_key_id() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    return g_previous_signing_key_id;
}

bool is_valid_signing_key_material(const std::string& key) {
    if (key.size() < kMinimumSigningKeyLength) {
        return false;
    }

    for (char c : key) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 33 || uc > 126) {
            return false;
        }
    }

    return true;
}

std::string read_env_var(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return "";
    }
    std::string result(value);
    free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? "" : std::string(value);
#endif
}

bool parse_boolean_like(const std::string& value) {
    return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
}

void trim_in_place(std::string& value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
}

bool parse_signing_secret_payload(const std::string& payload, PolicySigningSecrets& out, std::string& error) {
    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        trim_in_place(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, sep);
        std::string value = line.substr(sep + 1);
        trim_in_place(key);
        trim_in_place(value);

        if (key == "SIGNING_KEY") {
            out.has_signing_key = true;
            out.signing_key = value;
        } else if (key == "PREVIOUS_SIGNING_KEY") {
            out.has_previous_signing_key = true;
            out.previous_signing_key = value;
        } else if (key == "SIGNING_KEY_ID") {
            out.has_signing_key_id = true;
            out.signing_key_id = value;
        } else if (key == "PREVIOUS_SIGNING_KEY_ID") {
            out.has_previous_signing_key_id = true;
            out.previous_signing_key_id = value;
        }
    }

    if (!out.has_signing_key) {
        error = "secret payload missing SIGNING_KEY";
        return false;
    }

    return true;
}

#ifdef _WIN32
class DpapiFilePolicySigningSecretProvider final : public IPolicySigningSecretProvider {
public:
    const char* name() const override {
        return "windows-dpapi-file";
    }

    bool load_signing_secrets(PolicySigningSecrets& out, std::string& error) override {
        const std::string configured_path = read_env_var("SENTINEL_POLICY_SECRET_FILE");
        const std::filesystem::path file_path = configured_path.empty()
            ? std::filesystem::path(kDefaultPolicySecretFilePath)
            : std::filesystem::path(configured_path);

        if (!std::filesystem::exists(file_path)) {
            error = "secret file not found";
            return false;
        }

        std::ifstream input(file_path, std::ios::binary);
        if (!input) {
            error = "unable to open secret file";
            return false;
        }

        std::vector<char> encrypted((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (encrypted.empty()) {
            error = "secret file is empty";
            return false;
        }

        DATA_BLOB encrypted_blob{};
        encrypted_blob.pbData = reinterpret_cast<BYTE*>(encrypted.data());
        encrypted_blob.cbData = static_cast<DWORD>(encrypted.size());

        DATA_BLOB plain_blob{};
        if (!CryptUnprotectData(&encrypted_blob, nullptr, nullptr, nullptr, nullptr, 0, &plain_blob)) {
            error = "DPAPI decrypt failed";
            return false;
        }

        std::string plain_payload(reinterpret_cast<char*>(plain_blob.pbData), plain_blob.cbData);
        LocalFree(plain_blob.pbData);

        return parse_signing_secret_payload(plain_payload, out, error);
    }
};
#endif

std::shared_ptr<IPolicySigningSecretProvider> default_policy_signing_secret_provider() {
#ifdef _WIN32
    static std::shared_ptr<IPolicySigningSecretProvider> provider = std::make_shared<DpapiFilePolicySigningSecretProvider>();
    return provider;
#else
    return nullptr;
#endif
}

std::shared_ptr<IPolicySigningSecretProvider> selected_policy_signing_secret_provider() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    if (g_policy_signing_secret_provider_for_tests) {
        return g_policy_signing_secret_provider_for_tests;
    }
    return default_policy_signing_secret_provider();
}

bool apply_signing_keys_from_provider(IPolicySigningSecretProvider& provider) {
    PolicySigningSecrets secrets;
    std::string error;
    if (!provider.load_signing_secrets(secrets, error)) {
        if (!error.empty()) {
            std::cerr << "Policy signing secret provider '" << provider.name() << "' unavailable: " << error << std::endl;
        }
        return false;
    }

    if (!secrets.has_signing_key || !is_valid_signing_key_material(secrets.signing_key)) {
        std::cerr << "Policy signing secret provider '" << provider.name() << "' returned invalid SIGNING_KEY." << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_signing_key = secrets.signing_key;

    if (secrets.has_previous_signing_key) {
        if (secrets.previous_signing_key.empty()) {
            g_previous_signing_key.clear();
        } else if (is_valid_signing_key_material(secrets.previous_signing_key)) {
            g_previous_signing_key = secrets.previous_signing_key;
        } else {
            std::cerr << "Policy signing secret provider '" << provider.name() << "' returned invalid PREVIOUS_SIGNING_KEY; clearing previous key." << std::endl;
            g_previous_signing_key.clear();
        }
    } else {
        g_previous_signing_key.clear();
    }

    if (secrets.has_signing_key_id) {
        if (is_valid_signing_key_id(secrets.signing_key_id)) {
            g_signing_key_id = secrets.signing_key_id;
        } else {
            std::cerr << "Policy signing secret provider '" << provider.name() << "' returned invalid SIGNING_KEY_ID; keeping existing id." << std::endl;
        }
    }

    if (secrets.has_previous_signing_key_id) {
        if (is_valid_signing_key_id(secrets.previous_signing_key_id)) {
            g_previous_signing_key_id = secrets.previous_signing_key_id;
        } else {
            std::cerr << "Policy signing secret provider '" << provider.name() << "' returned invalid PREVIOUS_SIGNING_KEY_ID; keeping existing id." << std::endl;
        }
    } else {
        g_previous_signing_key_id = kDefaultPreviousSigningKeyId;
    }

    return true;
}

void apply_signing_keys_from_environment() {
    const std::string primary_env = read_env_var("SENTINEL_POLICY_SIGNING_KEY");
    const std::string previous_env = read_env_var("SENTINEL_POLICY_PREVIOUS_SIGNING_KEY");
    const std::string primary_key_id_env = read_env_var("SENTINEL_POLICY_SIGNING_KEY_ID");
    const std::string previous_key_id_env = read_env_var("SENTINEL_POLICY_PREVIOUS_SIGNING_KEY_ID");

    std::lock_guard<std::mutex> lock(g_signing_key_mutex);

    if (!primary_env.empty()) {
        const std::string primary_candidate(primary_env);
        if (is_valid_signing_key_material(primary_candidate)) {
            g_signing_key = primary_candidate;
        } else {
            std::cerr << "Policy signing key from environment failed validation; keeping existing key." << std::endl;
        }
    }

    if (!previous_env.empty()) {
        const std::string previous_candidate(previous_env);
        if (is_valid_signing_key_material(previous_candidate)) {
            g_previous_signing_key = previous_candidate;
        } else {
            std::cerr << "Policy previous signing key from environment failed validation; disabling previous key." << std::endl;
            g_previous_signing_key.clear();
        }
    }

    if (!primary_key_id_env.empty()) {
        if (is_valid_signing_key_id(primary_key_id_env)) {
            g_signing_key_id = primary_key_id_env;
        } else {
            std::cerr << "Policy signing key id from environment failed validation; keeping existing id." << std::endl;
        }
    }

    if (!previous_key_id_env.empty()) {
        if (is_valid_signing_key_id(previous_key_id_env)) {
            g_previous_signing_key_id = previous_key_id_env;
        } else {
            std::cerr << "Policy previous signing key id from environment failed validation; keeping existing id." << std::endl;
        }
    }
}

void apply_signing_keys_from_config_sources() {
    if (parse_boolean_like(read_env_var("SENTINEL_POLICY_ALLOW_ENV_KEYS_ONLY"))) {
        apply_signing_keys_from_environment();
        return;
    }

    auto provider = selected_policy_signing_secret_provider();
    const bool provider_applied = provider != nullptr && apply_signing_keys_from_provider(*provider);
    if (!provider_applied) {
        apply_signing_keys_from_environment();
    }
}

void ensure_policy_signing_keys_initialized() {
    std::call_once(g_policy_key_env_init_once, []() {
        apply_signing_keys_from_config_sources();
    });
}

std::string build_unsigned_token(const std::string& version,
                                 const std::string& extension_id,
                                 const std::string& token_id) {
    return version + "." + extension_id + "." + token_id;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream ss;
    ss << std::hex;
    ss.fill('0');
    for (uint8_t b : bytes) {
        ss.width(2);
        ss << static_cast<unsigned int>(b);
    }
    return ss.str();
}

inline uint32_t rotr(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32 - shift));
}

uint32_t read_be_u32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

void write_be_u32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    p[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    p[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    p[3] = static_cast<uint8_t>(value & 0xff);
}

std::vector<uint8_t> sha256_bytes(const std::vector<uint8_t>& input) {
    static const std::array<uint32_t, 64> k = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    uint32_t h0 = 0x6a09e667;
    uint32_t h1 = 0xbb67ae85;
    uint32_t h2 = 0x3c6ef372;
    uint32_t h3 = 0xa54ff53a;
    uint32_t h4 = 0x510e527f;
    uint32_t h5 = 0x9b05688c;
    uint32_t h6 = 0x1f83d9ab;
    uint32_t h7 = 0x5be0cd19;

    std::vector<uint8_t> data = input;
    const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8ULL;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) {
        data.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xff));
    }

    std::array<uint32_t, 64> w{};
    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        for (size_t i = 0; i < 16; ++i) {
            w[i] = read_be_u32(&data[chunk + i * 4]);
        }
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;
        uint32_t f = h5;
        uint32_t g = h6;
        uint32_t h = h7;

        for (size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
        h5 += f;
        h6 += g;
        h7 += h;
    }

    std::vector<uint8_t> digest(32, 0);
    write_be_u32(&digest[0], h0);
    write_be_u32(&digest[4], h1);
    write_be_u32(&digest[8], h2);
    write_be_u32(&digest[12], h3);
    write_be_u32(&digest[16], h4);
    write_be_u32(&digest[20], h5);
    write_be_u32(&digest[24], h6);
    write_be_u32(&digest[28], h7);
    return digest;
}

std::string hmac_sha256_hex(const std::string& key, const std::string& message) {
    constexpr size_t block_size = 64;
    std::vector<uint8_t> key_bytes(key.begin(), key.end());
    if (key_bytes.size() > block_size) {
        key_bytes = sha256_bytes(key_bytes);
    }
    key_bytes.resize(block_size, 0x00);

    std::vector<uint8_t> o_key_pad(block_size, 0x5c);
    std::vector<uint8_t> i_key_pad(block_size, 0x36);
    for (size_t i = 0; i < block_size; ++i) {
        o_key_pad[i] ^= key_bytes[i];
        i_key_pad[i] ^= key_bytes[i];
    }

    std::vector<uint8_t> inner(i_key_pad.begin(), i_key_pad.end());
    inner.insert(inner.end(), message.begin(), message.end());
    const std::vector<uint8_t> inner_hash = sha256_bytes(inner);

    std::vector<uint8_t> outer(o_key_pad.begin(), o_key_pad.end());
    outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
    const std::vector<uint8_t> mac = sha256_bytes(outer);

    return bytes_to_hex(mac);
}

std::string compute_signature(const std::string& unsigned_token, const std::string& signing_key) {
    return hmac_sha256_hex(signing_key, unsigned_token);
}

enum class TokenValidationFailure {
    EmptyToken,
    MalformedToken,
    UnknownToken,
    PayloadTampered,
    SignatureInvalid,
    ExpiredToken,
    CapabilityMismatch,
};

const char* to_string(TokenValidationFailure failure) {
    switch (failure) {
        case TokenValidationFailure::EmptyToken:
            return "empty-token";
        case TokenValidationFailure::MalformedToken:
            return "malformed-token";
        case TokenValidationFailure::UnknownToken:
            return "unknown-token";
        case TokenValidationFailure::PayloadTampered:
            return "payload-tampered";
        case TokenValidationFailure::SignatureInvalid:
            return "signature-invalid";
        case TokenValidationFailure::ExpiredToken:
            return "expired-token";
        case TokenValidationFailure::CapabilityMismatch:
            return "capability-mismatch";
    }
    return "unknown";
}

void log_validation_failure(TokenValidationFailure failure,
                            const std::string& token,
                            const std::string& required_capability) {
    (void)token;
    std::cerr << "Token validation failed [" << to_string(failure)
              << "] required='" << required_capability << "'" << std::endl;
}

struct ParsedToken {
    std::string version;
    std::string extension_id;
    std::string token_id;
    std::string key_id;
    std::string signature;
};

void parse_token_id_fields(ParsedToken& token) {
    const auto separator = token.token_id.find('-');
    if (separator == std::string::npos) {
        token.key_id.clear();
        return;
    }
    token.key_id = token.token_id.substr(0, separator);
}

bool parse_token(const std::string& token, ParsedToken& out) {
    if (token.empty()) {
        return false;
    }

    const auto first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        return false;
    }

    const auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return false;
    }

    const auto third_dot = token.find('.', second_dot + 1);
    if (third_dot == std::string::npos) {
        return false;
    }

    if (token.find('.', third_dot + 1) != std::string::npos) {
        return false;
    }

    out.version = token.substr(0, first_dot);
    out.extension_id = token.substr(first_dot + 1, second_dot - first_dot - 1);
    out.token_id = token.substr(second_dot + 1, third_dot - second_dot - 1);
    out.signature = token.substr(third_dot + 1);

    if (out.version != kTokenVersion) {
        return false;
    }
    if (!is_safe_token_segment(out.extension_id)) {
        return false;
    }
    if (!is_safe_token_segment(out.token_id)) {
        return false;
    }
    if (!is_safe_token_segment(out.signature)) {
        return false;
    }

    parse_token_id_fields(out);
    if (!out.key_id.empty() && !is_valid_signing_key_id(out.key_id)) {
        return false;
    }

    return true;
}
}  // namespace

/// @brief Default capability engine implementation
class CapabilityEngineImpl : public ICapabilityEngine {
public:
    CapabilityVerificationResult verify_token(const std::string& token, const std::string& required_capability) override {
        if (token.empty()) {
            log_validation_failure(TokenValidationFailure::EmptyToken, token, required_capability);
            return {false, "", "", 0, 0};
        }

        ParsedToken parsed;
        if (!parse_token(token, parsed)) {
            log_validation_failure(TokenValidationFailure::MalformedToken, token, required_capability);
            return {false, "", "", 0, 0};
        }
        
        std::lock_guard<std::mutex> lock(token_mutex_);
        auto it = issued_tokens_.find(parsed.token_id);
        if (it == issued_tokens_.end()) {
            log_validation_failure(TokenValidationFailure::UnknownToken, token, required_capability);
            return {false, "", "", 0, 0};
        }

        if (it->second.extension_id != parsed.extension_id) {
            log_validation_failure(TokenValidationFailure::PayloadTampered, token, required_capability);
            return {false, "", "", 0, 0};
        }

        const std::string unsigned_token = build_unsigned_token(parsed.version, parsed.extension_id, parsed.token_id);
        const std::string current_key = current_signing_key();
        const std::string previous_key = current_previous_signing_key();
        const std::string current_key_id = current_signing_key_id();
        const std::string previous_key_id = current_previous_signing_key_id();

        bool signature_valid = false;
        if (!parsed.key_id.empty()) {
            if (parsed.key_id == current_key_id) {
                signature_valid = (compute_signature(unsigned_token, current_key) == parsed.signature) ||
                                  (!previous_key.empty() && compute_signature(unsigned_token, previous_key) == parsed.signature);
            } else if (parsed.key_id == previous_key_id && !previous_key.empty()) {
                signature_valid = (compute_signature(unsigned_token, previous_key) == parsed.signature);
            } else {
                log_validation_failure(TokenValidationFailure::SignatureInvalid, token, required_capability);
                return {false, "", "", 0, 0};
            }
        } else {
            signature_valid = (compute_signature(unsigned_token, current_key) == parsed.signature) ||
                              (!previous_key.empty() && compute_signature(unsigned_token, previous_key) == parsed.signature);
        }

        if (!signature_valid) {
            log_validation_failure(TokenValidationFailure::SignatureInvalid, token, required_capability);
            return {false, "", "", 0, 0};
        }

        const auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        if (it->second.expires_at > 0 && now > it->second.expires_at) {
            log_validation_failure(TokenValidationFailure::ExpiredToken, token, required_capability);
            return {false, "", "", 0, 0};  // Token expired
        }

        auto matched_capability = std::find_if(
            it->second.capabilities.begin(),
            it->second.capabilities.end(),
            [&required_capability](const std::string& granted) {
                return capability_matches(granted, required_capability);
            });

        if (matched_capability == it->second.capabilities.end()) {
            log_validation_failure(TokenValidationFailure::CapabilityMismatch, token, required_capability);
            return {false, "", "", 0, 0};
        }

        return {true, it->second.extension_id, *matched_capability,
                it->second.issued_at, it->second.expires_at};
    }

    std::string issue_token(const std::string& extension_id, const std::vector<std::string>& capabilities, 
                           uint32_t expiry_seconds = 0) override {
        if (!is_safe_token_segment(extension_id)) {
            return "";
        }

        std::lock_guard<std::mutex> lock(token_mutex_);
        const std::string signing_key_id = current_signing_key_id();
        if (!is_valid_signing_key_id(signing_key_id)) {
            return "";
        }

        const std::string token_id = signing_key_id + "-" + std::to_string(next_token_id_++);
        const std::string unsigned_token = build_unsigned_token(kTokenVersion, extension_id, token_id);
        const std::string signature = compute_signature(unsigned_token, current_signing_key());
        const std::string token = unsigned_token + "." + signature;
        
        const auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        std::vector<std::string> granted_capabilities = capabilities.empty() ? std::vector<std::string>{"*"} : capabilities;

        TokenMetadata metadata{
            extension_id,
            std::move(granted_capabilities),
            now,
            expiry_seconds > 0 ? static_cast<uint64_t>(now + expiry_seconds) : 0
        };
        
        issued_tokens_[token_id] = metadata;
        return token;
    }

    bool revoke_token(const std::string& token) override {
        ParsedToken parsed;
        if (!parse_token(token, parsed)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(token_mutex_);
        auto it = issued_tokens_.find(parsed.token_id);
        if (it == issued_tokens_.end()) {
            return false;
        }

        if (it->second.extension_id != parsed.extension_id) {
            return false;
        }

        issued_tokens_.erase(it);
        return true;
    }

    std::vector<std::string> get_capabilities(const std::string& extension_id) const override {
        std::lock_guard<std::mutex> lock(token_mutex_);
        std::set<std::string> unique_capabilities;
        for (const auto& pair : issued_tokens_) {
            if (pair.second.extension_id == extension_id) {
                for (const auto& capability : pair.second.capabilities) {
                    unique_capabilities.insert(capability);
                }
            }
        }

        std::vector<std::string> result;
        result.reserve(unique_capabilities.size());
        for (const auto& capability : unique_capabilities) {
            result.push_back(capability);
        }

        return result;
    }

private:
    struct TokenMetadata {
        std::string extension_id;
        std::vector<std::string> capabilities;
        uint64_t issued_at;
        uint64_t expires_at;
    };

    mutable std::mutex token_mutex_;
    std::unordered_map<std::string, TokenMetadata> issued_tokens_;
    uint64_t next_token_id_ = 1;
};

/// @brief Default policy service implementation
class PolicyServiceImpl : public IPolicyService {
public:
    PolicyServiceImpl() : capability_engine_(std::make_unique<CapabilityEngineImpl>()) {}

    PolicyDecision evaluate(const std::string& requester_id, const std::string& action, 
                           const std::string& resource_id, const std::string& context = "") override {
        for (const auto& rule_id : rule_order_) {
            const auto it = rules_.find(rule_id);
            if (it == rules_.end()) {
                continue;
            }

            const PolicyRule& rule = it->second;
            if (!action_matches(rule.action_pattern, action)) {
                continue;
            }

            if (!rule.requester_id.empty() && rule.requester_id != requester_id) {
                continue;
            }

            if (!rule.resource_id.empty() && rule.resource_id != resource_id) {
                continue;
            }

            if (!rule.context_contains.empty() &&
                context.find(rule.context_contains) == std::string::npos) {
                continue;
            }

            return rule.effect;
        }

        // Default deny for unknown actions
        return PolicyDecision::Deny;
    }

    bool register_rule(const std::string& rule_id, const std::string& rule_condition) override {
        if (rule_id.empty() || rule_condition.empty()) {
            return false;
        }

        PolicyRule parsed_rule;
        std::string action_pattern;
        std::string effect_raw;
        if (!extract_json_string_field(rule_condition, "action", action_pattern)) {
            return false;
        }
        if (!extract_json_string_field(rule_condition, "effect", effect_raw)) {
            return false;
        }

        if (!parse_effect(effect_raw, parsed_rule.effect)) {
            return false;
        }

        parsed_rule.action_pattern = action_pattern;
        extract_json_string_field(rule_condition, "requester_id", parsed_rule.requester_id);
        extract_json_string_field(rule_condition, "resource_id", parsed_rule.resource_id);
        extract_json_string_field(rule_condition, "context_contains", parsed_rule.context_contains);

        if (rules_.find(rule_id) == rules_.end()) {
            rule_order_.push_back(rule_id);
        }
        rules_[rule_id] = parsed_rule;
        return true;
    }

    ICapabilityEngine& capability_engine() override {
        return *capability_engine_;
    }

    bool has_permission(const std::string& extension_id, const std::string& permission) const override {
        // Fast path: explicit permission table.
        auto it = extension_permissions_.find(extension_id);
        if (it != extension_permissions_.end()) {
            auto permission_it = it->second.find(permission);
            if (permission_it != it->second.end() && permission_it->second) {
                return true;
            }
        }

        // Fallback: derive effective permission from capability grants.
        const auto capabilities = capability_engine_->get_capabilities(extension_id);
        for (const auto& capability : capabilities) {
            if (capability_matches(capability, permission)) {
                return true;
            }
        }

        return false;
    }

private:
    struct PolicyRule {
        std::string action_pattern;
        PolicyDecision effect = PolicyDecision::Deny;
        std::string requester_id;
        std::string resource_id;
        std::string context_contains;
    };

    std::unique_ptr<ICapabilityEngine> capability_engine_;
    std::unordered_map<std::string, PolicyRule> rules_;
    std::vector<std::string> rule_order_;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> extension_permissions_;
};

static PolicyServiceImpl* g_policy_service = nullptr;

PolicyServiceImpl& get_policy_service() {
    ensure_policy_signing_keys_initialized();
    if (g_policy_service == nullptr) {
        g_policy_service = new PolicyServiceImpl();
    }
    return *g_policy_service;
}

void initialize_policy_service() {
    (void)get_policy_service();
}

IPolicyService& get_policy_service_interface() {
    return get_policy_service();
}

void set_policy_signing_key_for_tests(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_signing_key = key.empty() ? kDefaultSigningKey : key;
}

void reset_policy_signing_key_for_tests() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_signing_key = kDefaultSigningKey;
}

void set_policy_previous_signing_key_for_tests(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_previous_signing_key = key;
}

void reset_policy_previous_signing_key_for_tests() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_previous_signing_key.clear();
}

void set_policy_signing_key_id_for_tests(const std::string& key_id) {
    if (!is_valid_signing_key_id(key_id)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_signing_key_id = key_id;
}

void set_policy_previous_signing_key_id_for_tests(const std::string& key_id) {
    if (!is_valid_signing_key_id(key_id)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_previous_signing_key_id = key_id;
}

void reset_policy_signing_key_ids_for_tests() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_signing_key_id = kDefaultSigningKeyId;
    g_previous_signing_key_id = kDefaultPreviousSigningKeyId;
}

std::string compute_policy_hmac_for_tests(const std::string& message) {
    return hmac_sha256_hex(current_signing_key(), message);
}

void reload_policy_signing_keys_from_environment_for_tests() {
    apply_signing_keys_from_config_sources();
}

void set_policy_signing_secret_provider_for_tests(std::shared_ptr<IPolicySigningSecretProvider> provider) {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_policy_signing_secret_provider_for_tests = std::move(provider);
}

void reset_policy_signing_secret_provider_for_tests() {
    std::lock_guard<std::mutex> lock(g_signing_key_mutex);
    g_policy_signing_secret_provider_for_tests.reset();
}

}  // namespace sentinel::services::policy
