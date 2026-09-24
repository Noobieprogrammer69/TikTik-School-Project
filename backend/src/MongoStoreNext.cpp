#include "tiktok/MongoStore.h"

#include "tiktok/RealtimeHub.h"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/update.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace tiktok {
namespace {

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;

std::string fieldString(const bsoncxx::document::view& document,
                        std::string_view key,
                        std::string fallback = {}) {
    const auto value = document[key];
    return value && value.type() == bsoncxx::type::k_string
               ? std::string(value.get_string().value)
               : std::move(fallback);
}

std::int64_t fieldInteger(const bsoncxx::document::view& document,
                          std::string_view key,
                          std::int64_t fallback = 0) {
    const auto value = document[key];
    if (!value) return fallback;
    if (value.type() == bsoncxx::type::k_int64) return value.get_int64().value;
    if (value.type() == bsoncxx::type::k_int32) return value.get_int32().value;
    return fallback;
}

bool fieldBool(const bsoncxx::document::view& document,
               std::string_view key,
               bool fallback = false) {
    const auto value = document[key];
    return value && value.type() == bsoncxx::type::k_bool
               ? value.get_bool().value
               : fallback;
}

std::string isoDate(const bsoncxx::document::view& document, std::string_view key) {
    const auto value = document[key];
    if (!value || value.type() != bsoncxx::type::k_date) return {};
    const auto point = std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            value.get_date().value)};
    const auto raw = std::chrono::system_clock::to_time_t(point);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string pairKey(std::string_view prefix,
                    const std::string& first,
                    const std::string& second) {
    return std::string(prefix) + ":" + std::to_string(first.size()) + ":" + first + second;
}

std::string chatKey(const std::string& first, const std::string& second) {
    const auto& left = first < second ? first : second;
    const auto& right = first < second ? second : first;
    return std::to_string(left.size()) + ":" + left + right;
}

