#include "tiktok/ApiServer.h"

#include "tiktok/Validation.h"

#include <drogon/Cookie.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace tiktok {
namespace {

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

drogon::HttpResponsePtr nextJson(Json::Value data,
                                 drogon::HttpStatusCode status = drogon::k200OK) {
    Json::Value envelope(Json::objectValue);
    envelope["data"] = std::move(data);
    auto response = drogon::HttpResponse::newHttpJsonResponse(envelope);
    response->setStatusCode(status);
    response->addHeader("Cache-Control", "no-store");
    return response;
}

drogon::HttpResponsePtr nextError(drogon::HttpStatusCode status,
                                  const std::string& code,
                                  const std::string& message) {
    Json::Value value(Json::objectValue);
    value["error"]["code"] = code;
    value["error"]["message"] = message;
    auto response = drogon::HttpResponse::newHttpJsonResponse(value);
    response->setStatusCode(status);
    response->addHeader("Cache-Control", "no-store");
    return response;
}

bool safeId(const std::string& value) {
    return !value.empty() && value.size() <= 240 && value.find('/') == std::string::npos &&
           value.find('\0') == std::string::npos;
}

std::size_t routeLimit(const drogon::HttpRequestPtr& request,
                       std::size_t fallback = 50,
                       std::size_t maximum = 100) {
    try {
        const auto raw = request->getParameter("limit");
        if (raw.empty()) return fallback;
        return std::max<std::size_t>(1, std::min<std::size_t>(maximum, std::stoull(raw)));
    } catch (...) { return fallback; }
}

}  // namespace

