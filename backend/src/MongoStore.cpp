#include "tiktok/MongoStore.h"

#include "tiktok/RealtimeHub.h"
#include "tiktok/Validation.h"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/index.hpp>
#include <mongocxx/options/update.hpp>
#include <mongocxx/uri.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
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

std::string getString(const bsoncxx::document::view& document,
                      std::string_view key,
                      std::string fallback = {}) {
    const auto element = document[key];
    if (element && element.type() == bsoncxx::type::k_string) {
        return std::string(element.get_string().value);
    }
    return fallback;
}

std::int64_t getInteger(const bsoncxx::document::view& document,
                        std::string_view key,
                        std::int64_t fallback = 0) {
    const auto element = document[key];
    if (!element) {
        return fallback;
    }
    if (element.type() == bsoncxx::type::k_int64) {
        return element.get_int64().value;
    }
    if (element.type() == bsoncxx::type::k_int32) {
        return element.get_int32().value;
    }
    return fallback;
}

std::string isoTimestamp(std::chrono::system_clock::time_point value) {
    const auto time = std::chrono::system_clock::to_time_t(value);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string dateField(const bsoncxx::document::view& document, std::string_view key) {
    const auto element = document[key];
    if (!element || element.type() != bsoncxx::type::k_date) {
        return {};
    }
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        element.get_date().value);
    return isoTimestamp(std::chrono::system_clock::time_point{sinceEpoch});
}

MediaRecord mediaFromPost(const bsoncxx::document::view& post) {
    const auto mediaElement = post["media"];
    if (!mediaElement || mediaElement.type() != bsoncxx::type::k_document) {
        throw StoreError(StoreError::Kind::kDatabase, "Post has no valid media metadata");
    }
    const auto media = mediaElement.get_document().view();
    MediaRecord result;
    result.id = getString(media, "id");
    result.storageName = getString(media, "storageName");
    result.originalName = getString(media, "originalName");
    result.contentType = getString(media, "contentType", "application/octet-stream");
    result.size = static_cast<std::size_t>(std::max<std::int64_t>(0, getInteger(media, "size")));
    const auto qualities = post["qualities"];
    if (qualities && qualities.type() == bsoncxx::type::k_array) {
        for (const auto& quality : qualities.get_array().value) {
            if (quality.type() == bsoncxx::type::k_string) {
                result.qualities.emplace_back(quality.get_string().value);
            }
        }
    }
    if (result.id.empty() || result.storageName.empty()) {
        throw StoreError(StoreError::Kind::kDatabase, "Post media metadata is incomplete");
    }
    return result;
}

std::optional<std::string> commentAuthor(const bsoncxx::document::view& post,
                                         const std::string& commentId) {
    const auto comments = post["comments"];
    if (!comments || comments.type() != bsoncxx::type::k_array) {
        return std::nullopt;
    }
    for (const auto& element : comments.get_array().value) {
        if (element.type() != bsoncxx::type::k_document) {
            continue;
        }
        const auto comment = element.get_document().view();
        if (getString(comment, "_id") == commentId) {
            return getString(comment, "userId");
        }
    }
    return std::nullopt;
}

StoreError databaseError(const std::exception& error) {
    return StoreError(StoreError::Kind::kDatabase, std::string("MongoDB operation failed: ") + error.what());
}

std::string conversationKey(const std::string& first, const std::string& second) {
    const auto& left = first < second ? first : second;
    const auto& right = first < second ? second : first;
    return std::to_string(left.size()) + ":" + left + right;
}

std::string directedKey(std::string_view prefix,
                        const std::string& first,
                        const std::string& second) {
    return std::string(prefix) + ":" + std::to_string(first.size()) + ":" + first + second;
}

std::vector<std::string> extractHashtags(std::string_view caption) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (std::size_t index = 0; index < caption.size(); ++index) {
        if (caption[index] != '#') {
            continue;
        }
        std::string tag;
        for (++index; index < caption.size() && tag.size() < 50; ++index) {
            const auto value = static_cast<unsigned char>(caption[index]);
            if (!std::isalnum(value) && caption[index] != '_') {
                break;
            }
            tag.push_back(static_cast<char>(std::tolower(value)));
        }
        if (!tag.empty() && seen.insert(tag).second) {
            result.push_back(std::move(tag));
        }
    }
    return result;
}

}  // namespace

MongoStore::MongoStore(const std::string& uri, const std::string& database)
    : pool_(mongocxx::uri{uri}), database_(database) {}

void MongoStore::ensureIndexes() {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];

        mongocxx::options::index uniqueSparse;
        uniqueSparse.unique(true);
        uniqueSparse.sparse(true);
        database["users"].create_index(make_document(kvp("googleSubject", 1)), uniqueSparse);

        database["posts"].create_index(make_document(kvp("createdAt", -1)));
        database["posts"].create_index(make_document(kvp("topic", 1), kvp("createdAt", -1)));
        database["posts"].create_index(make_document(kvp("userId", 1), kvp("createdAt", -1)));
        database["posts"].create_index(make_document(kvp("likes", 1)));
        database["posts"].create_index(make_document(kvp("hashtags", 1), kvp("createdAt", -1)));
        database["posts"].create_index(make_document(kvp("media.id", 1)), uniqueSparse);

        mongocxx::options::index ttl;
        ttl.expire_after(std::chrono::seconds{0});
        database["sessions"].create_index(make_document(kvp("expiresAt", 1)), ttl);
        database["sessions"].create_index(make_document(kvp("userId", 1)));

        database["messages"].create_index(
            make_document(kvp("conversationId", 1), kvp("createdAt", -1)));
        database["messages"].create_index(
            make_document(kvp("participants", 1), kvp("createdAt", -1)));
        database["messages"].create_index(
            make_document(kvp("recipientId", 1), kvp("senderId", 1), kvp("readAt", 1)));

        database["follows"].create_index(
            make_document(kvp("followerId", 1), kvp("createdAt", -1)));
        database["follows"].create_index(
            make_document(kvp("followingId", 1), kvp("createdAt", -1)));

        mongocxx::options::index uniqueReport;
        uniqueReport.unique(true);
        database["reports"].create_index(
            make_document(kvp("reporterId", 1), kvp("targetUserId", 1)), uniqueReport);
        database["reports"].create_index(make_document(kvp("status", 1), kvp("createdAt", -1)));

        database["notifications"].create_index(
            make_document(kvp("userId", 1), kvp("createdAt", -1)));
        database["notifications"].create_index(
            make_document(kvp("userId", 1), kvp("readAt", 1), kvp("createdAt", -1)));

        database["blocks"].create_index(
            make_document(kvp("ownerId", 1), kvp("targetUserId", 1)), uniqueReport);
        database["mutes"].create_index(
            make_document(kvp("ownerId", 1), kvp("targetUserId", 1)), uniqueReport);
        database["saves"].create_index(
            make_document(kvp("userId", 1), kvp("postId", 1)), uniqueReport);
        database["saves"].create_index(make_document(kvp("userId", 1), kvp("createdAt", -1)));
        database["view_history"].create_index(
            make_document(kvp("userId", 1), kvp("postId", 1)), uniqueReport);
        database["view_history"].create_index(
            make_document(kvp("userId", 1), kvp("viewedAt", -1)));
        database["conversation_settings"].create_index(
            make_document(kvp("userId", 1), kvp("otherUserId", 1)), uniqueReport);
        database["messages"].create_index(make_document(kvp("attachment.id", 1)), uniqueSparse);
        database["follow_requests"].create_index(
            make_document(kvp("requesterId", 1), kvp("targetUserId", 1)), uniqueReport);
        database["follow_requests"].create_index(
            make_document(kvp("targetUserId", 1), kvp("createdAt", -1)));
        database["feed_feedback"].create_index(
            make_document(kvp("userId", 1), kvp("postId", 1)), uniqueReport);
        database["feed_feedback"].create_index(
            make_document(kvp("userId", 1), kvp("creatorId", 1)));
        database["security_events"].create_index(
            make_document(kvp("userId", 1), kvp("createdAt", -1)));
        database["push_subscriptions"].create_index(
            make_document(kvp("userId", 1), kvp("endpoint", 1)), uniqueReport);
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

