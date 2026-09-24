#include "tiktok/MongoStore.h"

#include <catch2/catch_test_macros.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>

namespace {

constexpr auto kDatabase = "tiktok_clone_integration_test";

const std::string& testMongoUri() {
    static const std::string uri = [] {
        const auto* value = std::getenv("TIKTOK_TEST_MONGODB_URI");
        return value && *value ? std::string(value) : std::string{};
    }();
    return uri;
}

mongocxx::instance& mongoInstance() {
    static mongocxx::instance instance{};
    return instance;
}

void requireTestMongo() {
    (void)mongoInstance();
    if (testMongoUri().empty()) {
        SKIP("Set TIKTOK_TEST_MONGODB_URI to run isolated MongoDB integration tests");
    }
}

void resetDatabase() {
    mongocxx::client client{mongocxx::uri{testMongoUri()}};
    client[kDatabase].drop();
}

tiktok::PostInput samplePost() {
    return {"post:alice", "test:alice", "A school integration video", "Gaming",
            "media:alice", "integration.mp4", "integration.mp4", "video/mp4", 16};
}

}  // namespace

TEST_CASE("MongoDB persists users, sessions, and social operations") {
    requireTestMongo();
    resetDatabase();

    tiktok::MongoStore store(testMongoUri(), kDatabase);
    store.ensureIndexes();
    store.upsertUser({"test:alice", "Alice", "/avatar.svg", "test", "test:alice", true});
    store.upsertUser({"test:bob", "Bob", "/avatar.svg", "test", "test:bob", true});

    SECTION("a second store sees persisted data") {
        tiktok::MongoStore reopened(testMongoUri(), kDatabase);
        const auto alice = reopened.getUser("test:alice");
        REQUIRE(alice);
        REQUIRE((*alice)["userName"].asString() == "Alice");
    }

    SECTION("expired and revoked sessions cannot be restored") {
        const auto now = std::chrono::system_clock::now();
        store.createSession({"expired-token", "test:alice", "csrf", now - std::chrono::seconds{1}});
        REQUIRE_FALSE(store.getSession("expired-token"));

        store.createSession({"active-token", "test:alice", "csrf", now + std::chrono::minutes{5}});
        REQUIRE(store.getSession("active-token"));
        store.revokeSession("active-token");
        REQUIRE_FALSE(store.getSession("active-token"));
    }

    SECTION("likes are idempotent, comments persist, and deletion checks ownership") {
        store.createPost(samplePost());
        auto post = store.setLike("post:alice", "test:bob", true);
        REQUIRE(post["likes"].size() == 1);
        post = store.setLike("post:alice", "test:bob", true);
        REQUIRE(post["likes"].size() == 1);

        post = store.addComment("post:alice", "test:bob", "Great migration!", "comment:bob");
        REQUIRE(post["comments"].size() == 1);
        REQUIRE(post["comments"][0]["comment"].asString() == "Great migration!");

        const auto bob = store.getProfile("test:bob");
        REQUIRE(bob["userLikedVideos"].size() == 1);
        REQUIRE(store.search("school")["videos"].size() == 1);

        REQUIRE_THROWS_AS(store.deletePost("post:alice", "test:bob"), tiktok::StoreError);
        REQUIRE(store.getPost("post:alice"));
        const auto media = store.deletePost("post:alice", "test:alice");
        REQUIRE(media.id == "media:alice");
        REQUIRE_FALSE(store.getPost("post:alice"));
    }

    SECTION("private messages persist, preserve participants, and track unread state") {
        auto sent = store.createMessage(
            {"message:alice", "test:alice", "test:bob", "Hello Bob"});
        REQUIRE(sent["senderId"].asString() == "test:alice");
        REQUIRE(sent["recipientId"].asString() == "test:bob");
        REQUIRE_FALSE(sent["read"].asBool());

        const auto bobConversations = store.listConversations("test:bob");
        REQUIRE(bobConversations.size() == 1);
        REQUIRE(bobConversations[0]["user"]["_id"].asString() == "test:alice");
        REQUIRE(bobConversations[0]["unreadCount"].asUInt64() == 1);

        const auto messages = store.listMessages("test:bob", "test:alice");
        REQUIRE(messages.size() == 1);
        REQUIRE(messages[0]["text"].asString() == "Hello Bob");
        REQUIRE(store.markConversationRead("test:bob", "test:alice") == 1);
        REQUIRE(store.markConversationRead("test:bob", "test:alice") == 0);
        REQUIRE(store.listConversations("test:bob")[0]["unreadCount"].asUInt64() == 0);

        tiktok::MongoStore reopened(testMongoUri(), kDatabase);
        REQUIRE(reopened.listMessages("test:alice", "test:bob").size() == 1);
        REQUIRE_THROWS_AS(
            store.createMessage({"message:self", "test:alice", "test:alice", "No"}),
            tiktok::StoreError);
        REQUIRE_THROWS_AS(
            store.createMessage({"message:missing", "test:alice", "test:missing", "No"}),
            tiktok::StoreError);
    }

    SECTION("follows, reports, comment interactions, and notifications persist safely") {
        auto relationship = store.setFollow("test:alice", "test:bob", true);
        REQUIRE(relationship["following"].asBool());
        REQUIRE(relationship["followerCount"].asUInt64() == 1);
        relationship = store.setFollow("test:alice", "test:bob", true);
        REQUIRE(relationship["followerCount"].asUInt64() == 1);
        REQUIRE_THROWS_AS(store.setFollow("test:alice", "test:alice", true),
                          tiktok::StoreError);

        const tiktok::PostInput bobPost{
            "post:bob", "test:bob", "Bob's new video", "Gaming",
            "media:bob", "bob.mp4", "bob.mp4", "video/mp4", 20};
        auto post = store.createPost(bobPost);
        REQUIRE(store.listNotifications("test:alice").size() == 1);
        REQUIRE(store.listNotifications("test:alice")[0]["type"].asString() == "new_post");

        post = store.addComment("post:bob", "test:alice", "First!", "comment:alice");
        REQUIRE(post["comments"][0]["likes"].empty());
        REQUIRE(post["comments"][0]["replies"].empty());
        REQUIRE(store.listNotifications("test:bob").size() == 2);

        post = store.setCommentLike("post:bob", "comment:alice", "test:bob", true);
        REQUIRE(post["comments"][0]["likes"].size() == 1);
        post = store.setCommentLike("post:bob", "comment:alice", "test:bob", true);
        REQUIRE(post["comments"][0]["likes"].size() == 1);

        post = store.addReply("post:bob", "comment:alice", "test:bob",
                              "Thanks!", "reply:bob");
        REQUIRE(post["comments"][0]["replies"].size() == 1);
        REQUIRE(post["comments"][0]["replies"][0]["comment"].asString() == "Thanks!");

        REQUIRE_THROWS_AS(store.setCommentPinned(
                              "post:bob", "comment:alice", "test:alice", true),
                          tiktok::StoreError);
        post = store.setCommentPinned("post:bob", "comment:alice", "test:bob", true);
        REQUIRE(post["comments"][0]["pinned"].asBool());
        post = store.setCommentPinned("post:bob", "comment:alice", "test:bob", false);
        REQUIRE_FALSE(post["comments"][0]["pinned"].asBool());

        auto aliceNotifications = store.listNotifications("test:alice");
        REQUIRE(aliceNotifications.size() == 3);
        REQUIRE(store.markNotificationsRead("test:alice") == 3);
        REQUIRE(store.markNotificationsRead("test:alice") == 0);

        post = store.setCommentLike("post:bob", "comment:alice", "test:bob", false);
        REQUIRE(post["comments"][0]["likes"].empty());

        const auto report = store.createReport(
            {"report:one", "test:alice", "test:bob", "spam", "Repeated posts"});
        const auto repeatedReport = store.createReport(
            {"report:two", "test:alice", "test:bob", "harassment", "Updated details"});
        REQUIRE(report["reportId"].asString() == repeatedReport["reportId"].asString());
        REQUIRE_THROWS_AS(store.createReport(
                              {"report:self", "test:alice", "test:alice", "other", ""}),
                          tiktok::StoreError);

        relationship = store.setFollow("test:alice", "test:bob", false);
        REQUIRE_FALSE(relationship["following"].asBool());
        REQUIRE(relationship["followerCount"].asUInt64() == 0);

        const auto media = store.deletePost("post:bob", "test:bob");
        REQUIRE(media.id == "media:bob");
    }

    SECTION("upgraded profiles, feeds, safety, library, chat, moderation, and statistics persist") {
        const auto profile = store.updateProfile("test:alice", {"Alice Updated", "C++ creator"});
        REQUIRE(profile["userName"].asString() == "Alice Updated");
        REQUIRE(profile["bio"].asString() == "C++ creator");

        const tiktok::PostInput postInput{
            "post:upgrades", "test:bob", "MongoDB #cpp #School", "Gaming",
            "media:upgrades", "upgrades.mp4", "upgrades.mp4", "video/mp4", 20};
        auto post = store.createPost(postInput);
        REQUIRE(post["hashtags"].size() == 2);

        store.setFollow("test:alice", "test:bob", true);
        const auto followingFeed = store.listFeed(
            "test:alice", "following", std::nullopt, std::nullopt, "", 10);
        REQUIRE(followingFeed["items"].size() == 1);
        REQUIRE(followingFeed["items"][0]["_id"].asString() == "post:upgrades");
        const auto hashtagFeed = store.listFeed(
            "test:alice", "for_you", std::nullopt, std::string{"cpp"}, "", 1);
        REQUIRE(hashtagFeed["items"].size() == 1);

        REQUIRE(store.setSaved("post:upgrades", "test:alice", true)["saved"].asBool());
        REQUIRE(store.setSaved("post:upgrades", "test:alice", true)["saved"].asBool());
        REQUIRE(store.listSaved("test:alice").size() == 1);
        REQUIRE(store.recordView("post:upgrades", "test:alice", 4)["viewCount"].asUInt64() == 1);
        REQUIRE(store.recordView("post:upgrades", "test:alice", 9)["viewCount"].asUInt64() == 2);
        REQUIRE(store.recordView("post:upgrades", "", 0)["viewCount"].asUInt64() == 3);
        REQUIRE(store.listHistory("test:alice").size() == 1);
        REQUIRE(store.recordShare("post:upgrades", "test:alice")["shareCount"].asUInt64() == 1);
        const auto shareNotifications = store.listNotifications("test:bob");
        REQUIRE(std::any_of(shareNotifications.begin(), shareNotifications.end(),
                            [](const Json::Value& notification) {
                                return notification["type"].asString() == "post_share" &&
                                       notification["postId"].asString() == "post:upgrades";
                            }));

        post = store.addComment("post:upgrades", "test:alice", "Original", "comment:upgrade");
        post = store.editComment("post:upgrades", "comment:upgrade", "test:alice", "Edited");
        REQUIRE(post["comments"][0]["comment"].asString() == "Edited");
        post = store.addReply("post:upgrades", "comment:upgrade", "test:bob",
                              "Original reply", "reply:upgrade");
        post = store.editReply("post:upgrades", "comment:upgrade", "reply:upgrade",
                               "test:bob", "Edited reply");
        REQUIRE(post["comments"][0]["replies"][0]["comment"].asString() == "Edited reply");
        post = store.deleteReply("post:upgrades", "comment:upgrade", "reply:upgrade", "test:bob");
        REQUIRE(post["comments"][0]["replies"].empty());
        post = store.deleteComment("post:upgrades", "comment:upgrade", "test:alice");
        REQUIRE(post["comments"].empty());

        auto message = store.createMessage(
            {"message:upgrade", "test:alice", "test:bob", "React to me", "", "", "", "", ""});
        message = store.setMessageReaction("message:upgrade", "test:bob", "heart");
        REQUIRE(message["reactions"].size() == 1);
        REQUIRE(store.setConversationSettings("test:bob", "test:alice", true, true)["muted"].asBool());
        const auto bobConversations = store.listConversations("test:bob");
        REQUIRE(bobConversations[0]["muted"].asBool());
        REQUIRE(bobConversations[0]["archived"].asBool());
        message = store.deleteMessage("message:upgrade", "test:alice");
        REQUIRE(message["deleted"].asBool());
        REQUIRE_THROWS_AS(store.deleteMessage("message:upgrade", "test:bob"), tiktok::StoreError);

        Json::Value preferences(Json::objectValue);
        preferences["message"] = false;
        REQUIRE_FALSE(store.updateNotificationPreferences("test:bob", preferences)["message"].asBool());
        REQUIRE_FALSE(store.getNotificationPreferences("test:bob")["message"].asBool());

        REQUIRE(store.setMuted("test:alice", "test:bob", true)["muted"].asBool());
        REQUIRE(store.listFeed("test:alice", "for_you", std::nullopt, std::nullopt, "", 10)
                    ["items"].empty());
        REQUIRE(store.setMuted("test:alice", "test:bob", false)["muted"].asBool() == false);
        REQUIRE(store.setBlocked("test:alice", "test:bob", true)["blocked"].asBool());
        REQUIRE_THROWS_AS(
            store.createMessage({"message:blocked", "test:bob", "test:alice", "No", "", "", "", "", ""}),
            tiktok::StoreError);
        REQUIRE_FALSE(store.getRelationship("test:alice", "test:bob")["following"].asBool());
        REQUIRE_FALSE(store.setBlocked("test:alice", "test:bob", false)["blocked"].asBool());

        const auto report = store.createReport(
            {"report:upgrade", "test:alice", "test:bob", "spam", "Moderate this"});
        REQUIRE(store.listReports("open").size() == 1);
        REQUIRE(store.updateReportStatus(report["reportId"].asString(), "test:alice", "resolved")
                    ["status"].asString() == "resolved");
        REQUIRE(store.listReports("resolved").size() == 1);
        const auto stats = store.adminStatistics();
        REQUIRE(stats["users"].asUInt64() == 2);
        REQUIRE(stats["videos"].asUInt64() == 1);
        REQUIRE(stats["views"].asUInt64() == 3);
        REQUIRE(store.setUserSuspended("test:bob", "test:alice", true)["suspended"].asBool());
        REQUIRE(store.isSuspended("test:bob"));

        store.setSaved("post:upgrades", "test:alice", false);
        REQUIRE(store.listSaved("test:alice").empty());
        store.deletePost("post:upgrades", "test:bob");
    }

    resetDatabase();
}