void ApiServer::registerNextRoutes() {
    auto& app = drogon::app();

    app.registerHandler(
        "/api/me/privacy",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (request->method() == drogon::Put && !validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, request->method() == drogon::Put);
                if (!auth) return;
                if (request->method() == drogon::Get) {
                    callback(nextJson(store_.getPrivacySettings(auth->userId)));
                    return;
                }
                const auto body = request->getJsonObject();
                if (!body || !body->isObject()) {
                    callback(nextError(drogon::k400BadRequest, "invalid_request",
                                       "Privacy settings must be an object"));
                    return;
                }
                callback(nextJson(store_.updatePrivacySettings(auth->userId, *body)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/feedback",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                const auto type = body && (*body)["type"].isString()
                                      ? (*body)["type"].asString() : "";
                const auto enabled = !body || !(*body)["enabled"].isBool()
                                         ? true : (*body)["enabled"].asBool();
                callback(nextJson(store_.setFeedFeedback(auth->userId, postId, type, enabled)));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/me/analytics",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                callback(nextJson(store_.creatorAnalytics(auth->userId)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/chat/{1}/history",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& otherUserId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                callback(nextJson(store_.listMessageHistory(
                    auth->userId, otherUserId, request->getParameter("before"), routeLimit(request))));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/chat/{1}/search",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& otherUserId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                const auto term = request->getParameter("q");
                if (term.size() < 2 || term.size() > 100) {
                    callback(nextError(drogon::k400BadRequest, "invalid_search",
                                       "Search must be 2–100 characters"));
                    return;
                }
                callback(nextJson(store_.searchMessages(
                    auth->userId, otherUserId, term, routeLimit(request))));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/chat/messages/{1}/edit",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& messageId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                const auto checked = validateMessage(body && (*body)["message"].isString()
                                                         ? (*body)["message"].asString() : "");
                if (!checked.ok) {
                    callback(nextError(drogon::k400BadRequest, "validation_failed", checked.error));
                    return;
                }
                callback(nextJson(store_.editMessage(messageId, auth->userId, checked.value)));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/reports/{1}/{2}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& targetType, const std::string& targetId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            if (!safeId(targetId)) {
                callback(nextError(drogon::k400BadRequest, "invalid_id", "Target ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                const auto reason = validateReportReason(
                    body && (*body)["reason"].isString() ? (*body)["reason"].asString() : "");
                const auto details = validateReportDetails(
                    body && (*body)["details"].isString() ? (*body)["details"].asString() : "");
                if (!reason.ok || !details.ok) {
                    callback(nextError(drogon::k400BadRequest, "validation_failed",
                                       !reason.ok ? reason.error : details.error));
                    return;
                }
                callback(nextJson(store_.createContentReport(
                    randomToken(16), auth->userId, targetType, targetId,
                    reason.value, details.value), drogon::k201Created));
            } catch (...) { handleException(callback); }
        }, {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/me/sessions",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (request->method() == drogon::Delete && !validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, request->method() == drogon::Delete);
                if (!auth) return;
                if (request->method() == drogon::Get) {
                    callback(nextJson(store_.listSessions(auth->userId, auth->tokenHash)));
                    return;
                }
                Json::Value result(Json::objectValue);
                result["revoked"] = static_cast<Json::UInt64>(
                    store_.revokeOtherSessions(auth->userId, auth->tokenHash));
                callback(nextJson(std::move(result)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/me/security-events",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                callback(nextJson(store_.listSecurityEvents(auth->userId, routeLimit(request, 30, 50))));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/me/export",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                auto response = nextJson(store_.exportAccount(auth->userId));
                response->addHeader("Content-Disposition", "attachment; filename=tiktik-account-export.json");
                callback(response);
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/me/account",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                if (!body || (*body)["confirmation"].asString() != "DELETE") {
                    callback(nextError(drogon::k400BadRequest, "confirmation_required",
                                       "Type DELETE to confirm permanent account deletion"));
                    return;
                }
                auto result = store_.deleteAccount(auth->userId);
                for (const auto& file : result["mediaFiles"]) {
                    std::error_code ignored;
                    std::filesystem::remove(config_.mediaRoot / file.asString(), ignored);
                }
                result.removeMember("mediaFiles");
                auto response = nextJson(std::move(result));
                drogon::Cookie cookie("tt_session", "");
                cookie.setPath("/");
                cookie.setHttpOnly(true);
                cookie.setSecure(config_.secureCookies);
                cookie.setSameSite(drogon::Cookie::SameSite::kLax);
                cookie.setMaxAge(0);
                response->addCookie(std::move(cookie));
                callback(response);
            } catch (...) { handleException(callback); }
        }, {drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/me/follow-requests",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                callback(nextJson(store_.listFollowRequests(auth->userId)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/me/follow-requests/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& requesterId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                if (!body || !(*body)["accept"].isBool()) {
                    callback(nextError(drogon::k400BadRequest, "invalid_request",
                                       "accept must be true or false"));
                    return;
                }
                callback(nextJson(store_.respondToFollowRequest(
                    auth->userId, requesterId, (*body)["accept"].asBool())));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/push-subscriptions",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                const auto endpoint = body && (*body)["endpoint"].isString()
                                          ? (*body)["endpoint"].asString() : "";
                if (endpoint.empty() || endpoint.size() > 2048) {
                    callback(nextError(drogon::k400BadRequest, "invalid_subscription",
                                       "Push subscription endpoint is invalid"));
                    return;
                }
                if (request->method() == drogon::Delete) {
                    Json::Value value(Json::objectValue);
                    value["deleted"] = static_cast<Json::UInt64>(
                        store_.deletePushSubscription(auth->userId, endpoint));
                    callback(nextJson(std::move(value)));
                    return;
                }
                callback(nextJson(store_.savePushSubscription(
                    auth->userId, endpoint,
                    (*body)["keys"]["p256dh"].asString(),
                    (*body)["keys"]["auth"].asString())));
            } catch (...) { handleException(callback); }
        }, {drogon::Post, drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/admin/metrics",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                if (!isAdmin(auth->userId)) {
                    callback(nextError(drogon::k403Forbidden, "admin_required",
                                       "Administrator access is required"));
                    return;
                }
                static const auto started = std::chrono::steady_clock::now();
                auto value = store_.adminStatistics();
                value["uptimeSeconds"] = static_cast<Json::Int64>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - started).count());
                value["database"] = "connected";
                value["mediaRootAvailable"] = std::filesystem::exists(config_.mediaRoot);
                callback(nextJson(std::move(value)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});
}

}  // namespace tiktok
