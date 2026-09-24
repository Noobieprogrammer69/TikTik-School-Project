#include "tiktok/MongoStore.h"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/update.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace tiktok {
namespace {

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;

std::string stringField(const bsoncxx::document::view& document,
                        std::string_view key,
                        std::string fallback = {}) {
    const auto value = document[key];
    return value && value.type() == bsoncxx::type::k_string
               ? std::string(value.get_string().value)
               : std::move(fallback);
}

std::int64_t integerField(const bsoncxx::document::view& document,
                          std::string_view key,
                          std::int64_t fallback = 0) {
    const auto value = document[key];
    if (!value) return fallback;
    if (value.type() == bsoncxx::type::k_int64) return value.get_int64().value;
    if (value.type() == bsoncxx::type::k_int32) return value.get_int32().value;
    return fallback;
}

bool boolField(const bsoncxx::document::view& document,
               std::string_view key,
               bool fallback = false) {
    const auto value = document[key];
    return value && value.type() == bsoncxx::type::k_bool
               ? value.get_bool().value
               : fallback;
}

std::string relationKey(std::string_view prefix,
                        const std::string& owner,
                        const std::string& target) {
    return std::string(prefix) + ":" + std::to_string(owner.size()) + ":" + owner + target;
}

StoreError databaseFailure(const std::exception& error) {
    return StoreError(StoreError::Kind::kDatabase,
                      std::string("MongoDB operation failed: ") + error.what());
}

std::optional<std::string> commentOwner(const bsoncxx::document::view& post,
                                        const std::string& commentId) {
    const auto comments = post["comments"];
    if (!comments || comments.type() != bsoncxx::type::k_array) return std::nullopt;
    for (const auto& value : comments.get_array().value) {
        if (value.type() != bsoncxx::type::k_document) continue;
        const auto comment = value.get_document().view();
        if (stringField(comment, "_id") == commentId) {
            return stringField(comment, "userId");
        }
    }
    return std::nullopt;
}

std::optional<std::string> replyOwner(const bsoncxx::document::view& post,
                                      const std::string& commentId,
                                      const std::string& replyId) {
    const auto comments = post["comments"];
    if (!comments || comments.type() != bsoncxx::type::k_array) return std::nullopt;
    for (const auto& value : comments.get_array().value) {
        if (value.type() != bsoncxx::type::k_document) continue;
        const auto comment = value.get_document().view();
        if (stringField(comment, "_id") != commentId) continue;
        const auto replies = comment["replies"];
        if (!replies || replies.type() != bsoncxx::type::k_array) return std::nullopt;
        for (const auto& replyValue : replies.get_array().value) {
            if (replyValue.type() != bsoncxx::type::k_document) continue;
            const auto reply = replyValue.get_document().view();
            if (stringField(reply, "_id") == replyId) return stringField(reply, "userId");
        }
    }
    return std::nullopt;
}

Json::Value defaultPreferences() {
    Json::Value result(Json::objectValue);
    for (const auto* type : {"follow", "follow_request", "follow_accepted", "new_post", "post_like", "comment", "reply",
                             "comment_like", "comment_pin", "post_share", "mention", "message"}) {
        result[type] = true;
    }
    return result;
}

}  // namespace

