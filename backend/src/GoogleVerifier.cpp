#include "tiktok/GoogleVerifier.h"

#include <jwt-cpp/jwt.h>

#include <chrono>
#include <exception>
#include <utility>

namespace tiktok {
namespace {

std::string stringClaim(const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& token,
                        const std::string& name) {
    if (!token.has_payload_claim(name)) {
        return {};
    }
    try {
        return token.get_payload_claim(name).as_string();
    } catch (const std::exception&) {
        return {};
    }
}

bool boolClaim(const jwt::decoded_jwt<jwt::traits::kazuho_picojson>& token,
               const std::string& name) {
    if (!token.has_payload_claim(name)) {
        return false;
    }
    try {
        return token.get_payload_claim(name).as_boolean();
    } catch (const std::exception&) {
        return stringClaim(token, name) == "true";
    }
}

}  // namespace

GoogleVerifier::GoogleVerifier(std::string clientId) : clientId_(std::move(clientId)) {}

std::unordered_map<std::string, std::string> parseGoogleCertificates(
    const Json::Value& response) {
    std::unordered_map<std::string, std::string> certificates;

    // The official PEM endpoint returns { "kid": "-----BEGIN CERTIFICATE..." }.
    if (response.isObject()) {
        for (const auto& keyId : response.getMemberNames()) {
            const auto& value = response[keyId];
            if (keyId != "keys" && value.isString() && !value.asString().empty()) {
                certificates.emplace(keyId, value.asString());
            }
        }
    }

    // Retain compatibility with JWK responses that include an x5c chain.
    if (response["keys"].isArray()) {
        for (const auto& key : response["keys"]) {
            if (!key["kid"].isString() || !key["x5c"].isArray() || key["x5c"].empty() ||
                !key["x5c"][0].isString()) {
                continue;
            }
            try {
                certificates.insert_or_assign(
                    key["kid"].asString(),
                    jwt::helper::convert_base64_der_to_pem(key["x5c"][0].asString()));
            } catch (const std::exception&) {
                // Ignore malformed keys and retain only usable certificates.
            }
        }
    }

    return certificates;
}

std::optional<std::string> GoogleVerifier::cachedCertificate(const std::string& keyId) {
    std::lock_guard lock(mutex_);
    if (std::chrono::steady_clock::now() >= certificatesExpireAt_) {
        certificates_.clear();
        return std::nullopt;
    }
    const auto found = certificates_.find(keyId);
    return found == certificates_.end() ? std::nullopt : std::optional<std::string>(found->second);
}

void GoogleVerifier::cacheCertificates(const Json::Value& response) {
    auto next = parseGoogleCertificates(response);
    std::lock_guard lock(mutex_);
    certificates_ = std::move(next);
    certificatesExpireAt_ = std::chrono::steady_clock::now() + std::chrono::hours(1);
}

std::optional<GoogleClaims> GoogleVerifier::verifyWithCertificate(const std::string& credential,
                                                                  const std::string& certificate,
                                                                  std::string& error) const {
    try {
        const auto decoded = jwt::decode(credential);
        if (decoded.get_algorithm() != "RS256") {
            error = "Google credential uses an unsupported signing algorithm";
            return std::nullopt;
        }

        jwt::verify()
            .allow_algorithm(jwt::algorithm::rs256{certificate, "", "", ""})
            .with_audience(clientId_)
            .verify(decoded);

        const auto issuer = decoded.get_issuer();
        if (issuer != "https://accounts.google.com" && issuer != "accounts.google.com") {
            error = "Google credential has an invalid issuer";
            return std::nullopt;
        }
        if (!decoded.has_expires_at() || decoded.get_expires_at() <= std::chrono::system_clock::now()) {
            error = "Google credential has expired";
            return std::nullopt;
        }

        GoogleClaims claims;
        claims.subject = decoded.get_subject();
        claims.name = stringClaim(decoded, "name");
        claims.picture = stringClaim(decoded, "picture");
        claims.email = stringClaim(decoded, "email");
        claims.emailVerified = boolClaim(decoded, "email_verified");
        if (claims.subject.empty() || claims.name.empty()) {
            error = "Google credential is missing required identity claims";
            return std::nullopt;
        }
        if (!claims.email.empty() && !claims.emailVerified) {
            error = "Google email address is not verified";
            return std::nullopt;
        }
        return claims;
    } catch (const std::exception& exception) {
        error = std::string("Google credential verification failed: ") + exception.what();
        return std::nullopt;
    }
}

void GoogleVerifier::verifyAsync(const std::string& credential, Callback callback) {
    if (clientId_.empty()) {
        callback(std::nullopt, "Google login is not configured on this server");
        return;
    }

    std::string keyId;
    try {
        const auto decoded = jwt::decode(credential);
        if (!decoded.has_header_claim("kid")) {
            callback(std::nullopt, "Google credential is missing a signing key ID");
            return;
        }
        keyId = decoded.get_header_claim("kid").as_string();
    } catch (const std::exception&) {
        callback(std::nullopt, "Google credential is not a valid JWT");
        return;
    }

    if (const auto certificate = cachedCertificate(keyId)) {
        std::string error;
        auto claims = verifyWithCertificate(credential, *certificate, error);
        callback(std::move(claims), std::move(error));
        return;
    }

    auto client = drogon::HttpClient::newHttpClient("https://www.googleapis.com", nullptr, false, true);
    auto request = drogon::HttpRequest::newHttpRequest();
    // The PEM endpoint is an official Google signing-key endpoint and maps
    // each JWT kid directly to an X.509 certificate accepted by jwt-cpp.
    request->setPath("/oauth2/v1/certs");
    request->setMethod(drogon::Get);
    client->sendRequest(
        request,
        [this, client, credential, keyId, callback = std::move(callback)](
            drogon::ReqResult requestResult, const drogon::HttpResponsePtr& response) mutable {
            if (requestResult != drogon::ReqResult::Ok || !response ||
                response->getStatusCode() != drogon::k200OK || !response->getJsonObject()) {
                callback(std::nullopt, "Could not retrieve Google's public signing keys");
                return;
            }
            cacheCertificates(*response->getJsonObject());
            const auto certificate = cachedCertificate(keyId);
            if (!certificate) {
                callback(std::nullopt, "Google credential references an unknown signing key");
                return;
            }
            std::string error;
            auto claims = verifyWithCertificate(credential, *certificate, error);
            callback(std::move(claims), std::move(error));
        },
        10.0);
}

}  // namespace tiktok
