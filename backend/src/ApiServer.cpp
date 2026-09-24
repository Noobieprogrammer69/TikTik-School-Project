#include "tiktok/ApiServer.h"

#include "tiktok/Validation.h"

#include <drogon/Cookie.h>
#include <drogon/MultiPart.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <system_error>

namespace tiktok {
namespace {

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

drogon::HttpResponsePtr jsonResponse(Json::Value data,
                                     drogon::HttpStatusCode status = drogon::k200OK) {
    Json::Value envelope(Json::objectValue);
    envelope["data"] = std::move(data);
    auto response = drogon::HttpResponse::newHttpJsonResponse(envelope);
    response->setStatusCode(status);
    response->addHeader("Cache-Control", "no-store");
    return response;
}

drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode status,
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

std::string contentTypeFor(const drogon::HttpFile& file) {
    switch (file.getContentType()) {
        case drogon::CT_VIDEO_MP4:
            return "video/mp4";
        case drogon::CT_VIDEO_WEBM:
            return "video/webm";
        case drogon::CT_VIDEO_OGG:
            return "video/ogg";
        default:
            break;
    }

    const auto extension = std::string(file.getFileExtension());
    if (extension == "mp4" || extension == ".mp4") {
        return "video/mp4";
    }
    if (extension == "webm" || extension == ".webm") {
        return "video/webm";
    }
    if (extension == "ogg" || extension == ".ogg" || extension == "ogv" || extension == ".ogv") {
        return "video/ogg";
    }
    return "application/octet-stream";
}

bool validIdentifier(const std::string& id) {
    return !id.empty() && id.size() <= 200 && id.find('/') == std::string::npos &&
           id.find('\0') == std::string::npos;
}

std::int64_t epochSeconds(std::chrono::system_clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

std::string quotedPath(const std::filesystem::path& path) {
#ifdef _WIN32
    auto value = path.string();
    std::replace(value.begin(), value.end(), '"', '_');
    return "\"" + value + "\"";
#else
    auto value = path.string();
    std::string escaped{"'"};
    for (const auto character : value) {
        if (character == '\'') escaped += "'\\''";
        else escaped.push_back(character);
    }
    return escaped + "'";
#endif
}

bool runFfmpeg(const std::string& binary, const std::string& arguments) {
    if (binary.empty() || binary.find_first_of("\r\n") != std::string::npos) return false;
    const auto command = quotedPath(binary) + " -hide_banner -loglevel error -y " + arguments;
    return std::system(command.c_str()) == 0;
}

}  // namespace

ApiServer::ApiServer(AppConfig config, MongoStore& store)
    : config_(std::move(config)),
      store_(store),
      googleVerifier_(config_.googleClientId),
      loginRateLimiter_(config_.loginRateLimit,
                        std::chrono::seconds{config_.loginRateWindowSeconds}) {}

bool ApiServer::validMutationOrigin(const drogon::HttpRequestPtr& request,
                                    const ResponseCallback& callback) const {
    const auto origin = request->getHeader("Origin");
    if (!origin.empty() && origin != config_.frontendOrigin) {
        callback(errorResponse(drogon::k403Forbidden, "origin_forbidden",
                               "The request origin is not allowed"));
        return false;
    }
    return true;
}

drogon::HttpResponsePtr ApiServer::optionsResponse(const drogon::HttpRequestPtr& request) const {
    auto response = drogon::HttpResponse::newHttpResponse(drogon::k204NoContent, drogon::CT_NONE);
    const auto origin = request->getHeader("Origin");
    if (origin == config_.frontendOrigin) {
        response->addHeader("Access-Control-Allow-Origin", origin);
        response->addHeader("Access-Control-Allow-Credentials", "true");
        response->addHeader("Access-Control-Allow-Methods", "GET, HEAD, POST, PUT, DELETE, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-CSRF-Token, X-Test-Auth-Key");
        response->addHeader("Access-Control-Max-Age", "600");
        response->addHeader("Vary", "Origin");
    }
    return response;
}

std::optional<ApiServer::AuthContext> ApiServer::authenticate(
    const drogon::HttpRequestPtr& request, const ResponseCallback& callback, bool requireCsrf) {
    const auto rawToken = request->getCookie("tt_session");
    if (rawToken.empty()) {
        callback(errorResponse(drogon::k401Unauthorized, "authentication_required",
                               "Log in to continue"));
        return std::nullopt;
    }

    const auto tokenHash = sha256Hex(rawToken);
    const auto session = store_.getSession(tokenHash);
    if (!session) {
        callback(errorResponse(drogon::k401Unauthorized, "session_invalid",
                               "Your session is invalid or expired; please log in again"));
        return std::nullopt;
    }
    if (store_.isSuspended(session->userId)) {
        callback(errorResponse(drogon::k403Forbidden, "account_suspended",
                               "This account has been suspended"));
        return std::nullopt;
    }
    if (requireCsrf) {
        const auto csrfToken = request->getHeader("X-CSRF-Token");
        if (csrfToken.empty() || !secureEquals(sha256Hex(csrfToken), session->csrfHash)) {
            callback(errorResponse(drogon::k403Forbidden, "csrf_invalid",
                                   "Security token is missing or stale; refresh and try again"));
            return std::nullopt;
        }
    }
    return AuthContext{tokenHash, session->userId, session->csrfHash, session->expiresAt};
}

drogon::HttpResponsePtr ApiServer::sessionResponse(
    const Json::Value& user,
    const std::string& rawSessionToken,
    const std::string& csrfToken,
    std::chrono::system_clock::time_point expiresAt,
    bool setCookie) const {
    Json::Value data(Json::objectValue);
    auto responseUser = user;
    responseUser["isAdmin"] = isAdmin(user["_id"].asString());
    data["user"] = std::move(responseUser);
    data["csrfToken"] = csrfToken;
    data["expiresAt"] = static_cast<Json::Int64>(epochSeconds(expiresAt));
    auto response = jsonResponse(std::move(data));

    if (setCookie) {
        drogon::Cookie cookie("tt_session", rawSessionToken);
        cookie.setPath("/");
        cookie.setHttpOnly(true);
        cookie.setSecure(config_.secureCookies);
        cookie.setSameSite(drogon::Cookie::SameSite::kLax);
        cookie.setMaxAge(static_cast<int>(
            std::min<std::uint32_t>(config_.sessionTtlSeconds,
                                    static_cast<std::uint32_t>(std::numeric_limits<int>::max()))));
        response->addCookie(std::move(cookie));
    }
    return response;
}

void ApiServer::establishSession(const UserIdentity& identity, const ResponseCallback& callback) {
    const auto user = store_.upsertUser(identity);
    if (user["suspended"].asBool()) {
        throw StoreError(StoreError::Kind::kForbidden, "This account has been suspended");
    }
    const auto rawSessionToken = randomToken();
    const auto csrfToken = randomToken();
    const auto expiresAt = std::chrono::system_clock::now() +
                           std::chrono::seconds{config_.sessionTtlSeconds};
    store_.createSession(
        SessionRecord{sha256Hex(rawSessionToken), identity.id, sha256Hex(csrfToken), expiresAt});
    store_.recordSecurityEvent(identity.id, "login", identity.provider + " sign-in");
    callback(sessionResponse(user, rawSessionToken, csrfToken, expiresAt, true));
}

void ApiServer::handleException(const ResponseCallback& callback) const {
    try {
        throw;
    } catch (const StoreError& error) {
        switch (error.kind()) {
            case StoreError::Kind::kNotFound:
                callback(errorResponse(drogon::k404NotFound, "not_found", error.what()));
                return;
            case StoreError::Kind::kForbidden:
                callback(errorResponse(drogon::k403Forbidden, "forbidden", error.what()));
                return;
            case StoreError::Kind::kConflict:
                callback(errorResponse(drogon::k409Conflict, "conflict", error.what()));
                return;
            case StoreError::Kind::kDatabase:
                LOG_ERROR << error.what();
                callback(errorResponse(drogon::k503ServiceUnavailable, "database_unavailable",
                                       "The database is temporarily unavailable"));
                return;
        }
    } catch (const std::exception& error) {
        LOG_ERROR << error.what();
        callback(errorResponse(drogon::k500InternalServerError, "internal_error",
                               "The server could not complete the request"));
    }
}

void ApiServer::registerRoutes() {
    auto& app = drogon::app();

    app.registerPreSendingAdvice([origin = config_.frontendOrigin](
                                     const drogon::HttpRequestPtr& request,
                                     const drogon::HttpResponsePtr& response) {
        const auto requestOrigin = request->getHeader("Origin");
        if (!requestOrigin.empty() && requestOrigin == origin) {
            response->addHeader("Access-Control-Allow-Origin", requestOrigin);
            response->addHeader("Access-Control-Allow-Credentials", "true");
            response->addHeader("Vary", "Origin");
        }
        response->addHeader("X-Content-Type-Options", "nosniff");
        response->addHeader("Referrer-Policy", "same-origin");
    });

    app.registerHandler(
        "/api/health",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                store_.ping();
                Json::Value data(Json::objectValue);
                data["status"] = "ok";
                data["database"] = "connected";
                callback(jsonResponse(std::move(data)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/config/public",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            Json::Value data(Json::objectValue);
            data["googleClientId"] = config_.googleClientId;
            data["maxUploadBytes"] = static_cast<Json::UInt64>(config_.maxUploadBytes);
            data["mediaProcessing"] = config_.enableMediaProcessing;
            callback(jsonResponse(std::move(data)));
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/auth/google",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!loginRateLimiter_.allow(request->peerAddr().toIp(), std::chrono::steady_clock::now())) {
                callback(errorResponse(drogon::k429TooManyRequests, "login_rate_limited",
                                       "Too many login attempts; wait a few minutes and try again"));
                return;
            }
            const auto body = request->getJsonObject();
            if (!body || !(*body)["credential"].isString() ||
                (*body)["credential"].asString().size() > 16'384) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_request",
                                       "A valid Google credential is required"));
                return;
            }
            const auto credential = (*body)["credential"].asString();
            googleVerifier_.verifyAsync(
                credential,
                [this, callback = std::move(callback)](
                    std::optional<GoogleClaims> claims, std::string error) mutable {
                    if (!claims) {
                        callback(errorResponse(drogon::k401Unauthorized, "google_credential_invalid", error));
                        return;
                    }
                    try {
                        establishSession(UserIdentity{claims->subject, claims->name, claims->picture,
                                                      "google", claims->subject, claims->emailVerified},
                                         callback);
                    } catch (...) {
                        handleException(callback);
                    }
                });
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/auth/session",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) {
                    return;
                }
                const auto user = store_.getUser(auth->userId);
                if (!user) {
                    callback(errorResponse(drogon::k401Unauthorized, "user_missing",
                                           "The account for this session no longer exists"));
                    return;
                }
                const auto csrfToken = randomToken();
                store_.rotateCsrf(auth->tokenHash, sha256Hex(csrfToken));
                callback(sessionResponse(*user, "", csrfToken, auth->expiresAt, false));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/auth/logout",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                store_.revokeSession(auth->tokenHash);
                store_.recordSecurityEvent(auth->userId, "logout", "Session signed out");
                Json::Value data(Json::objectValue);
                data["loggedOut"] = true;
                auto response = jsonResponse(std::move(data));
                drogon::Cookie cookie("tt_session", "deleted");
                cookie.setPath("/");
                cookie.setHttpOnly(true);
                cookie.setSecure(config_.secureCookies);
                cookie.setSameSite(drogon::Cookie::SameSite::kLax);
                cookie.setMaxAge(0);
                cookie.setExpiresDate(trantor::Date{0});
                response->addCookie(std::move(cookie));
                callback(response);
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/test/login",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!config_.enableTestAuth) {
                callback(errorResponse(drogon::k404NotFound, "not_found", "Route not found"));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!secureEquals(request->getHeader("X-Test-Auth-Key"), config_.testAuthKey)) {
                callback(errorResponse(drogon::k403Forbidden, "test_auth_forbidden",
                                       "Test authentication key is invalid"));
                return;
            }
            const auto body = request->getJsonObject();
            const auto suffix = body && (*body)["id"].isString() ? (*body)["id"].asString() : "student";
            const auto name = body && (*body)["name"].isString() ? (*body)["name"].asString() : "Test Student";
            if (!validIdentifier(suffix) || name.empty() || name.size() > 100) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_request", "Invalid test user"));
                return;
            }
            try {
                establishSession(UserIdentity{"test:" + suffix, name,
                                              "/avatar.svg", "test", "test:" + suffix, true},
                                 callback);
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/users",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                callback(jsonResponse(store_.listUsers()));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/users/{1}/relationship",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& targetUserId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validIdentifier(targetUserId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "User ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) {
                    return;
                }
                callback(jsonResponse(store_.getRelationship(auth->userId, targetUserId)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/users/{1}/follow",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& targetUserId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(targetUserId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "User ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                const auto body = request->getJsonObject();
                if (!body || !(*body)["follow"].isBool()) {
                    callback(errorResponse(drogon::k400BadRequest, "invalid_request",
                                           "The follow field must be true or false"));
                    return;
                }
                callback(jsonResponse(store_.setFollow(
                    auth->userId, targetUserId, (*body)["follow"].asBool())));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/users/{1}/report",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& targetUserId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(targetUserId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "User ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                const auto body = request->getJsonObject();
                const auto reason = validateReportReason(
                    body && (*body)["reason"].isString() ? (*body)["reason"].asString() : "");
                const auto details = validateReportDetails(
                    body && (*body)["details"].isString() ? (*body)["details"].asString() : "");
                if (!reason.ok || !details.ok) {
                    callback(errorResponse(drogon::k400BadRequest, "validation_failed",
                                           !reason.ok ? reason.error : details.error));
                    return;
                }
                callback(jsonResponse(store_.createReport(ReportInput{
                    randomToken(16), auth->userId, targetUserId, reason.value, details.value}),
                    drogon::k201Created));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/notifications/read",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                Json::Value result(Json::objectValue);
                result["read"] = true;
                result["updatedCount"] = static_cast<Json::UInt64>(
                    store_.markNotificationsRead(auth->userId));
                callback(jsonResponse(std::move(result)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/notifications",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) {
                    return;
                }
                callback(jsonResponse(store_.listNotifications(auth->userId)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/posts",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                std::optional<std::string> topic;
                const auto requestedTopic = request->getParameter("topic");
                if (!requestedTopic.empty()) {
                    const auto checked = validateTopic(requestedTopic);
                    if (!checked.ok) {
                        callback(errorResponse(drogon::k400BadRequest, "invalid_topic", checked.error));
                        return;
                    }
                    topic = checked.value;
                }
                callback(jsonResponse(store_.listPosts(topic)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validIdentifier(id)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "Post ID is invalid"));
                return;
            }
            try {
                if (request->method() == drogon::Delete) {
                    if (!validMutationOrigin(request, callback)) {
                        return;
                    }
                    const auto auth = authenticate(request, callback, true);
                    if (!auth) {
                        return;
                    }
                    const auto media = store_.deletePost(id, auth->userId);
                    std::error_code removeError;
                    std::filesystem::remove(config_.mediaRoot / media.storageName, removeError);
                    if (removeError) {
                        LOG_WARN << "Post deleted but media cleanup failed: " << removeError.message();
                    }
                    for (const auto& extra : {media.thumbnailStorageName, media.subtitleStorageName}) {
                        if (!extra.empty()) {
                            std::error_code ignored;
                            std::filesystem::remove(config_.mediaRoot / extra, ignored);
                        }
                    }
                    for (const auto& quality : media.qualities) {
                        std::error_code ignored;
                        std::filesystem::remove(config_.mediaRoot /
                            (media.id + "-" + quality + ".mp4"), ignored);
                    }
                    Json::Value data(Json::objectValue);
                    data["deleted"] = true;
                    callback(jsonResponse(std::move(data)));
                    return;
                }
                const auto post = store_.getPost(id);
                if (!post) {
                    callback(errorResponse(drogon::k404NotFound, "not_found", "Post not found"));
                    return;
                }
                std::string viewerId;
                const auto rawToken = request->getCookie("tt_session");
                if (!rawToken.empty()) {
                    const auto session = store_.getSession(sha256Hex(rawToken));
                    if (session) viewerId = session->userId;
                }
                if (!store_.canViewPost(id, viewerId)) {
                    callback(errorResponse(drogon::k403Forbidden, "private_content",
                                           "Follow this private account to view the video"));
                    return;
                }
                callback(jsonResponse(*post));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Delete, drogon::Options});

    app.registerHandler(
        "/api/videos",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                drogon::MultiPartParser parser;
                if (parser.parse(request) != 0 || parser.getFiles().empty() ||
                    parser.getFiles().size() > 3) {
                    callback(errorResponse(drogon::k400BadRequest, "invalid_upload",
                                           "Upload one video, an optional cover, and optional WebVTT subtitles"));
                    return;
                }
                const auto caption = validateCaption(parser.getParameter<std::string>("caption"));
                const auto topic = validateTopic(parser.getParameter<std::string>("topic"));
                if (!caption.ok || !topic.ok) {
                    callback(errorResponse(drogon::k400BadRequest, "validation_failed",
                                           !caption.ok ? caption.error : topic.error));
                    return;
                }

                const drogon::HttpFile* videoFile = nullptr;
                const drogon::HttpFile* subtitleFile = nullptr;
                const drogon::HttpFile* coverFile = nullptr;
                for (const auto& candidate : parser.getFiles()) {
                    if (candidate.getItemName() == "subtitles") subtitleFile = &candidate;
                    else if (candidate.getItemName() == "cover") coverFile = &candidate;
                    else if (candidate.getItemName() == "video" || videoFile == nullptr) videoFile = &candidate;
                }
                if (!videoFile) {
                    callback(errorResponse(drogon::k400BadRequest, "invalid_upload",
                                           "A video file is required"));
                    return;
                }
                const auto& file = *videoFile;
                const auto mediaValidation = validateVideo(file.getFileName(), contentTypeFor(file),
                                                           file.fileContent(), config_.maxUploadBytes);
                if (!mediaValidation.ok) {
                    callback(errorResponse(drogon::k415UnsupportedMediaType, "invalid_video",
                                           mediaValidation.error));
                    return;
                }

                const auto postId = randomToken(16);
                const auto mediaId = randomToken(16);
                const auto storageName = mediaId + "." + mediaValidation.extension;
                const auto target = config_.mediaRoot / storageName;
                if (file.saveAs(target.string()) != 0) {
                    callback(errorResponse(drogon::k500InternalServerError, "media_write_failed",
                                           "The video could not be stored"));
                    return;
                }
                auto finalStorageName = storageName;
                auto finalContentType = mediaValidation.contentType;
                auto finalSize = file.fileLength();
                auto finalTarget = target;
                if (config_.enableMediaProcessing && mediaValidation.extension != "mp4") {
                    const auto processedName = mediaId + "-standard.mp4";
                    const auto processedTarget = config_.mediaRoot / processedName;
                    if (runFfmpeg(config_.ffmpegBinary,
                                  "-i " + quotedPath(target) +
                                  " -movflags +faststart -c:v libx264 -preset veryfast -crf 24"
                                  " -c:a aac -b:a 128k " + quotedPath(processedTarget))) {
                        std::error_code sizeError;
                        const auto processedSize = std::filesystem::file_size(processedTarget, sizeError);
                        if (!sizeError && processedSize > 0) {
                            std::error_code ignored;
                            std::filesystem::remove(target, ignored);
                            finalStorageName = processedName;
                            finalContentType = "video/mp4";
                            finalSize = static_cast<std::size_t>(processedSize);
                            finalTarget = processedTarget;
                        }
                    }
                }

                std::vector<std::string> qualities;
                if (config_.enableMediaProcessing) {
                    for (const auto& quality : {std::string{"360"}, std::string{"720"}}) {
                        const auto variant = config_.mediaRoot / (mediaId + "-" + quality + ".mp4");
                        if (runFfmpeg(config_.ffmpegBinary,
                                      "-i " + quotedPath(finalTarget) + " -vf scale=-2:" + quality +
                                      " -movflags +faststart -c:v libx264 -preset veryfast -crf 26"
                                      " -c:a aac -b:a 96k " + quotedPath(variant))) {
                            qualities.push_back(quality);
                        } else {
                            std::error_code ignored;
                            std::filesystem::remove(variant, ignored);
                        }
                    }
                }

                std::string thumbnailId;
                std::string thumbnailStorageName;
                std::filesystem::path thumbnailTarget;
                if (coverFile) {
                    const auto cover = coverFile->fileContent();
                    const bool jpeg = cover.size() >= 3 &&
                                      static_cast<unsigned char>(cover[0]) == 0xff &&
                                      static_cast<unsigned char>(cover[1]) == 0xd8 &&
                                      static_cast<unsigned char>(cover[2]) == 0xff;
                    const bool png = cover.size() >= 8 && cover.substr(0, 8) == "\x89PNG\r\n\x1a\n";
                    const bool webp = cover.size() >= 12 && cover.substr(0, 4) == "RIFF" &&
                                      cover.substr(8, 4) == "WEBP";
                    if (cover.empty() || cover.size() > 5ULL * 1024ULL * 1024ULL ||
                        (!jpeg && !png && !webp)) {
                        std::error_code ignored;
                        std::filesystem::remove(finalTarget, ignored);
                        for (const auto& quality : qualities) {
                            std::filesystem::remove(config_.mediaRoot /
                                (mediaId + "-" + quality + ".mp4"), ignored);
                        }
                        callback(errorResponse(drogon::k415UnsupportedMediaType, "invalid_cover",
                                               "Cover must be a JPEG, PNG, or WebP image up to 5 MiB"));
                        return;
                    }
                    thumbnailId = randomToken(16);
                    const auto extension = jpeg ? "jpg" : png ? "png" : "webp";
                    thumbnailStorageName = thumbnailId + "." + extension;
                    thumbnailTarget = config_.mediaRoot / thumbnailStorageName;
                    if (coverFile->saveAs(thumbnailTarget.string()) != 0) {
                        std::error_code ignored;
                        std::filesystem::remove(finalTarget, ignored);
                        for (const auto& quality : qualities) {
                            std::filesystem::remove(config_.mediaRoot /
                                (mediaId + "-" + quality + ".mp4"), ignored);
                        }
                        callback(errorResponse(drogon::k500InternalServerError, "media_write_failed",
                                               "The cover could not be stored"));
                        return;
                    }
                } else if (config_.enableMediaProcessing) {
                    thumbnailId = randomToken(16);
                    thumbnailStorageName = thumbnailId + ".jpg";
                    thumbnailTarget = config_.mediaRoot / thumbnailStorageName;
                    if (!runFfmpeg(config_.ffmpegBinary,
                                   "-ss 0.1 -i " + quotedPath(finalTarget) +
                                   " -frames:v 1 -vf scale=480:-2 " + quotedPath(thumbnailTarget))) {
                        std::error_code ignored;
                        std::filesystem::remove(thumbnailTarget, ignored);
                        thumbnailId.clear();
                        thumbnailStorageName.clear();
                    }
                }

                std::string subtitleId;
                std::string subtitleStorageName;
                std::filesystem::path subtitleTarget;
                if (subtitleFile) {
                    const auto contents = subtitleFile->fileContent();
                    const auto extension = std::string(subtitleFile->getFileExtension());
                    if (contents.size() > 1024ULL * 1024ULL ||
                        (extension != "vtt" && extension != ".vtt") ||
                        !contents.starts_with("WEBVTT")) {
                        std::error_code ignored;
                        std::filesystem::remove(finalTarget, ignored);
                        std::filesystem::remove(thumbnailTarget, ignored);
                        callback(errorResponse(drogon::k415UnsupportedMediaType, "invalid_subtitles",
                                               "Subtitles must be a WebVTT file up to 1 MiB"));
                        return;
                    }
                    subtitleId = randomToken(16);
                    subtitleStorageName = subtitleId + ".vtt";
                    subtitleTarget = config_.mediaRoot / subtitleStorageName;
                    if (subtitleFile->saveAs(subtitleTarget.string()) != 0) {
                        std::error_code ignored;
                        std::filesystem::remove(finalTarget, ignored);
                        std::filesystem::remove(thumbnailTarget, ignored);
                        callback(errorResponse(drogon::k500InternalServerError, "media_write_failed",
                                               "The subtitle file could not be stored"));
                        return;
                    }
                }
                try {
                    auto post = store_.createPost(PostInput{postId, auth->userId, caption.value, topic.value,
                                                           mediaId, finalStorageName, file.getFileName(),
                                                           finalContentType, finalSize,
                                                           thumbnailId, thumbnailStorageName,
                                                           subtitleId, subtitleStorageName,
                                                           qualities});
                    callback(jsonResponse(std::move(post), drogon::k201Created));
                } catch (...) {
                    std::error_code ignored;
                    std::filesystem::remove(finalTarget, ignored);
                    std::filesystem::remove(thumbnailTarget, ignored);
                    std::filesystem::remove(subtitleTarget, ignored);
                    for (const auto& quality : qualities) {
                        std::filesystem::remove(config_.mediaRoot /
                            (mediaId + "-" + quality + ".mp4"), ignored);
                    }
                    throw;
                }
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/like",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(id)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "Post ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                const auto body = request->getJsonObject();
                if (!body || !(*body)["like"].isBool()) {
                    callback(errorResponse(drogon::k400BadRequest, "invalid_request",
                                           "The like field must be true or false"));
                    return;
                }
                callback(jsonResponse(store_.setLike(id, auth->userId, (*body)["like"].asBool())));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/comments",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(id)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "Post ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                const auto body = request->getJsonObject();
                const auto comment = validateComment(body && (*body)["comment"].isString()
                                                         ? (*body)["comment"].asString()
                                                         : "");
                if (!comment.ok) {
                    callback(errorResponse(drogon::k400BadRequest, "validation_failed", comment.error));
                    return;
                }
                callback(jsonResponse(
                    store_.addComment(id, auth->userId, comment.value, randomToken(12))));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/comments/{2}/like",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId, const std::string& commentId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(postId) || !validIdentifier(commentId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id",
                                       "Post or comment ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                const auto body = request->getJsonObject();
                if (!body || !(*body)["like"].isBool()) {
                    callback(errorResponse(drogon::k400BadRequest, "invalid_request",
                                           "The like field must be true or false"));
                    return;
                }
                callback(jsonResponse(store_.setCommentLike(
                    postId, commentId, auth->userId, (*body)["like"].asBool())));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/comments/{2}/replies",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId, const std::string& commentId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(postId) || !validIdentifier(commentId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id",
                                       "Post or comment ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                const auto body = request->getJsonObject();
                const auto reply = validateComment(
                    body && (*body)["reply"].isString() ? (*body)["reply"].asString() : "");
                if (!reply.ok) {
                    callback(errorResponse(drogon::k400BadRequest, "validation_failed", reply.error));
                    return;
                }
                callback(jsonResponse(store_.addReply(
                    postId, commentId, auth->userId, reply.value, randomToken(12))));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/posts/{1}/comments/{2}/pin",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& postId, const std::string& commentId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(postId) || !validIdentifier(commentId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id",
                                       "Post or comment ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                const auto body = request->getJsonObject();
                if (!body || !(*body)["pinned"].isBool()) {
                    callback(errorResponse(drogon::k400BadRequest, "invalid_request",
                                           "The pinned field must be true or false"));
                    return;
                }
                callback(jsonResponse(store_.setCommentPinned(
                    postId, commentId, auth->userId, (*body)["pinned"].asBool())));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/profiles/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validIdentifier(id)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "User ID is invalid"));
                return;
            }
            try {
                std::string viewerId;
                const auto token = request->getCookie("tt_session");
                if (!token.empty()) {
                    const auto session = store_.getSession(sha256Hex(token));
                    if (session) viewerId = session->userId;
                }
                callback(jsonResponse(store_.getProfile(id, viewerId)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/chat/conversations",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, false);
                if (!auth) {
                    return;
                }
                callback(jsonResponse(store_.listConversations(auth->userId)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/chat/{1}/messages",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& otherUserId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validIdentifier(otherUserId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id",
                                       "Chat user ID is invalid"));
                return;
            }
            if (request->method() == drogon::Post &&
                !validMutationOrigin(request, callback)) {
                return;
            }
            try {
                const auto auth = authenticate(request, callback, request->method() == drogon::Post);
                if (!auth) {
                    return;
                }
                if (request->method() == drogon::Get) {
                    callback(jsonResponse(store_.listMessages(auth->userId, otherUserId)));
                    return;
                }

                const auto body = request->getJsonObject();
                const auto message = validateMessage(body && (*body)["message"].isString()
                                                         ? (*body)["message"].asString()
                                                         : "");
                if (!message.ok) {
                    callback(errorResponse(drogon::k400BadRequest, "validation_failed",
                                           message.error));
                    return;
                }
                const auto replyTo = body && (*body)["replyTo"].isString()
                                         ? (*body)["replyTo"].asString()
                                         : "";
                callback(jsonResponse(
                    store_.createMessage(MessageInput{randomToken(16), auth->userId,
                                                      otherUserId, message.value, replyTo}),
                    drogon::k201Created));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Post, drogon::Options});

    app.registerHandler(
        "/api/chat/{1}/read",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback,
               const std::string& otherUserId) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validMutationOrigin(request, callback)) {
                return;
            }
            if (!validIdentifier(otherUserId)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id",
                                       "Chat user ID is invalid"));
                return;
            }
            try {
                const auto auth = authenticate(request, callback, true);
                if (!auth) {
                    return;
                }
                Json::Value result(Json::objectValue);
                result["read"] = true;
                result["updatedCount"] = static_cast<Json::UInt64>(
                    store_.markConversationRead(auth->userId, otherUserId));
                callback(jsonResponse(std::move(result)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Put, drogon::Options});

    app.registerHandler(
        "/api/search",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            const auto term = request->getParameter("q");
            if (term.empty() || term.size() > 100) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_search",
                                       "Search text must be between 1 and 100 characters"));
                return;
            }
            try {
                callback(jsonResponse(store_.search(term)));
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    app.registerHandler(
        "/api/media/{1}",
        [this](const drogon::HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            if (request->method() == drogon::Options) {
                callback(optionsResponse(request));
                return;
            }
            if (!validIdentifier(id)) {
                callback(errorResponse(drogon::k400BadRequest, "invalid_id", "Media ID is invalid"));
                return;
            }
            try {
                std::string viewerId;
                const auto rawToken = request->getCookie("tt_session");
                if (!rawToken.empty()) {
                    const auto session = store_.getSession(sha256Hex(rawToken));
                    if (session) viewerId = session->userId;
                }
                const auto media = store_.getMedia(id);
                if (!media) {
                    callback(errorResponse(drogon::k404NotFound, "not_found", "Video not found"));
                    return;
                }
                if (!store_.canViewMedia(id, viewerId)) {
                    callback(errorResponse(drogon::k403Forbidden, "private_content",
                                           "This media belongs to a private account"));
                    return;
                }
                auto storageName = media->storageName;
                auto responseContentType = media->contentType;
                const auto quality = request->getParameter("quality");
                if (!quality.empty() &&
                    std::find(media->qualities.begin(), media->qualities.end(), quality) !=
                        media->qualities.end()) {
                    storageName = media->id + "-" + quality + ".mp4";
                    responseContentType = "video/mp4";
                }
                const auto path = config_.mediaRoot / storageName;
                std::error_code fileError;
                const auto fileSize = std::filesystem::file_size(path, fileError);
                if (fileError) {
                    callback(errorResponse(drogon::k404NotFound, "media_missing",
                                           "Video file is missing from storage"));
                    return;
                }

                const auto range = parseByteRange(request->getHeader("Range"),
                                                  static_cast<std::size_t>(fileSize));
                if (range.status == RangeStatus::kMalformed ||
                    range.status == RangeStatus::kUnsatisfiable) {
                    auto response = errorResponse(drogon::k416RequestedRangeNotSatisfiable,
                                                  "range_not_satisfiable",
                                                  "Requested video byte range is invalid");
                    response->addHeader("Content-Range", "bytes */" + std::to_string(fileSize));
                    callback(response);
                    return;
                }

                drogon::HttpResponsePtr response;
                if (range.status == RangeStatus::kValid) {
                    response = drogon::HttpResponse::newFileResponse(
                        path.string(), range.start, range.end - range.start + 1, true, "",
                        drogon::CT_CUSTOM, responseContentType, request);
                    response->setStatusCode(drogon::k206PartialContent);
                } else {
                    response = drogon::HttpResponse::newFileResponse(
                        path.string(), "", drogon::CT_CUSTOM, responseContentType, request);
                }
                response->addHeader("Accept-Ranges", "bytes");
                response->addHeader("Cache-Control", "private, max-age=3600");
                callback(response);
            } catch (...) {
                handleException(callback);
            }
        },
        {drogon::Get, drogon::Options});

    registerUpgradeRoutes();
    registerNextRoutes();
}

}  // namespace tiktok
