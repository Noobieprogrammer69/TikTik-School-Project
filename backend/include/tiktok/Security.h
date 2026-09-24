#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tiktok {

std::string randomToken(std::size_t byteCount = 32);
std::string sha256Hex(std::string_view value);
bool secureEquals(std::string_view left, std::string_view right);

class LoginRateLimiter {
  public:
    LoginRateLimiter(std::size_t maximumAttempts, std::chrono::seconds window);
    bool allow(const std::string& key, std::chrono::steady_clock::time_point now);
    void clear();

  private:
    std::size_t maximumAttempts_;
    std::chrono::seconds window_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> attempts_;
};

}  // namespace tiktok

