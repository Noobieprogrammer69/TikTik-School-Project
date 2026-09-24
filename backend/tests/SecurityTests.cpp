#include "tiktok/GoogleVerifier.h"
#include "tiktok/Security.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("random tokens are non-empty, fixed length, and distinct") {
    const auto first = tiktok::randomToken(16);
    const auto second = tiktok::randomToken(16);
    REQUIRE(first.size() == 32);
    REQUIRE(second.size() == 32);
    REQUIRE(first != second);
}

TEST_CASE("SHA-256 hashing uses the expected encoding") {
    REQUIRE(tiktok::sha256Hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(tiktok::secureEquals("same-token", "same-token"));
    REQUIRE_FALSE(tiktok::secureEquals("same-token", "other-token"));
    REQUIRE_FALSE(tiktok::secureEquals("short", "longer"));
}

TEST_CASE("login rate limiter expires attempts after its window") {
    tiktok::LoginRateLimiter limiter(2, 60s);
    const auto start = std::chrono::steady_clock::time_point{};
    REQUIRE(limiter.allow("127.0.0.1", start));
    REQUIRE(limiter.allow("127.0.0.1", start + 1s));
    REQUIRE_FALSE(limiter.allow("127.0.0.1", start + 2s));
    REQUIRE(limiter.allow("another-address", start + 2s));
    REQUIRE(limiter.allow("127.0.0.1", start + 61s));
}

TEST_CASE("Google PEM signing certificates are indexed by key ID") {
    Json::Value response(Json::objectValue);
    response["signing-key-one"] =
        "-----BEGIN CERTIFICATE-----\nexample\n-----END CERTIFICATE-----\n";

    const auto certificates = tiktok::parseGoogleCertificates(response);

    REQUIRE(certificates.size() == 1);
    REQUIRE(certificates.at("signing-key-one") == response["signing-key-one"].asString());
}

TEST_CASE("Google certificate parser ignores JWK keys without x5c") {
    Json::Value response(Json::objectValue);
    response["keys"] = Json::Value(Json::arrayValue);
    Json::Value key(Json::objectValue);
    key["kid"] = "rsa-modulus-only";
    key["n"] = "not-an-x5c-certificate";
    response["keys"].append(key);

    REQUIRE(tiktok::parseGoogleCertificates(response).empty());
}
