#pragma once

#include <drogon/HttpClient.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace tiktok {

struct GoogleClaims {
    std::string subject;
    std::string name;
    std::string picture;
    std::string email;
    bool emailVerified{false};
};

// Google publishes signing keys in both JWK and PEM response shapes. This
// parser accepts the PEM map used by /oauth2/v1/certs and the older x5c JWK
// shape so certificate rotation does not break login.
std::unordered_map<std::string, std::string> parseGoogleCertificates(
    const Json::Value& response);

class GoogleVerifier {
  public:
    using Callback = std::function<void(std::optional<GoogleClaims>, std::string)>;

    explicit GoogleVerifier(std::string clientId);
    void verifyAsync(const std::string& credential, Callback callback);

  private:
    std::optional<GoogleClaims> verifyWithCertificate(const std::string& credential,
                                                      const std::string& certificate,
                                                      std::string& error) const;
    std::optional<std::string> cachedCertificate(const std::string& keyId);
    void cacheCertificates(const Json::Value& jwks);

    std::string clientId_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::string> certificates_;
    std::chrono::steady_clock::time_point certificatesExpireAt_{};
};

}  // namespace tiktok
