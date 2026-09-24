#include "tiktok/Security.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tiktok {

std::string randomToken(std::size_t byteCount) {
    if (byteCount == 0 || byteCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("invalid random token length");
    }
    std::vector<unsigned char> bytes(byteCount);
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("OpenSSL could not generate secure random bytes");
    }

    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.resize(bytes.size() * 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = alphabet[bytes[index] >> 4U];
        result[index * 2 + 1] = alphabet[bytes[index] & 0x0fU];
    }
    return result;
}

std::string sha256Hex(std::string_view value) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digestLength = 0;
    if (EVP_Digest(value.data(), value.size(), digest.data(), &digestLength, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("OpenSSL could not calculate SHA-256");
    }

    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.resize(digestLength * 2);
    for (unsigned int index = 0; index < digestLength; ++index) {
        result[index * 2] = alphabet[digest[index] >> 4U];
        result[index * 2 + 1] = alphabet[digest[index] & 0x0fU];
    }
    return result;
}

bool secureEquals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

LoginRateLimiter::LoginRateLimiter(std::size_t maximumAttempts, std::chrono::seconds window)
    : maximumAttempts_(maximumAttempts), window_(window) {
    if (maximumAttempts_ == 0 || window_.count() <= 0) {
        throw std::invalid_argument("rate limiter values must be positive");
    }
}

bool LoginRateLimiter::allow(const std::string& key, std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    auto& attempts = attempts_[key];
    const auto cutoff = now - window_;
    while (!attempts.empty() && attempts.front() <= cutoff) {
        attempts.pop_front();
    }
    if (attempts.size() >= maximumAttempts_) {
        return false;
    }
    attempts.push_back(now);
    return true;
}

void LoginRateLimiter::clear() {
    std::lock_guard lock(mutex_);
    attempts_.clear();
}

}  // namespace tiktok