void MongoStore::ping() {
    try {
        auto client = pool_.acquire();
        (*client)[database_].run_command(make_document(kvp("ping", 1)));
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::userToJson(const bsoncxx::document::view& user) const {
    Json::Value result(Json::objectValue);
    result["_id"] = getString(user, "_id");
    result["_type"] = "user";
    result["userName"] = getString(user, "userName", "Unknown User");
    result["image"] = getString(user, "image");
    result["bio"] = getString(user, "bio");
    result["suspended"] = user["suspended"] && user["suspended"].type() == bsoncxx::type::k_bool
                              ? user["suspended"].get_bool().value
                              : false;
    result["provider"] = getString(user, "provider", "legacy-unlinked");
    const auto privacy = user["privacy"];
    result["privateAccount"] = false;
    result["showLikedVideos"] = true;
    if (privacy && privacy.type() == bsoncxx::type::k_document) {
        const auto view = privacy.get_document().view();
        const auto privateAccount = view["privateAccount"];
        const auto showLikedVideos = view["showLikedVideos"];
        result["privateAccount"] = privateAccount &&
                                           privateAccount.type() == bsoncxx::type::k_bool
                                       ? privateAccount.get_bool().value
                                       : false;
        result["showLikedVideos"] = showLikedVideos &&
                                             showLikedVideos.type() == bsoncxx::type::k_bool
                                         ? showLikedVideos.get_bool().value
                                         : true;
    }
    result["_createdAt"] = dateField(user, "createdAt");
    return result;
}

Json::Value MongoStore::upsertUser(const UserIdentity& identity) {
    try {
        auto client = pool_.acquire();
        auto users = (*client)[database_]["users"];
        const auto now = std::chrono::system_clock::now();
        const auto existing = users.find_one(make_document(kvp("_id", identity.id)));
        const bool hasCustomAvatar = existing && existing->view()["avatar"];
        const bool hasCustomProfile = existing && existing->view()["profileCustomized"] &&
                                      existing->view()["profileCustomized"].type() ==
                                          bsoncxx::type::k_bool &&
                                      existing->view()["profileCustomized"].get_bool().value;

        bsoncxx::builder::basic::document fields;
        fields.append(kvp("provider", identity.provider),
                      kvp("updatedAt", bsoncxx::types::b_date{now}));
        if (!hasCustomProfile) {
            fields.append(kvp("userName", identity.userName));
        }
        if (!hasCustomAvatar) {
            fields.append(kvp("image", identity.image));
        }
        if (!identity.providerSubject.empty()) {
            fields.append(kvp("googleSubject", identity.providerSubject));
        }
        if (identity.provider == "google") {
            fields.append(kvp("emailVerified", identity.emailVerified));
        }

        mongocxx::options::update options;
        options.upsert(true);
        const auto update = make_document(
            kvp("$set", fields.extract()),
            kvp("$setOnInsert", make_document(kvp("createdAt", bsoncxx::types::b_date{now}))));
        users.update_one(make_document(kvp("_id", identity.id)), update.view(), options);
        const auto user = users.find_one(make_document(kvp("_id", identity.id)));
        if (!user) {
            throw StoreError(StoreError::Kind::kDatabase, "User upsert returned no user");
        }
        return userToJson(user->view());
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

std::optional<Json::Value> MongoStore::getUser(const std::string& id) {
    try {
        auto client = pool_.acquire();
        const auto user = (*client)[database_]["users"].find_one(make_document(kvp("_id", id)));
        if (!user) {
            return std::nullopt;
        }
        return userToJson(user->view());
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::listUsers(std::size_t limit) {
    try {
        auto client = pool_.acquire();
        mongocxx::options::find options;
        options.sort(make_document(kvp("userName", 1)));
        options.limit(static_cast<std::int64_t>(limit));
        Json::Value result(Json::arrayValue);
        for (const auto& user : (*client)[database_]["users"].find({}, options)) {
            result.append(userToJson(user));
        }
        return result;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

void MongoStore::createSession(const SessionRecord& session) {
    try {
        auto client = pool_.acquire();
        const auto now = std::chrono::system_clock::now();
        (*client)[database_]["sessions"].insert_one(make_document(
            kvp("_id", session.tokenHash), kvp("userId", session.userId),
            kvp("csrfHash", session.csrfHash), kvp("createdAt", bsoncxx::types::b_date{now}),
            kvp("expiresAt", bsoncxx::types::b_date{session.expiresAt})));
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

std::optional<SessionRecord> MongoStore::getSession(const std::string& tokenHash) {
    try {
        auto client = pool_.acquire();
        auto sessions = (*client)[database_]["sessions"];
        const auto document = sessions.find_one(make_document(kvp("_id", tokenHash)));
        if (!document) {
            return std::nullopt;
        }
        const auto view = document->view();
        if (view["revokedAt"]) {
            return std::nullopt;
        }
        const auto expiresElement = view["expiresAt"];
        if (!expiresElement || expiresElement.type() != bsoncxx::type::k_date) {
            throw StoreError(StoreError::Kind::kDatabase, "Session has no valid expiration");
        }
        const auto expiresAt = std::chrono::system_clock::time_point{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                expiresElement.get_date().value)};
        if (expiresAt <= std::chrono::system_clock::now()) {
            sessions.delete_one(make_document(kvp("_id", tokenHash)));
            return std::nullopt;
        }
        return SessionRecord{tokenHash, getString(view, "userId"), getString(view, "csrfHash"), expiresAt};
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

void MongoStore::rotateCsrf(const std::string& tokenHash, const std::string& csrfHash) {
    try {
        auto client = pool_.acquire();
        (*client)[database_]["sessions"].update_one(
            make_document(kvp("_id", tokenHash)),
            make_document(kvp("$set", make_document(kvp("csrfHash", csrfHash)))));
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

void MongoStore::revokeSession(const std::string& tokenHash) {
    try {
        auto client = pool_.acquire();
        (*client)[database_]["sessions"].update_one(
            make_document(kvp("_id", tokenHash)),
            make_document(kvp("$set", make_document(
                                         kvp("revokedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::postToJson(mongocxx::database& database,
                                   const bsoncxx::document::view& post) const {
    Json::Value result(Json::objectValue);
    result["_id"] = getString(post, "_id");
    result["caption"] = getString(post, "caption");
    result["topic"] = getString(post, "topic");
    result["userId"] = getString(post, "userId");
    result["_createdAt"] = dateField(post, "createdAt");
    result["viewCount"] = static_cast<Json::UInt64>(
        std::max<std::int64_t>(0, getInteger(post, "viewCount")));
    result["shareCount"] = static_cast<Json::UInt64>(
        std::max<std::int64_t>(0, getInteger(post, "shareCount")));
    result["hashtags"] = Json::Value(Json::arrayValue);
    const auto hashtags = post["hashtags"];
    if (hashtags && hashtags.type() == bsoncxx::type::k_array) {
        for (const auto& hashtag : hashtags.get_array().value) {
            if (hashtag.type() == bsoncxx::type::k_string) {
                result["hashtags"].append(std::string(hashtag.get_string().value));
            }
        }
    }

    const auto author = database["users"].find_one(make_document(kvp("_id", result["userId"].asString())));
    if (author) {
        result["postedBy"] = userToJson(author->view());
    } else {
        Json::Value unknown(Json::objectValue);
        unknown["_id"] = result["userId"];
        unknown["userName"] = "Unknown User";
        unknown["image"] = "";
        result["postedBy"] = std::move(unknown);
    }

    const auto media = mediaFromPost(post);
    result["video"]["asset"]["_id"] = media.id;
    result["video"]["asset"]["url"] = "/api/media/" + media.id;
    result["video"]["asset"]["contentType"] = media.contentType;
    result["video"]["asset"]["qualities"] = Json::Value(Json::arrayValue);
    const auto qualities = post["qualities"];
    if (qualities && qualities.type() == bsoncxx::type::k_array) {
        for (const auto& quality : qualities.get_array().value) {
            if (quality.type() == bsoncxx::type::k_string) {
                result["video"]["asset"]["qualities"].append(
                    std::string(quality.get_string().value));
            }
        }
    }
    const auto thumbnail = post["thumbnail"];
    if (thumbnail && thumbnail.type() == bsoncxx::type::k_document) {
        const auto view = thumbnail.get_document().view();
        const auto id = getString(view, "id");
        if (!id.empty()) {
            result["thumbnailUrl"] = "/api/media/" + id;
        }
    }
    const auto subtitles = post["subtitles"];
    if (subtitles && subtitles.type() == bsoncxx::type::k_document) {
        const auto view = subtitles.get_document().view();
        const auto id = getString(view, "id");
        if (!id.empty()) {
            result["subtitlesUrl"] = "/api/media/" + id;
        }
    }
    result["video"]["asset"]["size"] = static_cast<Json::UInt64>(media.size);

    result["likes"] = Json::Value(Json::arrayValue);
    const auto likes = post["likes"];
    if (likes && likes.type() == bsoncxx::type::k_array) {
        for (const auto& like : likes.get_array().value) {
            if (like.type() == bsoncxx::type::k_string) {
                Json::Value item(Json::objectValue);
                item["_ref"] = std::string(like.get_string().value);
                result["likes"].append(std::move(item));
            }
        }
    }

    result["comments"] = Json::Value(Json::arrayValue);
    const auto comments = post["comments"];
    if (comments && comments.type() == bsoncxx::type::k_array) {
        for (const auto& commentElement : comments.get_array().value) {
            if (commentElement.type() != bsoncxx::type::k_document) {
                continue;
            }
            const auto comment = commentElement.get_document().view();
            Json::Value item(Json::objectValue);
            item["_key"] = getString(comment, "_id");
            item["comment"] = getString(comment, "text");
            const auto commentUser = getString(comment, "userId");
            item["postedBy"]["_id"] = commentUser;
            item["postedBy"]["_ref"] = commentUser;
            item["createdAt"] = dateField(comment, "createdAt");
            item["editedAt"] = dateField(comment, "editedAt");
            item["pinned"] = getString(post, "pinnedCommentId") == getString(comment, "_id");
            item["likes"] = Json::Value(Json::arrayValue);
            const auto commentLikes = comment["likes"];
            if (commentLikes && commentLikes.type() == bsoncxx::type::k_array) {
                for (const auto& like : commentLikes.get_array().value) {
                    if (like.type() == bsoncxx::type::k_string) {
                        Json::Value likeItem(Json::objectValue);
                        likeItem["_ref"] = std::string(like.get_string().value);
                        item["likes"].append(std::move(likeItem));
                    }
                }
            }
            item["replies"] = Json::Value(Json::arrayValue);
            const auto replies = comment["replies"];
            if (replies && replies.type() == bsoncxx::type::k_array) {
                for (const auto& replyElement : replies.get_array().value) {
                    if (replyElement.type() != bsoncxx::type::k_document) {
                        continue;
                    }
                    const auto reply = replyElement.get_document().view();
                    Json::Value replyItem(Json::objectValue);
                    replyItem["_key"] = getString(reply, "_id");
                    replyItem["comment"] = getString(reply, "text");
                    const auto replyUser = getString(reply, "userId");
                    replyItem["postedBy"]["_id"] = replyUser;
                    replyItem["postedBy"]["_ref"] = replyUser;
                    replyItem["createdAt"] = dateField(reply, "createdAt");
                    replyItem["editedAt"] = dateField(reply, "editedAt");
                    item["replies"].append(std::move(replyItem));
                }
            }
            result["comments"].append(std::move(item));
        }
    }
    return result;
}

Json::Value MongoStore::listPosts(const std::optional<std::string>& topic,
                                  const std::optional<std::string>& searchTerm,
                                  std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        bsoncxx::builder::basic::document filter;
        if (topic && !topic->empty()) {
            filter.append(kvp("topic", bsoncxx::types::b_regex{"^" + escapeRegex(*topic), "i"}));
        }
        if (searchTerm && !searchTerm->empty()) {
            const auto escaped = escapeRegex(*searchTerm);
            filter.append(kvp(
                "$or", make_array(
                           make_document(kvp("caption", bsoncxx::types::b_regex{escaped, "i"})),
                           make_document(kvp("topic", bsoncxx::types::b_regex{escaped, "i"})))));
        }

        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1), kvp("_id", -1)));
        options.limit(static_cast<std::int64_t>(limit));

        Json::Value result(Json::arrayValue);
        for (const auto& post : database["posts"].find(filter.view(), options)) {
            result.append(postToJson(database, post));
        }
        return result;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

std::optional<Json::Value> MongoStore::getPost(const std::string& id) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto post = database["posts"].find_one(make_document(kvp("_id", id)));
        if (!post) {
            return std::nullopt;
        }
        return postToJson(database, post->view());
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::getProfile(const std::string& userId, const std::string& viewerId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto user = database["users"].find_one(make_document(kvp("_id", userId)));
        if (!user) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }

        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1), kvp("_id", -1)));
        options.limit(100);

        Json::Value result(Json::objectValue);
        result["user"] = userToJson(user->view());
        result["followerCount"] = static_cast<Json::UInt64>(
            database["follows"].count_documents(make_document(kvp("followingId", userId))));
        result["followingCount"] = static_cast<Json::UInt64>(
            database["follows"].count_documents(make_document(kvp("followerId", userId))));
        result["userVideos"] = Json::Value(Json::arrayValue);
        result["userLikedVideos"] = Json::Value(Json::arrayValue);
        const bool canViewPrivate = viewerId == userId || !result["user"]["privateAccount"].asBool() ||
            (!viewerId.empty() && static_cast<bool>(database["follows"].find_one(
                make_document(kvp("followerId", viewerId), kvp("followingId", userId)))));
        result["privateContentHidden"] = !canViewPrivate;
        if (canViewPrivate) {
            for (const auto& post : database["posts"].find(make_document(kvp("userId", userId)), options)) {
                result["userVideos"].append(postToJson(database, post));
            }
        }
        if (canViewPrivate && (viewerId == userId || result["user"]["showLikedVideos"].asBool())) {
            for (const auto& post : database["posts"].find(make_document(kvp("likes", userId)), options)) {
                result["userLikedVideos"].append(postToJson(database, post));
            }
        }
        return result;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::search(const std::string& term) {
    Json::Value result(Json::objectValue);
    result["videos"] = Json::Value(Json::arrayValue);
    for (const auto& video : listPosts(std::nullopt, term)) {
        if (!video["postedBy"]["privateAccount"].asBool()) result["videos"].append(video);
    }

    try {
        auto client = pool_.acquire();
        auto users = (*client)[database_]["users"];
        mongocxx::options::find options;
        options.sort(make_document(kvp("userName", 1)));
        options.limit(100);
        Json::Value accounts(Json::arrayValue);
        for (const auto& user : users.find(
                 make_document(kvp("userName", bsoncxx::types::b_regex{escapeRegex(term), "i"})), options)) {
            accounts.append(userToJson(user));
        }
        result["users"] = std::move(accounts);
        return result;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

void MongoStore::upsertNotification(mongocxx::database& database,
                                    const std::string& id,
                                    const std::string& userId,
                                    const std::string& actorId,
                                    const std::string& type,
                                    const std::string& postId,
                                    const std::string& commentId) {
    if (userId.empty() || userId == actorId) {
        return;
    }
    const auto recipient = database["users"].find_one(make_document(kvp("_id", userId)));
    if (recipient) {
        const auto preferences = recipient->view()["notificationPreferences"];
        if (preferences && preferences.type() == bsoncxx::type::k_document) {
            const auto enabled = preferences.get_document().view()[type];
            if (enabled && enabled.type() == bsoncxx::type::k_bool &&
                !enabled.get_bool().value) {
                return;
            }
        }
    }
    const auto now = std::chrono::system_clock::now();
    bsoncxx::builder::basic::document created;
    created.append(kvp("userId", userId), kvp("actorId", actorId), kvp("type", type),
                   kvp("createdAt", bsoncxx::types::b_date{now}));
    if (!postId.empty()) {
        created.append(kvp("postId", postId));
    }
    if (!commentId.empty()) {
        created.append(kvp("commentId", commentId));
    }
    mongocxx::options::update options;
    options.upsert(true);
    database["notifications"].update_one(
        make_document(kvp("_id", id)),
        make_document(kvp("$setOnInsert", created.extract())), options);
    Json::Value event(Json::objectValue);
    event["type"] = "notification";
    event["notificationType"] = type;
    RealtimeHub::publish(userId, event);
}

void MongoStore::removeNotification(mongocxx::database& database, const std::string& id) {
    database["notifications"].delete_one(make_document(kvp("_id", id)));
}

Json::Value MongoStore::getRelationship(const std::string& actingUserId,
                                        const std::string& targetUserId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["users"].find_one(make_document(kvp("_id", targetUserId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        Json::Value result(Json::objectValue);
        result["following"] = static_cast<bool>(database["follows"].find_one(
            make_document(kvp("_id", directedKey("follow", actingUserId, targetUserId)))));
        result["requested"] = static_cast<bool>(database["follow_requests"].find_one(
            make_document(kvp("_id", directedKey("follow_request", actingUserId,
                                                   targetUserId)))));
        result["followerCount"] = static_cast<Json::UInt64>(
            database["follows"].count_documents(make_document(kvp("followingId", targetUserId))));
        result["followingCount"] = static_cast<Json::UInt64>(
            database["follows"].count_documents(make_document(kvp("followerId", targetUserId))));
        return result;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::setFollow(const std::string& actingUserId,
                                  const std::string& targetUserId,
                                  bool follow) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (actingUserId == targetUserId) {
            throw StoreError(StoreError::Kind::kConflict, "You cannot follow yourself");
        }
        const auto target = database["users"].find_one(make_document(kvp("_id", targetUserId)));
        if (!target) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        if (follow && isBlocked(actingUserId, targetUserId)) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "This account cannot be followed");
        }
        const auto id = directedKey("follow", actingUserId, targetUserId);
        if (follow) {
            bool isPrivate = false;
            const auto privacy = target->view()["privacy"];
            if (privacy && privacy.type() == bsoncxx::type::k_document) {
                const auto field = privacy.get_document().view()["privateAccount"];
                isPrivate = field && field.type() == bsoncxx::type::k_bool &&
                            field.get_bool().value;
            }
            if (isPrivate) {
                const auto requestId = directedKey("follow_request", actingUserId,
                                                   targetUserId);
                mongocxx::options::update options;
                options.upsert(true);
                database["follow_requests"].update_one(
                    make_document(kvp("_id", requestId)),
                    make_document(kvp("$setOnInsert", make_document(
                        kvp("requesterId", actingUserId), kvp("targetUserId", targetUserId),
                        kvp("status", "pending"),
                        kvp("createdAt", bsoncxx::types::b_date{
                                             std::chrono::system_clock::now()})))),
                    options);
                upsertNotification(database, requestId, targetUserId, actingUserId,
                                   "follow_request");
                return getRelationship(actingUserId, targetUserId);
            }
            mongocxx::options::update options;
            options.upsert(true);
            database["follows"].update_one(
                make_document(kvp("_id", id)),
                make_document(kvp("$setOnInsert", make_document(
                    kvp("followerId", actingUserId), kvp("followingId", targetUserId),
                    kvp("createdAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))),
                options);
            upsertNotification(database, id, targetUserId, actingUserId, "follow");
        } else {
            database["follows"].delete_one(make_document(kvp("_id", id)));
            removeNotification(database, id);
            const auto requestId = directedKey("follow_request", actingUserId, targetUserId);
            database["follow_requests"].delete_one(make_document(kvp("_id", requestId)));
            removeNotification(database, requestId);
        }
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
    return getRelationship(actingUserId, targetUserId);
}

Json::Value MongoStore::createReport(const ReportInput& input) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (input.reporterId == input.targetUserId) {
            throw StoreError(StoreError::Kind::kConflict, "You cannot report your own account");
        }
        if (!database["users"].find_one(make_document(kvp("_id", input.targetUserId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        const auto now = std::chrono::system_clock::now();
        mongocxx::options::update options;
        options.upsert(true);
        database["reports"].update_one(
            make_document(kvp("reporterId", input.reporterId),
                          kvp("targetUserId", input.targetUserId)),
            make_document(
                kvp("$set", make_document(kvp("reason", input.reason),
                                           kvp("details", input.details),
                                           kvp("updatedAt", bsoncxx::types::b_date{now}))),
                kvp("$setOnInsert", make_document(kvp("_id", input.id),
                                                   kvp("reporterId", input.reporterId),
                                                   kvp("targetUserId", input.targetUserId),
                                                   kvp("status", "open"),
                                                   kvp("createdAt", bsoncxx::types::b_date{now})))),
            options);
        const auto report = database["reports"].find_one(
            make_document(kvp("reporterId", input.reporterId),
                          kvp("targetUserId", input.targetUserId)));
        if (!report) {
            throw StoreError(StoreError::Kind::kDatabase, "Report could not be read back");
        }
        Json::Value result(Json::objectValue);
        result["reported"] = true;
        result["reportId"] = getString(report->view(), "_id");
        result["status"] = getString(report->view(), "status", "open");
        return result;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::messageToJson(const bsoncxx::document::view& message) const {
    Json::Value result(Json::objectValue);
    result["_id"] = getString(message, "_id");
    result["senderId"] = getString(message, "senderId");
    result["recipientId"] = getString(message, "recipientId");
    const bool deleted = static_cast<bool>(message["deletedAt"]);
    result["deleted"] = deleted;
    result["text"] = deleted ? "Message deleted" : getString(message, "text");
    result["replyTo"] = getString(message, "replyTo");
    result["createdAt"] = dateField(message, "createdAt");
    result["edited"] = static_cast<bool>(message["editedAt"]);
    if (result["edited"].asBool()) {
        result["editedAt"] = dateField(message, "editedAt");
    }
    result["read"] = static_cast<bool>(message["readAt"]);
    if (result["read"].asBool()) {
        result["readAt"] = dateField(message, "readAt");
    }
    result["reactions"] = Json::Value(Json::arrayValue);
    const auto reactions = message["reactions"];
    if (reactions && reactions.type() == bsoncxx::type::k_array) {
        for (const auto& item : reactions.get_array().value) {
            if (item.type() != bsoncxx::type::k_document) continue;
            const auto reaction = item.get_document().view();
            Json::Value value(Json::objectValue);
            value["userId"] = getString(reaction, "userId");
            value["emoji"] = getString(reaction, "emoji");
            result["reactions"].append(std::move(value));
        }
    }
    const auto attachment = message["attachment"];
    if (!deleted && attachment && attachment.type() == bsoncxx::type::k_document) {
        const auto view = attachment.get_document().view();
        result["attachment"]["id"] = getString(view, "id");
        result["attachment"]["name"] = getString(view, "originalName");
        result["attachment"]["contentType"] = getString(view, "contentType");
        result["attachment"]["url"] = "/api/chat/media/" + getString(view, "id");
    }
    return result;
}

Json::Value MongoStore::notificationToJson(
    mongocxx::database& database,
    const bsoncxx::document::view& notification) const {
    Json::Value result(Json::objectValue);
    result["_id"] = getString(notification, "_id");
    result["type"] = getString(notification, "type");
    result["postId"] = getString(notification, "postId");
    result["commentId"] = getString(notification, "commentId");
    result["createdAt"] = dateField(notification, "createdAt");
    result["read"] = static_cast<bool>(notification["readAt"]);
    const auto actorId = getString(notification, "actorId");
    const auto actor = database["users"].find_one(make_document(kvp("_id", actorId)));
    if (actor) {
        result["actor"] = userToJson(actor->view());
    } else {
        result["actor"]["_id"] = actorId;
        result["actor"]["userName"] = "Unknown User";
        result["actor"]["image"] = "";
    }
    return result;
}

Json::Value MongoStore::listNotifications(const std::string& userId, std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1), kvp("_id", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        Json::Value result(Json::arrayValue);
        for (const auto& notification :
             database["notifications"].find(make_document(kvp("userId", userId)), options)) {
            result.append(notificationToJson(database, notification));
        }
        return result;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

std::size_t MongoStore::markNotificationsRead(const std::string& userId) {
    try {
        auto client = pool_.acquire();
        const auto outcome = (*client)[database_]["notifications"].update_many(
            make_document(kvp("userId", userId),
                          kvp("readAt", make_document(kvp("$exists", false)))),
            make_document(kvp("$set", make_document(
                kvp("readAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        return outcome ? static_cast<std::size_t>(outcome->modified_count()) : 0;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::listConversations(const std::string& userId, std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto messages = database["messages"];
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1), kvp("_id", -1)));
        options.limit(2'000);

        Json::Value result(Json::arrayValue);
        std::unordered_set<std::string> includedUsers;
        for (const auto& message : messages.find(make_document(kvp("participants", userId)), options)) {
            const auto senderId = getString(message, "senderId");
            const auto recipientId = getString(message, "recipientId");
            const auto otherUserId = senderId == userId ? recipientId : senderId;
            if (otherUserId.empty() || otherUserId == userId || includedUsers.contains(otherUserId)) {
                continue;
            }
            if (isBlocked(userId, otherUserId)) {
                continue;
            }
            const auto user = database["users"].find_one(make_document(kvp("_id", otherUserId)));
            if (!user) {
                continue;
            }

            Json::Value conversation(Json::objectValue);
            conversation["user"] = userToJson(user->view());
            conversation["lastMessage"] = messageToJson(message);
            conversation["unreadCount"] = static_cast<Json::UInt64>(messages.count_documents(
                make_document(kvp("conversationId", conversationKey(userId, otherUserId)),
                              kvp("senderId", otherUserId), kvp("recipientId", userId),
                              kvp("readAt", make_document(kvp("$exists", false))))));
            const auto settings = database["conversation_settings"].find_one(
                make_document(kvp("userId", userId), kvp("otherUserId", otherUserId)));
            conversation["muted"] = settings && settings->view()["muted"] &&
                                    settings->view()["muted"].get_bool().value;
            conversation["archived"] = settings && settings->view()["archived"] &&
                                       settings->view()["archived"].get_bool().value;
            result.append(std::move(conversation));
            includedUsers.insert(otherUserId);
            if (includedUsers.size() >= limit) {
                break;
            }
        }
        return result;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::listMessages(const std::string& userId,
                                     const std::string& otherUserId,
                                     std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["users"].find_one(make_document(kvp("_id", otherUserId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "Chat user not found");
        }
        if (isBlocked(userId, otherUserId)) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "This conversation is unavailable");
        }

        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1), kvp("_id", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        std::vector<Json::Value> newestFirst;
        for (const auto& message : database["messages"].find(
                 make_document(kvp("conversationId", conversationKey(userId, otherUserId))), options)) {
            newestFirst.push_back(messageToJson(message));
        }

        Json::Value result(Json::arrayValue);
        for (auto message = newestFirst.rbegin(); message != newestFirst.rend(); ++message) {
            result.append(*message);
        }
        return result;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::createMessage(const MessageInput& input) {
    if (!interactionAllowed(input.senderId, input.recipientId, "message")) {
        throw StoreError(StoreError::Kind::kForbidden,
                         "This account's privacy settings do not allow messages from you");
    }
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (input.senderId == input.recipientId) {
            throw StoreError(StoreError::Kind::kConflict, "You cannot send a message to yourself");
        }
        if (!database["users"].find_one(make_document(kvp("_id", input.senderId)))) {
            throw StoreError(StoreError::Kind::kForbidden, "Authenticated user no longer exists");
        }
        if (!database["users"].find_one(make_document(kvp("_id", input.recipientId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "Chat user not found");
        }
        if (isBlocked(input.senderId, input.recipientId)) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "Messaging is unavailable for this account");
        }

        if (!input.replyTo.empty()) {
            const auto replied = database["messages"].find_one(
                make_document(kvp("_id", input.replyTo),
                              kvp("participants", make_document(kvp("$all", make_array(
                                  input.senderId, input.recipientId))))));
            if (!replied) {
                throw StoreError(StoreError::Kind::kNotFound, "Reply message not found");
            }
        }

        const auto now = std::chrono::system_clock::now();
        bsoncxx::builder::basic::document message;
        message.append(kvp("_id", input.id),
                       kvp("conversationId", conversationKey(input.senderId, input.recipientId)),
                       kvp("participants", make_array(input.senderId, input.recipientId)),
                       kvp("senderId", input.senderId), kvp("recipientId", input.recipientId),
                       kvp("text", input.text), kvp("reactions", make_array()),
                       kvp("createdAt", bsoncxx::types::b_date{now}));
        if (!input.replyTo.empty()) {
            message.append(kvp("replyTo", input.replyTo));
        }
        if (!input.attachmentId.empty()) {
            message.append(kvp("attachment", make_document(
                kvp("id", input.attachmentId),
                kvp("storageName", input.attachmentStorageName),
                kvp("contentType", input.attachmentContentType),
                kvp("originalName", input.attachmentOriginalName))));
        }
        database["messages"].insert_one(message.extract());
        const auto recipientSettings = database["conversation_settings"].find_one(
            make_document(kvp("userId", input.recipientId),
                          kvp("otherUserId", input.senderId), kvp("muted", true)));
        if (!recipientSettings) {
            upsertNotification(database, "message:" + input.id, input.recipientId,
                               input.senderId, "message");
        }
        const auto created = database["messages"].find_one(make_document(kvp("_id", input.id)));
        if (!created) {
            throw StoreError(StoreError::Kind::kDatabase, "Created message could not be read back");
        }
        Json::Value event(Json::objectValue);
        event["type"] = "message";
        event["message"] = messageToJson(created->view());
        RealtimeHub::publish(input.recipientId, event);
        return messageToJson(created->view());
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

std::size_t MongoStore::markConversationRead(const std::string& userId,
                                             const std::string& otherUserId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto messages = database["messages"];
        const auto outcome = messages.update_many(
            make_document(kvp("conversationId", conversationKey(userId, otherUserId)),
                          kvp("senderId", otherUserId), kvp("recipientId", userId),
                          kvp("readAt", make_document(kvp("$exists", false)))),
            make_document(kvp("$set", make_document(
                kvp("readAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        database["notifications"].update_many(
            make_document(kvp("userId", userId), kvp("actorId", otherUserId),
                          kvp("type", "message"),
                          kvp("readAt", make_document(kvp("$exists", false)))),
            make_document(kvp("$set", make_document(
                kvp("readAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        return outcome ? static_cast<std::size_t>(outcome->modified_count()) : 0;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::createPost(const PostInput& input) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["users"].find_one(make_document(kvp("_id", input.userId)))) {
            throw StoreError(StoreError::Kind::kForbidden, "Authenticated user no longer exists");
        }
        const auto now = std::chrono::system_clock::now();
        bsoncxx::builder::basic::array hashtags;
        for (const auto& hashtag : extractHashtags(input.caption)) {
            hashtags.append(hashtag);
        }
        bsoncxx::builder::basic::array qualities;
        for (const auto& quality : input.qualities) {
            qualities.append(quality);
        }
        bsoncxx::builder::basic::document post;
        post.append(
            kvp("_id", input.id), kvp("caption", input.caption), kvp("topic", input.topic),
            kvp("userId", input.userId),
            kvp("media", make_document(kvp("id", input.mediaId), kvp("storageName", input.storageName),
                                       kvp("originalName", input.originalName),
                                       kvp("contentType", input.contentType),
                                       kvp("size", static_cast<std::int64_t>(input.size)))),
            kvp("hashtags", hashtags.extract()), kvp("qualities", qualities.extract()),
            kvp("likes", make_array()),
            kvp("comments", make_array()), kvp("viewCount", static_cast<std::int64_t>(0)),
            kvp("shareCount", static_cast<std::int64_t>(0)),
            kvp("createdAt", bsoncxx::types::b_date{now}),
            kvp("updatedAt", bsoncxx::types::b_date{now}));
        if (!input.thumbnailId.empty()) {
            const auto thumbnailType = input.thumbnailStorageName.ends_with(".png")
                                           ? "image/png"
                                           : input.thumbnailStorageName.ends_with(".webp")
                                                 ? "image/webp"
                                                 : "image/jpeg";
            post.append(kvp("thumbnail", make_document(
                kvp("id", input.thumbnailId), kvp("storageName", input.thumbnailStorageName),
                kvp("contentType", thumbnailType))));
        }
        if (!input.subtitleId.empty()) {
            post.append(kvp("subtitles", make_document(
                kvp("id", input.subtitleId), kvp("storageName", input.subtitleStorageName),
                kvp("contentType", "text/vtt"))));
        }
        database["posts"].insert_one(post.extract());
        for (const auto& follow :
             database["follows"].find(make_document(kvp("followingId", input.userId)))) {
            const auto followerId = getString(follow, "followerId");
            upsertNotification(database,
                               directedKey("new_post:" + input.id, input.userId, followerId),
                               followerId, input.userId, "new_post", input.id);
        }
        notifyMentions(input.caption, input.userId, input.id);
        const auto created = database["posts"].find_one(make_document(kvp("_id", input.id)));
        if (!created) {
            throw StoreError(StoreError::Kind::kDatabase, "Created post could not be read back");
        }
        return postToJson(database, created->view());
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

Json::Value MongoStore::setLike(const std::string& postId, const std::string& userId, bool like) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto posts = database["posts"];
        const auto existing = posts.find_one(make_document(kvp("_id", postId)));
        if (!existing) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto ownerId = getString(existing->view(), "userId");
        if (isBlocked(userId, ownerId)) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "You cannot interact with this account");
        }
        if (!interactionAllowed(userId, ownerId, "comment")) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "This account's privacy settings do not allow comments from you");
        }
        const auto update = like
                                ? make_document(kvp("$addToSet", make_document(kvp("likes", userId))),
                                                kvp("$set", make_document(kvp(
                                                                "updatedAt", bsoncxx::types::b_date{
                                                                                 std::chrono::system_clock::now()}))))
                                : make_document(kvp("$pull", make_document(kvp("likes", userId))),
                                                kvp("$set", make_document(kvp(
                                                                "updatedAt", bsoncxx::types::b_date{
                                                                                 std::chrono::system_clock::now()}))));
        const auto outcome = posts.update_one(make_document(kvp("_id", postId)), update.view());
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto notificationId = directedKey("post_like:" + postId, userId, ownerId);
        if (like) {
            upsertNotification(database, notificationId, ownerId, userId, "post_like", postId);
        } else {
            removeNotification(database, notificationId);
        }
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
    const auto post = getPost(postId);
    if (!post) {
        throw StoreError(StoreError::Kind::kNotFound, "Post not found");
    }
    return *post;
}

Json::Value MongoStore::addComment(const std::string& postId,
                                   const std::string& userId,
                                   const std::string& comment,
                                   const std::string& commentId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto posts = database["posts"];
        const auto existing = posts.find_one(make_document(kvp("_id", postId)));
        if (!existing) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto ownerId = getString(existing->view(), "userId");
        if (isBlocked(userId, ownerId)) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "You cannot interact with this account");
        }
        const auto now = std::chrono::system_clock::now();
        const auto outcome = posts.update_one(
            make_document(kvp("_id", postId)),
            make_document(kvp("$push", make_document(kvp(
                                             "comments", make_document(kvp("_id", commentId),
                                                                       kvp("text", comment),
                                                                       kvp("userId", userId),
                                                                       kvp("likes", make_array()),
                                                                       kvp("replies", make_array()),
                                                                       kvp("createdAt", bsoncxx::types::b_date{now}))))),
                          kvp("$set", make_document(kvp("updatedAt", bsoncxx::types::b_date{now})))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        upsertNotification(database, "comment:" + commentId, ownerId, userId,
                           "comment", postId, commentId);
        notifyMentions(comment, userId, postId, commentId);
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
    const auto post = getPost(postId);
    if (!post) {
        throw StoreError(StoreError::Kind::kNotFound, "Post not found");
    }
    return *post;
}

Json::Value MongoStore::setCommentLike(const std::string& postId,
                                       const std::string& commentId,
                                       const std::string& userId,
                                       bool like) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto posts = database["posts"];
        const auto existing = posts.find_one(make_document(kvp("_id", postId)));
        if (!existing) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto authorId = commentAuthor(existing->view(), commentId);
        if (!authorId) {
            throw StoreError(StoreError::Kind::kNotFound, "Comment not found");
        }
        if (isBlocked(userId, getString(existing->view(), "userId")) ||
            isBlocked(userId, *authorId)) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "You cannot interact with this account");
        }
        if (!interactionAllowed(userId, getString(existing->view(), "userId"), "comment")) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "This account's privacy settings do not allow replies from you");
        }
        const auto now = std::chrono::system_clock::now();
        const auto update = like
            ? make_document(kvp("$addToSet", make_document(kvp("comments.$.likes", userId))),
                            kvp("$set", make_document(kvp("updatedAt", bsoncxx::types::b_date{now}))))
            : make_document(kvp("$pull", make_document(kvp("comments.$.likes", userId))),
                            kvp("$set", make_document(kvp("updatedAt", bsoncxx::types::b_date{now}))));
        const auto outcome = posts.update_one(
            make_document(kvp("_id", postId), kvp("comments._id", commentId)), update.view());
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "Comment not found");
        }
        const auto notificationId = directedKey(
            "comment_like:" + postId + ":" + commentId, userId, *authorId);
        if (like) {
            upsertNotification(database, notificationId, *authorId, userId,
                               "comment_like", postId, commentId);
        } else {
            removeNotification(database, notificationId);
        }
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
    const auto post = getPost(postId);
    if (!post) {
        throw StoreError(StoreError::Kind::kNotFound, "Post not found");
    }
    return *post;
}

Json::Value MongoStore::addReply(const std::string& postId,
                                 const std::string& commentId,
                                 const std::string& userId,
                                 const std::string& reply,
                                 const std::string& replyId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto posts = database["posts"];
        const auto existing = posts.find_one(make_document(kvp("_id", postId)));
        if (!existing) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto authorId = commentAuthor(existing->view(), commentId);
        if (!authorId) {
            throw StoreError(StoreError::Kind::kNotFound, "Comment not found");
        }
        if (isBlocked(userId, getString(existing->view(), "userId")) ||
            isBlocked(userId, *authorId)) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "You cannot interact with this account");
        }
        const auto now = std::chrono::system_clock::now();
        const auto outcome = posts.update_one(
            make_document(kvp("_id", postId), kvp("comments._id", commentId)),
            make_document(
                kvp("$push", make_document(kvp(
                    "comments.$.replies",
                    make_document(kvp("_id", replyId), kvp("text", reply),
                                  kvp("userId", userId),
                                  kvp("createdAt", bsoncxx::types::b_date{now}))))),
                kvp("$set", make_document(kvp("updatedAt", bsoncxx::types::b_date{now})))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "Comment not found");
        }
        upsertNotification(database, "reply:" + replyId, *authorId, userId,
                           "reply", postId, commentId);
        notifyMentions(reply, userId, postId, commentId);
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
    const auto post = getPost(postId);
    if (!post) {
        throw StoreError(StoreError::Kind::kNotFound, "Post not found");
    }
    return *post;
}

Json::Value MongoStore::setCommentPinned(const std::string& postId,
                                         const std::string& commentId,
                                         const std::string& actingUserId,
                                         bool pinned) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto posts = database["posts"];
        const auto existing = posts.find_one(make_document(kvp("_id", postId)));
        if (!existing) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        if (getString(existing->view(), "userId") != actingUserId) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "Only the video owner can pin comments");
        }
        const auto authorId = commentAuthor(existing->view(), commentId);
        if (!authorId) {
            throw StoreError(StoreError::Kind::kNotFound, "Comment not found");
        }
        const auto notificationId = "comment_pin:" + postId + ":" + commentId;
        if (pinned) {
            const auto previousPinned = getString(existing->view(), "pinnedCommentId");
            if (!previousPinned.empty() && previousPinned != commentId) {
                removeNotification(database, "comment_pin:" + postId + ":" + previousPinned);
            }
            posts.update_one(make_document(kvp("_id", postId)),
                             make_document(kvp("$set", make_document(
                                 kvp("pinnedCommentId", commentId),
                                 kvp("updatedAt", bsoncxx::types::b_date{
                                                      std::chrono::system_clock::now()})))));
            upsertNotification(database, notificationId, *authorId, actingUserId,
                               "comment_pin", postId, commentId);
        } else {
            posts.update_one(
                make_document(kvp("_id", postId), kvp("pinnedCommentId", commentId)),
                make_document(kvp("$unset", make_document(kvp("pinnedCommentId", ""))),
                              kvp("$set", make_document(kvp(
                                  "updatedAt", bsoncxx::types::b_date{
                                                   std::chrono::system_clock::now()})))));
            removeNotification(database, notificationId);
        }
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
    const auto post = getPost(postId);
    if (!post) {
        throw StoreError(StoreError::Kind::kNotFound, "Post not found");
    }
    return *post;
}

MediaRecord MongoStore::deletePost(const std::string& postId, const std::string& actingUserId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        auto posts = database["posts"];
        const auto post = posts.find_one(make_document(kvp("_id", postId)));
        if (!post) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        if (getString(post->view(), "userId") != actingUserId) {
            throw StoreError(StoreError::Kind::kForbidden, "Only the post owner can delete it");
        }
        const auto media = mediaFromPost(post->view());
        auto completeMedia = media;
        const auto thumbnail = post->view()["thumbnail"];
        if (thumbnail && thumbnail.type() == bsoncxx::type::k_document) {
            completeMedia.thumbnailStorageName =
                getString(thumbnail.get_document().view(), "storageName");
        }
        const auto subtitles = post->view()["subtitles"];
        if (subtitles && subtitles.type() == bsoncxx::type::k_document) {
            completeMedia.subtitleStorageName =
                getString(subtitles.get_document().view(), "storageName");
        }
        const auto outcome = posts.delete_one(make_document(kvp("_id", postId), kvp("userId", actingUserId)));
        if (!outcome || outcome->deleted_count() != 1) {
            throw StoreError(StoreError::Kind::kConflict, "Post changed before it could be deleted");
        }
        database["notifications"].delete_many(make_document(kvp("postId", postId)));
        return completeMedia;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

std::optional<MediaRecord> MongoStore::getMedia(const std::string& mediaId) {
    try {
        auto client = pool_.acquire();
        const auto post = (*client)[database_]["posts"].find_one(make_document(
            kvp("$or", make_array(make_document(kvp("media.id", mediaId)),
                                  make_document(kvp("thumbnail.id", mediaId)),
                                  make_document(kvp("subtitles.id", mediaId))))));
        if (!post) {
            return std::nullopt;
        }
        const auto view = post->view();
        if (getString(view["media"].get_document().view(), "id") == mediaId) {
            return mediaFromPost(view);
        }
        const auto thumbnail = view["thumbnail"];
        if (thumbnail && thumbnail.type() == bsoncxx::type::k_document &&
            getString(thumbnail.get_document().view(), "id") == mediaId) {
            const auto asset = thumbnail.get_document().view();
            return MediaRecord{mediaId, getString(asset, "storageName"),
                               "thumbnail.jpg", getString(asset, "contentType", "image/jpeg"), 0};
        }
        const auto subtitles = view["subtitles"];
        if (subtitles && subtitles.type() == bsoncxx::type::k_document) {
            const auto asset = subtitles.get_document().view();
            return MediaRecord{mediaId, getString(asset, "storageName"),
                               "subtitles.vtt", getString(asset, "contentType", "text/vtt"), 0};
        }
        return std::nullopt;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseError(error);
    }
}

}  // namespace tiktok
