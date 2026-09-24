#pragma once

#include "tiktok/Config.h"
#include "tiktok/GoogleVerifier.h"
#include "tiktok/MongoStore.h"
#include "tiktok/Security.h"

#include <drogon/HttpAppFramework.h>

#include <filesystem>
#include <optional>
#include <string>

namespace tiktok {

class ApiServer {
  public:
    ApiServer(AppConfig config, MongoStore& store);
    void registerRoutes();

  private:
    struct AuthContext {
        std::string tokenHash;
        std::string userId;
        std::string csrfHash;
        std::chrono::system_clock::time_point expiresAt;
    };

    using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;

    std::optional<AuthContext> authenticate(const drogon::HttpRequestPtr& request,
                                            const ResponseCallback& callback,
                                            bool requireCsrf);
    drogon::HttpResponsePtr sessionResponse(const Json::Value& user,
                                            const std::string& rawSessionToken,
                                            const std::string& csrfToken,
                                            std::chrono::system_clock::time_point expiresAt,
                                            bool setCookie) const;
    void establishSession(const UserIdentity& identity, const ResponseCallback& callback);
    bool validMutationOrigin(const drogon::HttpRequestPtr& request,
                             const ResponseCallback& callback) const;
    drogon::HttpResponsePtr optionsResponse(const drogon::HttpRequestPtr& request) const;
    void handleException(const ResponseCallback& callback) const;
    void registerUpgradeRoutes();
    void registerNextRoutes();
    bool isAdmin(const std::string& userId) const;

    AppConfig config_;
    MongoStore& store_;
    GoogleVerifier googleVerifier_;
    LoginRateLimiter loginRateLimiter_;
};

}  // namespace tiktok
