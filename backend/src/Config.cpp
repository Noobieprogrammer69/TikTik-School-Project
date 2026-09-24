#include "tiktok/Config.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <sstream>

namespace tiktok {
namespace {

std::string envString(const char* name, std::string fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::move(fallback) : std::string(value);
}

bool envBool(const char* name, bool fallback) {
    const auto value = envString(name);
    if (value.empty()) {
        return fallback;
    }

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    throw std::runtime_error(std::string(name) + " must be true/false or 1/0");
}

std::uint64_t envUnsigned(const char* name, std::uint64_t fallback, std::uint64_t maximum) {
    const auto value = envString(name);
    if (value.empty()) {
        return fallback;
    }

    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtoull(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' || parsed == 0 || parsed > maximum) {
        throw std::runtime_error(std::string(name) + " must be a positive integer no greater than " +
                                 std::to_string(maximum));
    }
    return parsed;
}

}  // namespace

AppConfig AppConfig::fromEnvironment() {
    constexpr std::uint64_t kMaximumConfiguredUpload = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    AppConfig config;
    config.mongoUri = envString("MONGODB_URI", config.mongoUri);
    config.mongoDatabase = envString("MONGODB_DATABASE", config.mongoDatabase);
    config.mediaRoot = envString("MEDIA_ROOT", config.mediaRoot.string());
    config.listenAddress = envString("BACKEND_LISTEN_ADDRESS", config.listenAddress);
    config.port = static_cast<std::uint16_t>(
        envUnsigned("BACKEND_PORT", config.port, std::numeric_limits<std::uint16_t>::max()));
    config.frontendOrigin = envString("FRONTEND_ORIGIN", config.frontendOrigin);
    config.googleClientId = envString("GOOGLE_CLIENT_ID");
    config.maxUploadBytes = static_cast<std::size_t>(
        envUnsigned("MAX_UPLOAD_BYTES", config.maxUploadBytes, kMaximumConfiguredUpload));
    config.sessionTtlSeconds = static_cast<std::uint32_t>(
        envUnsigned("SESSION_TTL_SECONDS", config.sessionTtlSeconds,
                    std::numeric_limits<std::uint32_t>::max()));
    config.loginRateLimit = static_cast<std::uint32_t>(
        envUnsigned("LOGIN_RATE_LIMIT", config.loginRateLimit, 10'000));
    config.loginRateWindowSeconds = static_cast<std::uint32_t>(
        envUnsigned("LOGIN_RATE_WINDOW_SECONDS", config.loginRateWindowSeconds, 86'400));
    config.secureCookies = envBool("COOKIE_SECURE", config.secureCookies);
    config.enableTestAuth = envBool("ENABLE_TEST_AUTH", config.enableTestAuth);
    config.testAuthKey = envString("TEST_AUTH_KEY");
    config.enableMediaProcessing = envBool("ENABLE_MEDIA_PROCESSING", config.enableMediaProcessing);
    config.ffmpegBinary = envString("FFMPEG_BINARY", config.ffmpegBinary);
    const auto configuredAdmins = envString("ADMIN_USER_IDS");
    std::istringstream adminStream(configuredAdmins);
    std::string adminId;
    while (std::getline(adminStream, adminId, ',')) {
        const auto first = adminId.find_first_not_of(" \t\r\n");
        const auto last = adminId.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            config.adminUserIds.insert(adminId.substr(first, last - first + 1));
        }
    }

    if (config.mongoDatabase.empty() || config.mongoDatabase.size() > 63 ||
        config.mongoDatabase.find_first_of("/\\. \"$\0") != std::string::npos) {
        throw std::runtime_error("MONGODB_DATABASE is not a valid MongoDB database name");
    }
    if (config.enableTestAuth && config.testAuthKey.size() < 24) {
        throw std::runtime_error("TEST_AUTH_KEY must be at least 24 characters when ENABLE_TEST_AUTH=true");
    }
    return config;
}

}  // namespace tiktok