Json::Value MongoStore::updateProfile(const std::string& userId,
                                      const ProfileUpdate& update) {
    try {
        auto client = pool_.acquire();
        auto users = (*client)[database_]["users"];
        const auto outcome = users.update_one(
            make_document(kvp("_id", userId)),
            make_document(kvp("$set", make_document(
                kvp("userName", update.userName), kvp("bio", update.bio),
                kvp("profileCustomized", true),
                kvp("updatedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        const auto user = users.find_one(make_document(kvp("_id", userId)));
        return userToJson(user->view());
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::updateAvatar(const std::string& userId,
                                     const std::string& storageName,
                                     const std::string& contentType) {
    try {
        auto client = pool_.acquire();
        auto users = (*client)[database_]["users"];
        const auto outcome = users.update_one(
            make_document(kvp("_id", userId)),
            make_document(kvp("$set", make_document(
                kvp("image", "/api/avatars/" + userId + "?v=" + storageName),
                kvp("avatar", make_document(kvp("storageName", storageName),
                                              kvp("contentType", contentType))),
                kvp("updatedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        return userToJson(users.find_one(make_document(kvp("_id", userId)))->view());
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

std::optional<StoredAsset> MongoStore::getAvatar(const std::string& userId) {
    try {
        auto client = pool_.acquire();
        const auto user = (*client)[database_]["users"].find_one(make_document(kvp("_id", userId)));
        if (!user) return std::nullopt;
        const auto avatar = user->view()["avatar"];
        if (!avatar || avatar.type() != bsoncxx::type::k_document) return std::nullopt;
        const auto value = avatar.get_document().view();
        return StoredAsset{"", stringField(value, "storageName"),
                           stringField(value, "contentType", "application/octet-stream"), 0};
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

bool MongoStore::isSuspended(const std::string& userId) {
    try {
        auto client = pool_.acquire();
        const auto user = (*client)[database_]["users"].find_one(make_document(kvp("_id", userId)));
        return user && boolField(user->view(), "suspended");
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::listFeed(const std::string& viewerId,
                                 const std::string& mode,
                                 const std::optional<std::string>& topic,
                                 const std::optional<std::string>& hashtag,
                                 const std::string& cursor,
                                 std::size_t limit) {
    auto candidates = listPosts(topic, std::nullopt, 250);
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        std::unordered_set<std::string> following;
        std::unordered_set<std::string> hidden;
        std::unordered_set<std::string> hiddenPosts;
        std::unordered_set<std::string> saved;
        std::unordered_set<std::string> preferredTopics;
        std::unordered_set<std::string> privateCreators;
        for (const auto& value : database["users"].find(
                 make_document(kvp("privacy.privateAccount", true)))) {
            privateCreators.insert(stringField(value, "_id"));
        }
        if (!viewerId.empty()) {
            for (const auto& value : database["follows"].find(make_document(kvp("followerId", viewerId)))) {
                following.insert(stringField(value, "followingId"));
            }
            for (const auto& value : database["mutes"].find(make_document(kvp("ownerId", viewerId)))) {
                hidden.insert(stringField(value, "targetUserId"));
            }
            for (const auto& value : database["blocks"].find(make_document(
                     kvp("$or", make_array(make_document(kvp("ownerId", viewerId)),
                                           make_document(kvp("targetUserId", viewerId))))))) {
                const auto owner = stringField(value, "ownerId");
                hidden.insert(owner == viewerId ? stringField(value, "targetUserId") : owner);
            }
            for (const auto& value : database["feed_feedback"].find(
                     make_document(kvp("userId", viewerId)))) {
                const auto type = stringField(value, "type");
                if (type == "not_interested") hiddenPosts.insert(stringField(value, "postId"));
                if (type == "hide_creator") hidden.insert(stringField(value, "targetUserId"));
            }
            for (const auto& value : database["saves"].find(make_document(kvp("userId", viewerId)))) {
                saved.insert(stringField(value, "postId"));
            }
            mongocxx::options::find historyOptions;
            historyOptions.sort(make_document(kvp("viewedAt", -1)));
            historyOptions.limit(30);
            for (const auto& history : database["view_history"].find(
                     make_document(kvp("userId", viewerId)), historyOptions)) {
                const auto post = database["posts"].find_one(
                    make_document(kvp("_id", stringField(history, "postId"))));
                if (post) preferredTopics.insert(stringField(post->view(), "topic"));
            }
        }

        std::vector<Json::Value> filtered;
        for (auto& post : candidates) {
            const auto ownerId = post["userId"].asString();
            if (hidden.contains(ownerId) || hiddenPosts.contains(post["_id"].asString())) continue;
            if (privateCreators.contains(ownerId) && ownerId != viewerId &&
                !following.contains(ownerId)) continue;
            if (mode == "following" && !following.contains(ownerId)) continue;
            if (hashtag && !hashtag->empty()) {
                bool matches = false;
                for (const auto& value : post["hashtags"]) {
                    if (value.asString() == *hashtag) matches = true;
                }
                if (!matches) continue;
            }
            post["saved"] = saved.contains(post["_id"].asString());
            if (mode == "for_you") {
                if (following.contains(ownerId)) post["recommendationReason"] = "From someone you follow";
                else if (preferredTopics.contains(post["topic"].asString())) post["recommendationReason"] = "Because you watched similar videos";
                else post["recommendationReason"] = "Popular and recent";
            }
            filtered.push_back(post);
        }
        if (mode == "for_you" && !viewerId.empty()) {
            std::stable_sort(filtered.begin(), filtered.end(), [&](const auto& left, const auto& right) {
                const auto score = [&](const auto& post) {
                    std::int64_t value = static_cast<std::int64_t>(post["likes"].size());
                    if (following.contains(post["userId"].asString())) value += 1'000;
                    if (preferredTopics.contains(post["topic"].asString())) value += 250;
                    return value;
                };
                return score(left) > score(right);
            });
        }
        std::size_t start = 0;
        if (!cursor.empty()) {
            const auto found = std::find_if(filtered.begin(), filtered.end(), [&](const auto& post) {
                return post["_id"].asString() == cursor;
            });
            if (found != filtered.end()) start = static_cast<std::size_t>(found - filtered.begin()) + 1;
        }
        Json::Value result(Json::objectValue);
        result["items"] = Json::Value(Json::arrayValue);
        const auto finish = std::min(filtered.size(), start + limit);
        for (auto index = start; index < finish; ++index) result["items"].append(filtered[index]);
        result["nextCursor"] = finish < filtered.size() && finish > start
                                   ? filtered[finish - 1]["_id"].asString()
                                   : "";
        return result;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

bool MongoStore::isBlocked(const std::string& firstUserId,
                           const std::string& secondUserId) {
    if (firstUserId.empty() || secondUserId.empty()) return false;
    try {
        auto client = pool_.acquire();
        return static_cast<bool>((*client)[database_]["blocks"].find_one(make_document(
            kvp("$or", make_array(
                make_document(kvp("ownerId", firstUserId), kvp("targetUserId", secondUserId)),
                make_document(kvp("ownerId", secondUserId), kvp("targetUserId", firstUserId)))))));
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::getSafetyRelationship(const std::string& actingUserId,
                                               const std::string& targetUserId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        Json::Value result(Json::objectValue);
        result["blocked"] = static_cast<bool>(database["blocks"].find_one(
            make_document(kvp("ownerId", actingUserId), kvp("targetUserId", targetUserId))));
        result["blockedBy"] = static_cast<bool>(database["blocks"].find_one(
            make_document(kvp("ownerId", targetUserId), kvp("targetUserId", actingUserId))));
        result["muted"] = static_cast<bool>(database["mutes"].find_one(
            make_document(kvp("ownerId", actingUserId), kvp("targetUserId", targetUserId))));
        return result;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::setBlocked(const std::string& actingUserId,
                                   const std::string& targetUserId,
                                   bool blocked) {
    if (actingUserId == targetUserId) {
        throw StoreError(StoreError::Kind::kConflict, "You cannot block yourself");
    }
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["users"].find_one(make_document(kvp("_id", targetUserId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        const auto id = relationKey("block", actingUserId, targetUserId);
        if (blocked) {
            mongocxx::options::update options;
            options.upsert(true);
            database["blocks"].update_one(
                make_document(kvp("_id", id)),
                make_document(kvp("$setOnInsert", make_document(
                    kvp("ownerId", actingUserId), kvp("targetUserId", targetUserId),
                    kvp("createdAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))),
                options);
            database["follows"].delete_many(make_document(kvp("$or", make_array(
                make_document(kvp("followerId", actingUserId), kvp("followingId", targetUserId)),
                make_document(kvp("followerId", targetUserId), kvp("followingId", actingUserId))))));
            database["notifications"].delete_many(make_document(kvp("$or", make_array(
                make_document(kvp("userId", actingUserId), kvp("actorId", targetUserId)),
                make_document(kvp("userId", targetUserId), kvp("actorId", actingUserId))))));
        } else {
            database["blocks"].delete_one(make_document(kvp("_id", id)));
        }
        return getSafetyRelationship(actingUserId, targetUserId);
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::setMuted(const std::string& actingUserId,
                                 const std::string& targetUserId,
                                 bool muted) {
    if (actingUserId == targetUserId) {
        throw StoreError(StoreError::Kind::kConflict, "You cannot mute yourself");
    }
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["users"].find_one(make_document(kvp("_id", targetUserId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        auto collection = database["mutes"];
        const auto id = relationKey("mute", actingUserId, targetUserId);
        if (muted) {
            mongocxx::options::update options;
            options.upsert(true);
            collection.update_one(make_document(kvp("_id", id)),
                make_document(kvp("$setOnInsert", make_document(
                    kvp("ownerId", actingUserId), kvp("targetUserId", targetUserId),
                    kvp("createdAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))), options);
        } else {
            collection.delete_one(make_document(kvp("_id", id)));
        }
        return getSafetyRelationship(actingUserId, targetUserId);
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::setSaved(const std::string& postId,
                                 const std::string& userId,
                                 bool savedValue) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["posts"].find_one(make_document(kvp("_id", postId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto id = relationKey("save", userId, postId);
        if (savedValue) {
            mongocxx::options::update options;
            options.upsert(true);
            database["saves"].update_one(make_document(kvp("_id", id)),
                make_document(kvp("$setOnInsert", make_document(
                    kvp("userId", userId), kvp("postId", postId),
                    kvp("createdAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))), options);
        } else {
            database["saves"].delete_one(make_document(kvp("_id", id)));
        }
        Json::Value result(Json::objectValue);
        result["saved"] = savedValue;
        result["postId"] = postId;
        return result;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::listSaved(const std::string& userId, std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        Json::Value result(Json::arrayValue);
        for (const auto& saved : database["saves"].find(make_document(kvp("userId", userId)), options)) {
            const auto post = database["posts"].find_one(make_document(kvp("_id", stringField(saved, "postId"))));
            if (post) {
                auto value = postToJson(database, post->view());
                value["saved"] = true;
                result.append(std::move(value));
            }
        }
        return result;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::recordView(const std::string& postId,
                                   const std::string& userId,
                                   std::uint32_t watchSeconds) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        if (!database["posts"].find_one(make_document(kvp("_id", postId)))) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto now = std::chrono::system_clock::now();
        if (!userId.empty()) {
            const auto id = relationKey("view", userId, postId);
            mongocxx::options::update options;
            options.upsert(true);
            database["view_history"].update_one(make_document(kvp("_id", id)),
                make_document(kvp("$set", make_document(
                                  kvp("userId", userId), kvp("postId", postId),
                                  kvp("watchSeconds", static_cast<std::int64_t>(watchSeconds)),
                                  kvp("viewedAt", bsoncxx::types::b_date{now}))),
                              kvp("$setOnInsert", make_document(
                                  kvp("createdAt", bsoncxx::types::b_date{now})))),
                options);
        }
        // History is one row per user/video, but views are events. Every explicit
        // open/play/loop report increments the public counter, including repeats
        // from the same account.
        database["posts"].update_one(make_document(kvp("_id", postId)),
            make_document(kvp("$inc", make_document(kvp("viewCount", 1)))));
        const auto post = database["posts"].find_one(make_document(kvp("_id", postId)));
        Json::Value result(Json::objectValue);
        result["recorded"] = true;
        result["viewCount"] = static_cast<Json::UInt64>(
            std::max<std::int64_t>(0, integerField(post->view(), "viewCount")));
        return result;
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::listHistory(const std::string& userId, std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        mongocxx::options::find options;
        options.sort(make_document(kvp("viewedAt", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        Json::Value result(Json::arrayValue);
        for (const auto& history : database["view_history"].find(make_document(kvp("userId", userId)), options)) {
            const auto post = database["posts"].find_one(make_document(kvp("_id", stringField(history, "postId"))));
            if (post) result.append(postToJson(database, post->view()));
        }
        return result;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::recordShare(const std::string& postId, const std::string& userId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto existing = database["posts"].find_one(make_document(kvp("_id", postId)));
        if (!existing) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        const auto ownerId = stringField(existing->view(), "userId");
        const auto outcome = database["posts"].update_one(make_document(kvp("_id", postId)),
            make_document(kvp("$inc", make_document(kvp("shareCount", 1)))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        }
        database["share_events"].insert_one(make_document(
            kvp("postId", postId), kvp("userId", userId),
            kvp("createdAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})));
        upsertNotification(database,
                           relationKey("post_share:" + postId + ":", userId, ownerId),
                           ownerId, userId, "post_share", postId);
        return *getPost(postId);
    } catch (const StoreError&) {
        throw;
    } catch (const std::exception& error) {
        throw databaseFailure(error);
    }
}

Json::Value MongoStore::editComment(const std::string& postId,
                                    const std::string& commentId,
                                    const std::string& actingUserId,
                                    const std::string& text) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto filter = make_document(
            kvp("_id", postId),
            kvp("comments", make_document(kvp("$elemMatch", make_document(
                kvp("_id", commentId), kvp("userId", actingUserId))))));
        const auto update = make_document(kvp("$set", make_document(
            kvp("comments.$.text", text),
            kvp("comments.$.editedAt",
                bsoncxx::types::b_date{std::chrono::system_clock::now()}))));
        const auto outcome = database["posts"].update_one(filter.view(), update.view());
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kForbidden, "Only the comment author can edit it");
        }
        return *getPost(postId);
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::deleteComment(const std::string& postId,
                                      const std::string& commentId,
                                      const std::string& actingUserId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto post = database["posts"].find_one(make_document(kvp("_id", postId)));
        if (!post) throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        const auto author = commentOwner(post->view(), commentId);
        if (!author) throw StoreError(StoreError::Kind::kNotFound, "Comment not found");
        if (*author != actingUserId && stringField(post->view(), "userId") != actingUserId) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "Only the comment author or video owner can remove it");
        }
        database["posts"].update_one(make_document(kvp("_id", postId)),
            make_document(kvp("$pull", make_document(kvp(
                "comments", make_document(kvp("_id", commentId)))))));
        database["posts"].update_one(
            make_document(kvp("_id", postId), kvp("pinnedCommentId", commentId)),
            make_document(kvp("$unset", make_document(kvp("pinnedCommentId", "")))));
        database["notifications"].delete_many(make_document(kvp("commentId", commentId)));
        return *getPost(postId);
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::editReply(const std::string& postId,
                                  const std::string& commentId,
                                  const std::string& replyId,
                                  const std::string& actingUserId,
                                  const std::string& text) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto post = database["posts"].find_one(make_document(kvp("_id", postId)));
        if (!post) throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        const auto owner = replyOwner(post->view(), commentId, replyId);
        if (!owner || *owner != actingUserId) {
            throw StoreError(StoreError::Kind::kForbidden, "Only the reply author can edit it");
        }
        mongocxx::options::update options;
        options.array_filters(make_array(make_document(kvp("c._id", commentId)),
                                         make_document(kvp("r._id", replyId))));
        database["posts"].update_one(make_document(kvp("_id", postId)),
            make_document(kvp("$set", make_document(
                kvp("comments.$[c].replies.$[r].text", text),
                kvp("comments.$[c].replies.$[r].editedAt",
                    bsoncxx::types::b_date{std::chrono::system_clock::now()})))), options);
        return *getPost(postId);
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::deleteReply(const std::string& postId,
                                    const std::string& commentId,
                                    const std::string& replyId,
                                    const std::string& actingUserId) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto post = database["posts"].find_one(make_document(kvp("_id", postId)));
        if (!post) throw StoreError(StoreError::Kind::kNotFound, "Post not found");
        const auto owner = replyOwner(post->view(), commentId, replyId);
        if (!owner) throw StoreError(StoreError::Kind::kNotFound, "Reply not found");
        if (*owner != actingUserId && stringField(post->view(), "userId") != actingUserId) {
            throw StoreError(StoreError::Kind::kForbidden,
                             "Only the reply author or video owner can remove it");
        }
        mongocxx::options::update options;
        options.array_filters(make_array(make_document(kvp("c._id", commentId))));
        database["posts"].update_one(make_document(kvp("_id", postId)),
            make_document(kvp("$pull", make_document(kvp(
                "comments.$[c].replies", make_document(kvp("_id", replyId)))))), options);
        database["notifications"].delete_many(make_document(kvp("_id", "reply:" + replyId)));
        return *getPost(postId);
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::setMessageReaction(const std::string& messageId,
                                           const std::string& userId,
                                           const std::string& emoji) {
    try {
        auto client = pool_.acquire();
        auto messages = (*client)[database_]["messages"];
        const auto message = messages.find_one(make_document(kvp("_id", messageId), kvp("participants", userId)));
        if (!message) throw StoreError(StoreError::Kind::kNotFound, "Message not found");
        messages.update_one(make_document(kvp("_id", messageId)),
            make_document(kvp("$pull", make_document(kvp("reactions", make_document(kvp("userId", userId)))))));
        if (!emoji.empty()) {
            messages.update_one(make_document(kvp("_id", messageId)),
                make_document(kvp("$addToSet", make_document(kvp("reactions", make_document(
                    kvp("userId", userId), kvp("emoji", emoji)))))));
        }
        return messageToJson(messages.find_one(make_document(kvp("_id", messageId)))->view());
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::deleteMessage(const std::string& messageId,
                                      const std::string& userId) {
    try {
        auto client = pool_.acquire();
        auto messages = (*client)[database_]["messages"];
        const auto before = messages.find_one(make_document(
            kvp("_id", messageId), kvp("senderId", userId),
            kvp("deletedAt", make_document(kvp("$exists", false)))));
        if (!before) {
            throw StoreError(StoreError::Kind::kForbidden, "Only the sender can delete this message");
        }
        std::string attachmentStorageName;
        const auto attachment = before->view()["attachment"];
        if (attachment && attachment.type() == bsoncxx::type::k_document) {
            attachmentStorageName = stringField(attachment.get_document().view(), "storageName");
        }
        const auto outcome = messages.update_one(
            make_document(kvp("_id", messageId), kvp("senderId", userId),
                          kvp("deletedAt", make_document(kvp("$exists", false)))),
            make_document(kvp("$set", make_document(
                kvp("deletedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()}))),
                          kvp("$unset", make_document(kvp("attachment", "")))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kForbidden, "Only the sender can delete this message");
        }
        auto result = messageToJson(messages.find_one(make_document(kvp("_id", messageId)))->view());
        if (!attachmentStorageName.empty()) result["deletedAttachmentStorageName"] = attachmentStorageName;
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::setConversationSettings(const std::string& userId,
                                                const std::string& otherUserId,
                                                bool muted,
                                                bool archived) {
    try {
        auto client = pool_.acquire();
        auto settings = (*client)[database_]["conversation_settings"];
        mongocxx::options::update options;
        options.upsert(true);
        settings.update_one(make_document(kvp("userId", userId), kvp("otherUserId", otherUserId)),
            make_document(kvp("$set", make_document(
                kvp("muted", muted), kvp("archived", archived),
                kvp("updatedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()}))),
                          kvp("$setOnInsert", make_document(
                              kvp("_id", relationKey("conversation", userId, otherUserId)),
                              kvp("userId", userId), kvp("otherUserId", otherUserId)))), options);
        Json::Value result(Json::objectValue);
        result["muted"] = muted;
        result["archived"] = archived;
        return result;
    } catch (const std::exception& error) { throw databaseFailure(error); }
}

std::optional<StoredAsset> MongoStore::getChatAsset(const std::string& mediaId,
                                                    const std::string& userId) {
    try {
        auto client = pool_.acquire();
        const auto message = (*client)[database_]["messages"].find_one(
            make_document(kvp("attachment.id", mediaId), kvp("participants", userId),
                          kvp("deletedAt", make_document(kvp("$exists", false)))));
        if (!message) return std::nullopt;
        const auto attachment = message->view()["attachment"].get_document().view();
        return StoredAsset{mediaId, stringField(attachment, "storageName"),
                           stringField(attachment, "contentType"), 0};
    } catch (const std::exception& error) { throw databaseFailure(error); }
}

std::size_t MongoStore::markNotificationRead(const std::string& userId,
                                             const std::string& notificationId) {
    try {
        auto client = pool_.acquire();
        const auto result = (*client)[database_]["notifications"].update_one(
            make_document(kvp("_id", notificationId), kvp("userId", userId),
                          kvp("readAt", make_document(kvp("$exists", false)))),
            make_document(kvp("$set", make_document(
                kvp("readAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        return result ? static_cast<std::size_t>(result->modified_count()) : 0;
    } catch (const std::exception& error) { throw databaseFailure(error); }
}

std::size_t MongoStore::deleteNotification(const std::string& userId,
                                           const std::string& notificationId) {
    try {
        auto client = pool_.acquire();
        const auto result = (*client)[database_]["notifications"].delete_one(
            make_document(kvp("_id", notificationId), kvp("userId", userId)));
        return result ? static_cast<std::size_t>(result->deleted_count()) : 0;
    } catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::getNotificationPreferences(const std::string& userId) {
    auto result = defaultPreferences();
    try {
        auto client = pool_.acquire();
        const auto user = (*client)[database_]["users"].find_one(make_document(kvp("_id", userId)));
        if (!user) throw StoreError(StoreError::Kind::kNotFound, "User not found");
        const auto preferences = user->view()["notificationPreferences"];
        if (preferences && preferences.type() == bsoncxx::type::k_document) {
            const auto value = preferences.get_document().view();
            for (const auto& key : result.getMemberNames()) {
                const auto enabled = value[key];
                if (enabled && enabled.type() == bsoncxx::type::k_bool) {
                    result[key] = enabled.get_bool().value;
                }
            }
        }
        return result;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::updateNotificationPreferences(const std::string& userId,
                                                       const Json::Value& preferences) {
    auto normalized = defaultPreferences();
    for (const auto& key : normalized.getMemberNames()) {
        if (preferences.isMember(key) && preferences[key].isBool()) normalized[key] = preferences[key];
    }
    try {
        auto client = pool_.acquire();
        bsoncxx::builder::basic::document fields;
        for (const auto& key : normalized.getMemberNames()) fields.append(kvp(key, normalized[key].asBool()));
        const auto outcome = (*client)[database_]["users"].update_one(
            make_document(kvp("_id", userId)),
            make_document(kvp("$set", make_document(kvp("notificationPreferences", fields.extract())))));
        if (!outcome || outcome->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        return normalized;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::listReports(const std::string& status, std::size_t limit) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        mongocxx::options::find options;
        options.sort(make_document(kvp("createdAt", -1)));
        options.limit(static_cast<std::int64_t>(limit));
        bsoncxx::builder::basic::document filter;
        if (!status.empty() && status != "all") filter.append(kvp("status", status));
        Json::Value result(Json::arrayValue);
        for (const auto& report : database["reports"].find(filter.view(), options)) {
            Json::Value value(Json::objectValue);
            value["_id"] = stringField(report, "_id");
            value["reason"] = stringField(report, "reason");
            value["details"] = stringField(report, "details");
            value["status"] = stringField(report, "status", "open");
            const auto reporter = database["users"].find_one(make_document(kvp("_id", stringField(report, "reporterId"))));
            const auto target = database["users"].find_one(make_document(kvp("_id", stringField(report, "targetUserId"))));
            if (reporter) value["reporter"] = userToJson(reporter->view());
            if (target) value["target"] = userToJson(target->view());
            result.append(std::move(value));
        }
        return result;
    } catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::updateReportStatus(const std::string& reportId,
                                           const std::string& moderatorId,
                                           const std::string& status) {
    try {
        auto client = pool_.acquire();
        const auto result = (*client)[database_]["reports"].update_one(
            make_document(kvp("_id", reportId)),
            make_document(kvp("$set", make_document(
                kvp("status", status), kvp("moderatorId", moderatorId),
                kvp("reviewedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        if (!result || result->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "Report not found");
        }
        Json::Value response(Json::objectValue);
        response["reportId"] = reportId;
        response["status"] = status;
        return response;
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::setUserSuspended(const std::string& userId,
                                         const std::string& moderatorId,
                                         bool suspended) {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        const auto result = database["users"].update_one(make_document(kvp("_id", userId)),
            make_document(kvp("$set", make_document(
                kvp("suspended", suspended), kvp("moderatorId", moderatorId),
                kvp("moderatedAt", bsoncxx::types::b_date{std::chrono::system_clock::now()})))));
        if (!result || result->matched_count() == 0) {
            throw StoreError(StoreError::Kind::kNotFound, "User not found");
        }
        if (suspended) database["sessions"].delete_many(make_document(kvp("userId", userId)));
        return userToJson(database["users"].find_one(make_document(kvp("_id", userId)))->view());
    } catch (const StoreError&) { throw; }
    catch (const std::exception& error) { throw databaseFailure(error); }
}

Json::Value MongoStore::adminStatistics() {
    try {
        auto client = pool_.acquire();
        auto database = (*client)[database_];
        Json::Value result(Json::objectValue);
        result["users"] = static_cast<Json::UInt64>(database["users"].count_documents({}));
        result["videos"] = static_cast<Json::UInt64>(database["posts"].count_documents({}));
        result["messages"] = static_cast<Json::UInt64>(database["messages"].count_documents({}));
        result["follows"] = static_cast<Json::UInt64>(database["follows"].count_documents({}));
        result["openReports"] = static_cast<Json::UInt64>(
            database["reports"].count_documents(make_document(kvp("status", "open"))));
        Json::UInt64 totalViews = 0;
        for (const auto& post : database["posts"].find({})) {
            totalViews += static_cast<Json::UInt64>(
                std::max<std::int64_t>(0, integerField(post, "viewCount")));
        }
        result["views"] = totalViews;
        result["saves"] = static_cast<Json::UInt64>(database["saves"].count_documents({}));
        Json::Value topics(Json::objectValue);
        for (const auto& post : database["posts"].find({})) {
            const auto topic = stringField(post, "topic", "Other");
            topics[topic] = topics.get(topic, 0).asUInt64() + 1;
        }
        result["topics"] = std::move(topics);
        return result;
    } catch (const std::exception& error) { throw databaseFailure(error); }
}

}  // namespace tiktok
