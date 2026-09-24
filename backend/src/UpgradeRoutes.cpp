#include "tiktok/ApiServer.h"

#include "tiktok/RealtimeHub.h"
#include "tiktok/Security.h"
#include "tiktok/Validation.h"

#include <drogon/MultiPart.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <system_error>

namespace tiktok {
namespace {

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

drogon::HttpResponsePtr upgradedJson(Json::Value data,
                                     drogon::HttpStatusCode status = drogon::k200OK) {
    Json::Value envelope(Json::objectValue);
    envelope["data"] = std::move(data);
    auto response = drogon::HttpResponse::newHttpJsonResponse(envelope);
    response->setStatusCode(status);
    response->addHeader("Cache-Control", "no-store");
    return response;
}

drogon::HttpResponsePtr upgradedError(drogon::HttpStatusCode status,
                                      const std::string& code,
                                      const std::string& message) {
    Json::Value body(Json::objectValue);
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    response->addHeader("Cache-Control", "no-store");
    return response;
}

bool upgradeId(const std::string& value) {
    return !value.empty() && value.size() <= 200 && value.find('/') == std::string::npos &&
           value.find('\0') == std::string::npos;
}

std::string trimmed(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::pair<std::string, std::string>> imageType(const drogon::HttpFile& file) {
    const auto bytes = file.fileContent();
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 &&
        static_cast<unsigned char>(bytes[2]) == 0xff) {
        return std::pair<std::string, std::string>{"jpg", "image/jpeg"};
    }
    if (bytes.size() >= 8 && bytes.substr(0, 8) == std::string_view{"\x89PNG\r\n\x1a\n", 8}) {
        return std::pair<std::string, std::string>{"png", "image/png"};
    }
    if (bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WEBP") {
        return std::pair<std::string, std::string>{"webp", "image/webp"};
    }
    return std::nullopt;
}

std::size_t requestedLimit(const drogon::HttpRequestPtr& request,
                           std::size_t fallback = 12,
                           std::size_t maximum = 50) {
    const auto raw = request->getParameter("limit");
    if (raw.empty()) return fallback;
    try {
        const auto value = std::stoull(raw);
        return std::max<std::size_t>(1, std::min<std::size_t>(maximum, value));
    } catch (...) {
        return fallback;
    }
}

}  // namespace

bool ApiServer::isAdmin(const std::string& userId) const {
    return config_.adminUserIds.contains(userId);
}

void ApiServer::registerUpgradeRoutes() {
    auto& app = drogon::app();

    app.registerHandler(
        "/api/me/profile",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (request->method() == drogon::Put && !validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, request->method() == drogon::Put);
                if (!auth) return;
                if (request->method() == drogon::Get) {
                    const auto user = store_.getUser(auth->userId);
                    if (!user) throw StoreError(StoreError::Kind::kNotFound, "User not found");
                    auto value = *user;
                    value["isAdmin"] = isAdmin(auth->userId);
                    callback(upgradedJson(std::move(value)));
                    return;
                }
                const auto body = request->getJsonObject();
                const auto name = trimmed(body && (*body)["userName"].isString()
                                              ? (*body)["userName"].asString() : "");
                const auto bio = trimmed(body && (*body)["bio"].isString()
                                             ? (*body)["bio"].asString() : "");
                if (name.size() < 2 || name.size() > 50 || bio.size() > 160) {
                    callback(upgradedError(drogon::k400BadRequest, "validation_failed",
                                           "Display name must be 2–50 characters and bio at most 160"));
                    return;
                }
                auto value = store_.updateProfile(auth->userId, {name, bio});
                value["isAdmin"] = isAdmin(auth->userId);
                callback(upgradedJson(std::move(value)));
            } catch (...) { handleException(callback); }
        },
        {drogon::Get, drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/me/avatar",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                drogon::MultiPartParser parser;
                if (parser.parse(request) != 0 || parser.getFiles().size() != 1) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_upload",
                                           "Upload exactly one profile image"));
                    return;
                }
                const auto& file = parser.getFiles().front();
                if (file.fileLength() == 0 || file.fileLength() > 5ULL * 1024ULL * 1024ULL) {
                    callback(upgradedError(drogon::k413RequestEntityTooLarge, "invalid_avatar",
                                           "Profile image must be no larger than 5 MiB"));
                    return;
                }
                const auto type = imageType(file);
                if (!type) {
                    callback(upgradedError(drogon::k415UnsupportedMediaType, "invalid_avatar",
                                           "Profile image must be JPEG, PNG, or WebP"));
                    return;
                }
                const auto previous = store_.getAvatar(auth->userId);
                const auto storageName = "avatar-" + randomToken(16) + "." + type->first;
                const auto target = config_.mediaRoot / storageName;
                if (file.saveAs(target.string()) != 0) {
                    callback(upgradedError(drogon::k500InternalServerError, "media_write_failed",
                                           "Profile image could not be stored"));
                    return;
                }
                try {
                    auto user = store_.updateAvatar(auth->userId, storageName, type->second);
                    user["isAdmin"] = isAdmin(auth->userId);
                    if (previous && !previous->storageName.empty()) {
                        std::error_code ignored;
                        std::filesystem::remove(config_.mediaRoot / previous->storageName, ignored);
                    }
                    callback(upgradedJson(std::move(user)));
                } catch (...) {
                    std::error_code ignored;
                    std::filesystem::remove(target, ignored);
                    throw;
                }
            } catch (...) { handleException(callback); }
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/avatars/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& userId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                const auto asset = store_.getAvatar(userId);
                if (!asset) {
                    callback(upgradedError(drogon::k404NotFound, "not_found", "Profile image not found"));
                    return;
                }
                auto response = drogon::HttpResponse::newFileResponse(
                    (config_.mediaRoot / asset->storageName).string(), "", drogon::CT_CUSTOM,
                    asset->contentType, request);
                response->addHeader("Cache-Control", "public, max-age=300");
                callback(response);
            } catch (...) { handleException(callback); }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/feed",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                std::string viewerId;
                if (!request->getCookie("tt_session").empty()) {
                    const auto auth = authenticate(request, callback, false);
                    if (!auth) return;
                    viewerId = auth->userId;
                }
                auto mode = request->getParameter("mode");
                if (mode.empty()) mode = "for_you";
                if (mode != "for_you" && mode != "following") {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_feed", "Unknown feed mode"));
                    return;
                }
                if (mode == "following" && viewerId.empty()) {
                    callback(upgradedError(drogon::k401Unauthorized, "authentication_required",
                                           "Log in to open your following feed"));
                    return;
                }
                std::optional<std::string> topic;
                if (!request->getParameter("topic").empty()) {
                    const auto checked = validateTopic(request->getParameter("topic"));
                    if (!checked.ok) {
                        callback(upgradedError(drogon::k400BadRequest, "invalid_topic", checked.error));
                        return;
                    }
                    topic = checked.value;
                }
                std::optional<std::string> hashtag;
                if (!request->getParameter("hashtag").empty()) {
                    auto value = request->getParameter("hashtag");
                    if (!value.empty() && value.front() == '#') value.erase(value.begin());
                    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (value.empty() || value.size() > 50 ||
                        !std::all_of(value.begin(), value.end(), [](unsigned char c) {
                            return std::isalnum(c) || c == '_';
                        })) {
                        callback(upgradedError(drogon::k400BadRequest, "invalid_hashtag",
                                               "Hashtag is invalid"));
                        return;
                    }
                    hashtag = value;
                }
                callback(upgradedJson(store_.listFeed(
                    viewerId, mode, topic, hashtag, request->getParameter("cursor"),
                    requestedLimit(request))));
            } catch (...) { handleException(callback); }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/users/{1}/safety",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& targetId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!upgradeId(targetId)) {
                callback(upgradedError(drogon::k400BadRequest, "invalid_id", "User ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                callback(upgradedJson(store_.getSafetyRelationship(auth->userId, targetId)));
            } catch (...) { handleException(callback); }
        },
        {drogon::Get, drogon::Options});

    const auto registerSafetyMutation = [this, &app](const std::string& path, bool block) {
        app.registerHandler(
            path,
            [this, block](const drogon::HttpRequestPtr& request, Callback&& callback,
                          const std::string& targetId) {
                if (request->method() == drogon::Options) {
                    callback(optionsResponse(request));
                    return;
                }
                if (!validMutationOrigin(request, callback)) return;
                try {
                    const auto auth = authenticate(request, callback, true);
                    if (!auth) return;
                    const auto body = request->getJsonObject();
                    const auto field = block ? "blocked" : "muted";
                    if (!body || !(*body)[field].isBool()) {
                        callback(upgradedError(drogon::k400BadRequest, "invalid_request",
                                               std::string(field) + " must be true or false"));
                        return;
                    }
                    callback(upgradedJson(block
                        ? store_.setBlocked(auth->userId, targetId, (*body)[field].asBool())
                        : store_.setMuted(auth->userId, targetId, (*body)[field].asBool())));
                } catch (...) { handleException(callback); }
            },
            {drogon::Put, drogon::Options});
    };
    registerSafetyMutation("/api/users/{1}/block", true);
    registerSafetyMutation("/api/users/{1}/mute", false);

    app.registerHandler(
        "/api/posts/{1}/save",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                if (!body || !(*body)["saved"].isBool()) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_request",
                                           "saved must be true or false"));
                    return;
                }
                callback(upgradedJson(store_.setSaved(postId, auth->userId,
                                                       (*body)["saved"].asBool())));
            } catch (...) { handleException(callback); }
        },
        {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/me/saved",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                callback(upgradedJson(store_.listSaved(auth->userId)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/me/history",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                callback(upgradedJson(store_.listHistory(auth->userId)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/view",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                std::string userId;
                if (!request->getCookie("tt_session").empty()) {
                    const auto auth = authenticate(request, callback, true);
                    if (!auth) return;
                    userId = auth->userId;
                }
                const auto body = request->getJsonObject();
                const auto seconds = body && (*body)["watchSeconds"].isUInt()
                    ? (*body)["watchSeconds"].asUInt() : 0U;
                if (seconds > 86'400U) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_view",
                                           "Watch time is invalid"));
                    return;
                }
                callback(upgradedJson(store_.recordView(postId, userId, seconds)));
            } catch (...) { handleException(callback); }
        }, {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/share",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                callback(upgradedJson(store_.recordShare(postId, auth->userId)));
            } catch (...) { handleException(callback); }
        }, {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/comments/{2}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId, const std::string& commentId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                if (request->method() == drogon::Delete) {
                    callback(upgradedJson(store_.deleteComment(postId, commentId, auth->userId)));
                    return;
                }
                const auto body = request->getJsonObject();
                const auto checked = validateComment(body && (*body)["comment"].isString()
                    ? (*body)["comment"].asString() : "");
                if (!checked.ok) {
                    callback(upgradedError(drogon::k400BadRequest, "validation_failed", checked.error));
                    return;
                }
                callback(upgradedJson(store_.editComment(postId, commentId, auth->userId,
                                                          checked.value)));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/comments/{2}/replies/{3}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId, const std::string& commentId,
               const std::string& replyId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                if (request->method() == drogon::Delete) {
                    callback(upgradedJson(store_.deleteReply(postId, commentId, replyId,
                                                             auth->userId)));
                    return;
                }
                const auto body = request->getJsonObject();
                const auto checked = validateComment(body && (*body)["reply"].isString()
                    ? (*body)["reply"].asString() : "");
                if (!checked.ok) {
                    callback(upgradedError(drogon::k400BadRequest, "validation_failed", checked.error));
                    return;
                }
                callback(upgradedJson(store_.editReply(postId, commentId, replyId,
                                                        auth->userId, checked.value)));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/notification-preferences",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (request->method() == drogon::Put && !validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, request->method() == drogon::Put);
                if (!auth) return;
                if (request->method() == drogon::Get) {
                    callback(upgradedJson(store_.getNotificationPreferences(auth->userId)));
                    return;
                }
                const auto body = request->getJsonObject();
                if (!body || !body->isObject()) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_request",
                                           "Notification preferences must be an object"));
                    return;
                }
                callback(upgradedJson(store_.updateNotificationPreferences(auth->userId, *body)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/notifications/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& notificationId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                Json::Value value(Json::objectValue);
                value[request->method() == drogon::Delete ? "deleted" : "read"] =
                    static_cast<Json::UInt64>(request->method() == drogon::Delete
                        ? store_.deleteNotification(auth->userId, notificationId)
                        : store_.markNotificationRead(auth->userId, notificationId));
                callback(upgradedJson(std::move(value)));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/chat/messages/{1}/reaction",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& messageId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                const auto emoji = body && (*body)["emoji"].isString()
                    ? (*body)["emoji"].asString() : "";
                if (emoji.size() > 16) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_reaction",
                                           "Reaction is too long"));
                    return;
                }
                callback(upgradedJson(store_.setMessageReaction(messageId, auth->userId, emoji)));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/chat/messages/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& messageId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                auto result = store_.deleteMessage(messageId, auth->userId);
                if (result.isMember("deletedAttachmentStorageName")) {
                    std::error_code ignored;
                    std::filesystem::remove(
                        config_.mediaRoot / result["deletedAttachmentStorageName"].asString(), ignored);
                    result.removeMember("deletedAttachmentStorageName");
                }
                callback(upgradedJson(std::move(result)));
            } catch (...) { handleException(callback); }
        }, {drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/chat/{1}/settings",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& otherUserId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                const auto body = request->getJsonObject();
                if (!body || !(*body)["muted"].isBool() || !(*body)["archived"].isBool()) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_request",
                                           "muted and archived must be booleans"));
                    return;
                }
                callback(upgradedJson(store_.setConversationSettings(
                    auth->userId, otherUserId, (*body)["muted"].asBool(),
                    (*body)["archived"].asBool())));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/chat/{1}/attachments",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& otherUserId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                drogon::MultiPartParser parser;
                if (parser.parse(request) != 0 || parser.getFiles().size() != 1) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_upload",
                                           "Attach exactly one image"));
                    return;
                }
                const auto& file = parser.getFiles().front();
                const auto type = imageType(file);
                if (!type || file.fileLength() > 10ULL * 1024ULL * 1024ULL) {
                    callback(upgradedError(drogon::k415UnsupportedMediaType, "invalid_attachment",
                                           "Attachment must be JPEG, PNG, or WebP up to 10 MiB"));
                    return;
                }
                const auto mediaId = randomToken(16);
                const auto storageName = "chat-" + mediaId + "." + type->first;
                const auto target = config_.mediaRoot / storageName;
                if (file.saveAs(target.string()) != 0) {
                    callback(upgradedError(drogon::k500InternalServerError, "media_write_failed",
                                           "Attachment could not be stored"));
                    return;
                }
                try {
                    const auto text = trimmed(parser.getParameter<std::string>("message"));
                    if (text.size() > 1'000) {
                        callback(upgradedError(drogon::k400BadRequest, "validation_failed",
                                               "Message must be at most 1,000 bytes"));
                        std::error_code ignored;
                        std::filesystem::remove(target, ignored);
                        return;
                    }
                    const auto created = store_.createMessage(MessageInput{
                        randomToken(16), auth->userId, otherUserId, text,
                        parser.getParameter<std::string>("replyTo"), mediaId, storageName,
                        type->second, file.getFileName()});
                    callback(upgradedJson(created, drogon::k201Created));
                } catch (...) {
                    std::error_code ignored;
                    std::filesystem::remove(target, ignored);
                    throw;
                }
            } catch (...) { handleException(callback); }
        }, {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/chat/media/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& mediaId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                const auto asset = store_.getChatAsset(mediaId, auth->userId);
                if (!asset) {
                    callback(upgradedError(drogon::k404NotFound, "not_found", "Attachment not found"));
                    return;
                }
                auto response = drogon::HttpResponse::newFileResponse(
                    (config_.mediaRoot / asset->storageName).string(), "", drogon::CT_CUSTOM,
                    asset->contentType, request);
                response->addHeader("Cache-Control", "private, max-age=3600");
                callback(response);
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/users/{1}/presence",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& targetId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                Json::Value value(Json::objectValue);
                value["online"] = !store_.isBlocked(auth->userId, targetId) &&
                                  RealtimeHub::online(targetId);
                callback(upgradedJson(std::move(value)));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/admin/stats",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                if (!isAdmin(auth->userId)) {
                    callback(upgradedError(drogon::k403Forbidden, "admin_required",
                                           "Administrator access is required"));
                    return;
                }
                callback(upgradedJson(store_.adminStatistics()));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/admin/reports",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) return;
                if (!isAdmin(auth->userId)) {
                    callback(upgradedError(drogon::k403Forbidden, "admin_required",
                                           "Administrator access is required"));
                    return;
                }
                callback(upgradedJson(store_.listReports(request->getParameter("status"))));
            } catch (...) { handleException(callback); }
        }, {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/admin/reports/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& reportId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                if (!isAdmin(auth->userId)) {
                    callback(upgradedError(drogon::k403Forbidden, "admin_required",
                                           "Administrator access is required"));
                    return;
                }
                const auto body = request->getJsonObject();
                const auto status = body && (*body)["status"].isString()
                    ? (*body)["status"].asString() : "";
                if (status != "open" && status != "reviewed" && status != "resolved" &&
                    status != "dismissed") {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_status",
                                           "Unknown report status"));
                    return;
                }
                callback(upgradedJson(store_.updateReportStatus(reportId, auth->userId, status)));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/admin/users/{1}/suspension",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& targetId) {
            if (request->method() == drogon::Options) { callback(optionsResponse(request)); return; }
            if (!validMutationOrigin(request, callback)) return;
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) return;
                if (!isAdmin(auth->userId)) {
                    callback(upgradedError(drogon::k403Forbidden, "admin_required",
                                           "Administrator access is required"));
                    return;
                }
                if (targetId == auth->userId) {
                    callback(upgradedError(drogon::k409Conflict, "conflict",
                                           "Administrators cannot suspend themselves"));
                    return;
                }
                const auto body = request->getJsonObject();
                if (!body || !(*body)["suspended"].isBool()) {
                    callback(upgradedError(drogon::k400BadRequest, "invalid_request",
                                           "suspended must be true or false"));
                    return;
                }
                callback(upgradedJson(store_.setUserSuspended(
                    targetId, auth->userId, (*body)["suspended"].asBool())));
            } catch (...) { handleException(callback); }
        }, {drogon::Put, drogon::Options});
}

}  // namespace tiktok
