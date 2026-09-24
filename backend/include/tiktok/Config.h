#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace tiktok {

struct AppConfig {
    std::string mongoUri{"mongodb://127.0.0.1:27017"};
    std::string mongoDatabase{"tiktok_clone"};
    std::filesystem::path mediaRoot{"data/media"};
    std::string listenAddress{"0.0.0.0"};
    std::uint16_t port{8080};
    std::string frontendOrigin{"http://localhost:3000"};
    std::string googleClientId;
    std::size_t maxUploadBytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
    std::uint32_t sessionTtlSeconds{7U * 24U * 60U * 60U};
    std::uint32_t loginRateLimit{10};
    std::uint32_t loginRateWindowSeconds{300};
    bool secureCookies{false};
    bool enableTestAuth{false};
    std::string testAuthKey;
    std::unordered_set<std::string> adminUserIds;
    bool enableMediaProcessing{true};
    std::string ffmpegBinary{"ffmpeg"};

    static AppConfig fromEnvironment();
};

}  // namespace tiktok