std::string escapePattern(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (const auto character : value) {
        if (std::string_view{"\\.^$|()[]{}*+?"}.find(character) != std::string_view::npos) {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

Json::Value privacyDefaults() {
    Json::Value result(Json::objectValue);
    result["privateAccount"] = false;
    result["allowMessages"] = "everyone";
    result["allowComments"] = "everyone";
    result["showLikedVideos"] = true;
    return result;
}

StoreError nextDatabaseError(const std::exception& error) {
    return StoreError(StoreError::Kind::kDatabase,
                      std::string("MongoDB operation failed: ") + error.what());
}

}  // namespace

bool MongoStore::canViewPost(const std::string& postId, const std::string& viewerId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto post = database["posts"].find_one(make_document(kvp("_id", postId)));
        if (!post) return false;
        const auto ownerId = fieldString(post->view(), "userId");
        if (viewerId == ownerId) return true;
        const auto owner = database["users"].find_one(make_document(kvp("_id", ownerId)));
        if (!owner) return false;
        const auto privacy = owner->view()["privacy"];
        const bool privateAccount = privacy && privacy.type() == bsoncxx::type::k_document &&
                                    fieldBool(privacy.get_document().view(), "privateAccount");
        if (!privateAccount) return true;
        if (viewerId.empty()) return false;
        return static_cast<bool>(database["follows"].find_one(
            make_document(kvp("followerId", viewerId), kvp("followingId", ownerId))));
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

bool MongoStore::canViewMedia(const std::string& mediaId, const std::string& viewerId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto post = database["posts"].find_one(make_document(kvp("$or", make_array(
            make_document(kvp("media.id", mediaId)), make_document(kvp("thumbnail.id", mediaId)),
            make_document(kvp("subtitles.id", mediaId))))));
        if (!post) return false;
        const auto ownerId = fieldString(post->view(), "userId");
        if (viewerId == ownerId) return true;
        const auto owner = database["users"].find_one(make_document(kvp("_id", ownerId)));
        if (!owner) return false;
        const auto privacy = owner->view()["privacy"];
        const bool privateAccount = privacy && privacy.type() == bsoncxx::type::k_document &&
                                    fieldBool(privacy.get_document().view(), "privateAccount");
        if (!privateAccount) return true;
        return !viewerId.empty() && static_cast<bool>(database["follows"].find_one(
            make_document(kvp("followerId", viewerId), kvp("followingId", ownerId))));
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::getPrivacySettings(const std::string& userId) {
    auto result = privacyDefaults();
    try {
        auto client = pool_.acquire();
        const auto user = (*client)[database_]["users"].find_one(make_document(kvp("_id", userId)));
        if (!user) throw StoreError(StoreError::Kind::kNotFound, "User not found");
        const auto privacy = user->view()["privacy"];
        if (privacy && privacy.type() == bsoncxx::type::k_document) {
            const auto value = privacy.get_document().view();
            result["privateAccount"] = fieldBool(value, "privateAccount");
            result["allowMessages"] = fieldString(value, "allowMessages", "everyone");
            result["allowComments"] = fieldString(value, "allowComments", "everyone");
            result["showLikedVideos"] = fieldBool(value, "showLikedVideos", true);
        }
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::updatePrivacySettings(const std::string& userId,
                                               const Json::Value& settings) {
    auto normalized = privacyDefaults();
    if (settings["privateAccount"].isBool()) {
        normalized["privateAccount"] = settings["privateAccount"];
    }
    if (settings["showLikedVideos"].isBool()) {
        normalized["showLikedVideos"] = settings["showLikedVideos"];
    }
    for (const auto* key : {"allowMessages", "allowComments"}) {
        if (!settings[key].isString()) continue;
        const auto value = settings[key].asString();
        if (value == "everyone" || value == "following" || value == "none") {
            normalized[key] = value;
        }
    }
    try {
        auto client = pool_.acquire();
        const auto outcome = (*client)[database_]["users"].update_one(
            make_document(kvp("_id", userId)),
            make_document(kvp("$set", make_document(
                kvp("privacy.privateAccount", normalized["privateAccount"].asBool()),
                kvp("privacy.allowMessages", normalized["allowMessages"].asString()),
                kvp("privacy.allowComments", normalized["allowComments"].asString()),
                kvp("privacy.showLikedVideos", normalized["showLikedVideos"].asBool()),
                kvp("updatedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        recordSecurityEvent(userId, "privacy_updated", "Privacy controls changed");
        return normalized;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

bool MongoStore::interactionAllowed(const std::string& actingUserId,
                                    const std::string& targetUserId,
                                    const std::string& kind) {
    if (actingUserId == targetUserId) return true;
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto target = database["users"].find_one(make_document(kvp("_id", targetUserId)));
        if (!target) return false;
        auto rule = std::string{"everyone"};
        const auto privacy = target->view()["privacy"];
        if (privacy && privacy.type() == bsoncxx::type::k_document) {
            rule = fieldString(privacy.get_document().view(),
                               kind == "message" ? "allowMessages" : "allowComments",
                               "everyone");
        }
        if (rule == "everyone") return true;
        if (rule == "none") return false;
        return static_cast<bool>(database["follows"].find_one(
            make_document(kvp("followerId", targetUserId), kvp("followingId", actingUserId))));
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::setFeedFeedback(const std::string& userId,
                                        const std::string& postId,
                                        const std::string& type,
                                        bool enabled) {
    if (type != "not_interested" && type != "hide_creator") {
        throw StoreError(StoreError::Kind::kConflict, "Unknown feedback type");
    }
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto post = database["posts"].find_one(make_document(kvp("_id", postId)));
        if (!post) throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        const auto id = pairKey("feedback:" + type, userId, postId);
        if (enabled) {
            mongocxx::options::update options;
            options.upsert(true);
            database["feed_feedback"].update_one(
                make_document(kvp("_id", id)),
                make_document(kvp("$set", make_document(
                    kvp("userId", userId), kvp("postId", postId), kvp("type", type),
                    kvp("targetUserId", fieldString(post->view(), "userId")),
                    kvp("topic", fieldString(post->view(), "topic")),
                    kvp("updatedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))),
                options);
        } else {
            database["feed_feedback"].delete_one(make_document(kvp("_id", id)));
        }
        Json::Value result(Json::objectValue);
        result["postId"] = postId;
        result["type"] = type;
        result["enabled"] = enabled;
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::creatorAnalytics(const std::string& userId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        Json::Value result(Json::objectValue);
        result["totals"]["videos"] = 0;
        result["totals"]["views"] = 0;
        result["totals"]["likes"] = 0;
        result["totals"]["shares"] = 0;
        result["totals"]["comments"] = 0;
        result["totals"]["watchSeconds"] = 0;
        result["videos"] = Json::Value(Json::arrayValue);
        std::unordered_set<std::string> postIds;
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1)));
        options.limit(100);
        for (const auto& post : database["posts"].find(make_document(kvp("userId", userId)), options)) {
            const auto id = fieldString(post, "_id");
            postIds.insert(id);
            Json::Value item(Json::objectValue);
            item["_id"] = id;
            item["caption"] = fieldString(post, "caption");
            item["createdAt"] = isoDate(post, "createdAt");
            item["views"] = static_cast<Json::Int64>(fieldInteger(post, "viewCount"));
            item["shares"] = static_cast<Json::Int64>(fieldInteger(post, "shareCount"));
            const auto likes = post["likes"];
            const auto comments = post["comments"];
            item["likes"] = likes && likes.type() == bsoncxx::type::k_array
                                ? static_cast<Json::UInt64>(std::distance(likes.get_array().value.begin(), likes.get_array().value.end())) : 0;
            item["comments"] = comments && comments.type() == bsoncxx::type::k_array
                                   ? static_cast<Json::UInt64>(std::distance(comments.get_array().value.begin(), comments.get_array().value.end())) : 0;
            item["watchSeconds"] = 0;
            result["totals"]["videos"] = result["totals"]["videos"].asUInt64() + 1;
            for (const auto* key : {"views", "likes", "shares", "comments"}) {
                result["totals"][key] = result["totals"][key].asUInt64() + item[key].asUInt64();
            }
            result["videos"].append(std::move(item));
        }
        for (const auto& view : database["view_history"].find({})) {
            const auto postId = fieldString(view, "postId");
            if (!postIds.contains(postId)) continue;
            const auto seconds = std::max<std::int64_t>(0, fieldInteger(view, "watchSeconds"));
            result["totals"]["watchSeconds"] = result["totals"]["watchSeconds"].asInt64() + seconds;
            for (auto& item : result["videos"]) {
                if (item["_id"].asString() == postId) {
                    item["watchSeconds"] = item["watchSeconds"].asInt64() + seconds;
                    break;
                }
            }
        }
        result["totals"]["followers"] = static_cast<Json::UInt64>(
            database["follows"].count_documents(make_document(kvp("followingId", userId))));
        return result;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::listMessageHistory(const std::string& userId,
                                           const std::string& otherUserId,
                                           const std::string& before,
                                           std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        bsoncxx::builder::basic::document filter;
        filter.append(kvp("conversationId", chatKey(userId, otherUserId)));
        if (!before.empty()) {
            const auto cursor = database["messages"].find_one(
                make_document(kvp("_id", before), kvp("conversationId", chatKey(userId, otherUserId))));
            if (cursor && cursor->view()["createdAt"]) {
                filter.append(kvp("createdAt", make_document(
                    kvp("$lt", cursor->view()["createdAt"].get_date()))));
            }
        }
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1), kvp("_id", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        std::vector<Json::Value> newest;
        for (const auto& message : database["messages"].find(filter.extract(), options)) {
            newest.push_back(messageToJson(message));
        }
        Json::Value result(Json::objectValue);
        result["items"] = Json::Value(Json::arrayValue);
        for (auto item = newest.rbegin(); item != newest.rend(); ++item) result["items"].append(*item);
        result["nextCursor"] = newest.size() == limit && !newest.empty()
                                   ? newest.back()["_id"].asString() : "";
        return result;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::searchMessages(const std::string& userId,
                                       const std::string& otherUserId,
                                       const std::string& term,
                                       std::size_t limit) {
    try {
        auto client = pool_.acquire();
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        Json::Value result(Json::arrayValue);
        for (const auto& message : (*client)[database_]["messages"].find(
                 make_document(kvp("conversationId", chatKey(userId, otherUserId)),
                               kvp("text", bsoncxx::types::b_regex{escapePattern(term), "i"}),
                               kvp("deletedAt", make_document(kvp("$exists", false)))), options)) {
            result.append(messageToJson(message));
        }
        return result;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::editMessage(const std::string& messageId,
                                    const std::string& userId,
                                    const std::string& text) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto outcome = database["messages"].update_one(
            make_document(kvp("_id", messageId), kvp("senderId", userId),
                          kvp("deletedAt", make_document(kvp("$exists", false)))),
            make_document(kvp("$set", make_document(
                kvp("text", text),
                kvp("editedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kForbidden, "Only the sender can edit this message");
        }
        const auto updated = database["messages"].find_one(make_document(kvp("_id", messageId)));
        auto value = messageToJson(updated->view());
        Json::Value event(Json::objectValue);
        event["type"] = "message_updated";
        event["message"] = value;
        RealtimeHub::publish(fieldString(updated->view(), "recipientId"), event);
        return value;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::listSessions(const std::string& userId,
                                     const std::string& currentTokenHash) {
    try {
        auto client = pool_.acquire();
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1)));
        Json::Value result(Json::arrayValue);
        for (const auto& session : (*client)[database_]["sessions"].find(
                 make_document(kvp("userId", userId),
                               kvp("revokedAt", make_document(kvp("$exists", false)))), options)) {
            const auto id = fieldString(session, "_id");
            Json::Value item(Json::objectValue);
            item["id"] = id.substr(0, std::min<std::size_t>(12, id.size()));
            item["current"] = id == currentTokenHash;
            item["createdAt"] = isoDate(session, "createdAt");
            item["expiresAt"] = isoDate(session, "expiresAt");
            result.append(std::move(item));
        }
        return result;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

std::size_t MongoStore::revokeOtherSessions(const std::string& userId,
                                            const std::string& currentTokenHash) {
    try {
        auto client = pool_.acquire();
        const auto outcome = (*client)[database_]["sessions"].update_many(
            make_document(kvp("userId", userId), kvp("_id", make_document(kvp("$ne", currentTokenHash))),
                          kvp("revokedAt", make_document(kvp("$exists", false)))),
            make_document(kvp("$set", make_document(
                kvp("revokedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        recordSecurityEvent(userId, "sessions_revoked", "Signed out other devices");
        return outcome ? static_cast<std::size_t>(outcome->modified_count()) : 0;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::exportAccount(const std::string& userId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto user = database["users"].find_one(make_document(kvp("_id", userId)));
        if (!user) throw StoreError(StoreError::Kind::kNotFound, "User not found");
        Json::Value result(Json::objectValue);
        result["exportedAt"] = isoDate(user->view(), "updatedAt");
        result["profile"] = userToJson(user->view());
        result["privacy"] = getPrivacySettings(userId);
        result["posts"] = listPosts(std::nullopt, std::nullopt, 500);
        Json::Value owned(Json::arrayValue);
        for (const auto& post : result["posts"]) if (post["userId"].asString() == userId) owned.append(post);
        result["posts"] = std::move(owned);
        result["saved"] = listSaved(userId, 500);
        result["history"] = listHistory(userId, 500);
        result["notifications"] = listNotifications(userId, 500);
        result["sessions"] = listSessions(userId, "");
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::deleteAccount(const std::string& userId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["users"].find_one(make_document(kvp("_id", userId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        Json::Value files(Json::arrayValue);
        for (const auto& post : database["posts"].find(make_document(kvp("userId", userId)))) {
            for (const auto* key : {"media", "thumbnail", "subtitles"}) {
                const auto asset = post[key];
                if (asset && asset.type() == bsoncxx::type::k_document) {
                    const auto storage = fieldString(asset.get_document().view(), "storageName");
                    if (!storage.empty()) files.append(storage);
                }
            }
            const auto qualities = post["qualities"];
            const auto media = post["media"];
            if (qualities && qualities.type() == bsoncxx::type::k_array &&
                media && media.type() == bsoncxx::type::k_document) {
                const auto mediaId = fieldString(media.get_document().view(), "id");
                for (const auto& quality : qualities.get_array().value) {
                    if (quality.type() == bsoncxx::type::k_string) {
                        files.append(mediaId + "-" + std::string(quality.get_string().value) + ".mp4");
                    }
                }
            }
        }
        const auto user = database["users"].find_one(make_document(kvp("_id", userId)));
        const auto avatar = user->view()["avatar"];
        if (avatar && avatar.type() == bsoncxx::type::k_document) {
            const auto storage = fieldString(avatar.get_document().view(), "storageName");
            if (!storage.empty()) files.append(storage);
        }
        database["posts"].delete_many(make_document(kvp("userId", userId)));
        database["messages"].delete_many(make_document(kvp("participants", userId)));
        database["sessions"].delete_many(make_document(kvp("userId", userId)));
        database["notifications"].delete_many(make_document(kvp("$or", make_array(
            make_document(kvp("userId", userId)), make_document(kvp("actorId", userId))))));
        for (const auto* collection : {"follows", "follow_requests"}) {
            database[collection].delete_many(make_document(kvp("$or", make_array(
                make_document(kvp("followerId", userId)), make_document(kvp("followingId", userId)),
                make_document(kvp("requesterId", userId)), make_document(kvp("targetUserId", userId))))));
        }
        for (const auto* collection : {"blocks", "mutes", "conversation_settings"}) {
            database[collection].delete_many(make_document(kvp("$or", make_array(
                make_document(kvp("ownerId", userId)), make_document(kvp("targetUserId", userId)),
                make_document(kvp("userId", userId)), make_document(kvp("otherUserId", userId))))));
        }
        for (const auto* collection : {"saves", "view_history", "feed_feedback",
                                       "push_subscriptions", "security_events"}) {
            database[collection].delete_many(make_document(kvp("userId", userId)));
        }
        database["reports"].delete_many(make_document(kvp("$or", make_array(
            make_document(kvp("reporterId", userId)), make_document(kvp("targetUserId", userId))))));
        database["users"].delete_one(make_document(kvp("_id", userId)));
        Json::Value result(Json::objectValue);
        result["deleted"] = true;
        result["mediaFiles"] = std::move(files);
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::createContentReport(const std::string& reportId,
                                            const std::string& reporterId,
                                            const std::string& targetType,
                                            const std::string& targetId,
                                            const std::string& reason,
                                            const std::string& details) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        bool found = false;
        std::string targetUserId;
        if (targetType == "video") {
            const auto post = database["posts"].find_one(make_document(kvp("_id", targetId)));
            found = static_cast<bool>(post);
            if (post) targetUserId = fieldString(post->view(), "userId");
        } else if (targetType == "message") {
            const auto message = database["messages"].find_one(make_document(
                kvp("_id", targetId), kvp("participants", reporterId)));
            found = static_cast<bool>(message);
            if (message) targetUserId = fieldString(message->view(), "senderId");
        } else if (targetType == "comment") {
            found = static_cast<bool>(database["posts"].find_one(make_document(
                kvp("comments", make_document(kvp("$elemMatch", make_document(kvp("_id", targetId))))))));
        } else {
            throw StoreError(StoreError::Kind::kConflict, "Unknown report target");
        }
        if (!found) throw StoreError(StoreError::Kind::kNotFound, "Reported content not found");
        if (!targetUserId.empty() && targetUserId == reporterId) {
            throw StoreError(StoreError::Kind::kConflict, "You cannot report your own content");
        }
        const auto now = std::chrono::system_clock::now();
        mongocxx::options::update options;
        options.upsert(true);
        database["reports"].update_one(
            make_document(kvp("reporterId", reporterId), kvp("targetType", targetType),
                          kvp("targetId", targetId)),
            make_document(kvp("$set", make_document(
                              kvp("reason", reason), kvp("details", details),
                              kvp("updatedAt", bsoncxx::types::b_date{now}))),
                          kvp("$setOnInsert", make_document(
                              kvp("_id", reportId), kvp("reporterId", reporterId),
                              kvp("targetUserId", targetUserId), kvp("targetType", targetType),
                              kvp("targetId", targetId), kvp("status", "open"),
                              kvp("createdAt", bsoncxx::types::b_date{now})))), options);
        Json::Value result(Json::objectValue);
        result["reported"] = true;
        result["reportId"] = reportId;
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::listFollowRequests(const std::string& userId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        Json::Value result(Json::arrayValue);
        for (const auto& request : database["follow_requests"].find(
                 make_document(kvp("targetUserId", userId), kvp("status", "pending")))) {
            const auto requesterId = fieldString(request, "requesterId");
            const auto requester = database["users"].find_one(make_document(kvp("_id", requesterId)));
            if (!requester) continue;
            Json::Value item(Json::objectValue);
            item["requester"] = userToJson(requester->view());
            item["createdAt"] = isoDate(request, "createdAt");
            result.append(std::move(item));
        }
        return result;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::respondToFollowRequest(const std::string& userId,
                                               const std::string& requesterId,
                                               bool accept) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto request = database["follow_requests"].find_one(make_document(
            kvp("requesterId", requesterId), kvp("targetUserId", userId), kvp("status", "pending")));
        if (!request) throw StoreError(StoreError::Kind::kNotFound, "Follow request not found");
        const auto requestId = fieldString(request->view(), "_id");
        database["follow_requests"].delete_one(make_document(kvp("_id", requestId)));
        removeNotification(database, requestId);
        if (accept) {
            mongocxx::options::update options;
            options.upsert(true);
            const auto id = pairKey("follow", requesterId, userId);
            database["follows"].update_one(make_document(kvp("_id", id)),
                make_document(kvp("$setOnInsert", make_document(
                    kvp("followerId", requesterId), kvp("followingId", userId),
                    kvp("createdAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))), options);
            upsertNotification(database, pairKey("follow_accepted", userId, requesterId),
                               requesterId, userId, "follow_accepted");
        }
        Json::Value result(Json::objectValue);
        result["accepted"] = accept;
        result["requesterId"] = requesterId;
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw nextDatabaseError(error); }
}

void MongoStore::recordSecurityEvent(const std::string& userId,
                                     const std::string& type,
                                     const std::string& detail) {
    try {
        auto client = pool_.acquire();
        (*client)[database_]["security_events"].insert_one(make_document(
            kvp("_id", pairKey(type + ":" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()), userId, detail)),
            kvp("userId", userId), kvp("type", type), kvp("detail", detail),
            kvp("createdAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})));
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::listSecurityEvents(const std::string& userId, std::size_t limit) {
    try {
        auto client = pool_.acquire();
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        Json::Value result(Json::arrayValue);
        for (const auto& event : (*client)[database_]["security_events"].find(
                 make_document(kvp("userId", userId)), options)) {
            Json::Value item(Json::objectValue);
            item["type"] = fieldString(event, "type");
            item["detail"] = fieldString(event, "detail");
            item["createdAt"] = isoDate(event, "createdAt");
            result.append(std::move(item));
        }
        return result;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

void MongoStore::notifyMentions(const std::string& text,
                                const std::string& actorId,
                                const std::string& postId,
                                const std::string& commentId) {
    std::unordered_set<std::string> mentions;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '@') continue;
        std::string name;
        for (++index; index < text.size() && name.size() < 50; ++index) {
            const auto character = static_cast<unsigned char>(text[index]);
            if (!std::isalnum(character) && text[index] != '_' && text[index] != '.') break;
            name.push_back(static_cast<char>(std::tolower(character)));
        }
        if (!name.empty()) mentions.insert(std::move(name));
    }
    if (mentions.empty()) return;
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        for (const auto& mention : mentions) {
            const auto user = database["users"].find_one(make_document(
                kvp("userName", bsoncxx::types::b_regex{"^" + escapePattern(mention) + "$", "i"})));
            if (!user) continue;
            const auto userId = fieldString(user->view(), "_id");
            upsertNotification(database,
                pairKey("mention:" + postId + ":" + commentId, actorId, userId),
                userId, actorId, "mention", postId, commentId);
        }
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

Json::Value MongoStore::savePushSubscription(const std::string& userId,
                                             const std::string& endpoint,
                                             const std::string& key,
                                             const std::string& auth) {
    try {
        auto client = pool_.acquire();
        mongocxx::options::update options;
        options.upsert(true);
        (*client)[database_]["push_subscriptions"].update_one(
            make_document(kvp("_id", pairKey("push", userId, endpoint))),
            make_document(kvp("$set", make_document(
                kvp("userId", userId), kvp("endpoint", endpoint), kvp("key", key), kvp("auth", auth),
                kvp("updatedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))), options);
        Json::Value result(Json::objectValue);
        result["subscribed"] = true;
        return result;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

std::size_t MongoStore::deletePushSubscription(const std::string& userId,
                                               const std::string& endpoint) {
    try {
        auto client = pool_.acquire();
        const auto outcome = (*client)[database_]["push_subscriptions"].delete_one(
            make_document(kvp("userId", userId), kvp("endpoint", endpoint)));
        return outcome ? static_cast<std::size_t>(outcome->deleted_count()) : 0;
    } catch (const std::exception& error) { throw nextDatabaseError(error); }
}

}  // namespace tiktok
