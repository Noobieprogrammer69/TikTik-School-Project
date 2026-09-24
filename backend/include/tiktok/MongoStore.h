#pragma once

#include <json/json.h>
#include <mongocxx/pool.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tiktok {

class StoreError : public std::runtime_error {
  public:
    enum class Kind { kNotFound, kForbidden, kConflict, kDatabase };

    StoreError(Kind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
    Kind kind() const noexcept { return kind_; }

  private:
    Kind kind_;
};

struct UserIdentity {
    std::string id;
    std::string userName;
    std::string image;
    std::string provider;
    std::string providerSubject;
    bool emailVerified{false};
};

struct SessionRecord {
    std::string tokenHash;
    std::string userId;
    std::string csrfHash;
    std::chrono::system_clock::time_point expiresAt;
};

struct PostInput {
    std::string id;
    std::string userId;
    std::string caption;
    std::string topic;
    std::string mediaId;
    std::string storageName;
    std::string originalName;
    std::string contentType;
    std::size_t size{0};
    std::string thumbnailId;
    std::string thumbnailStorageName;
    std::string subtitleId;
    std::string subtitleStorageName;
    std::vector<std::string> qualities;
};

struct MediaRecord {
    std::string id;
    std::string storageName;
    std::string originalName;
    std::string contentType;
    std::size_t size{0};
    std::string thumbnailStorageName;
    std::string subtitleStorageName;
    std::vector<std::string> qualities;
};

struct MessageInput {
    std::string id;
    std::string senderId;
    std::string recipientId;
    std::string text;
    std::string replyTo;
    std::string attachmentId;
    std::string attachmentStorageName;
    std::string attachmentContentType;
    std::string attachmentOriginalName;
};

struct ReportInput {
    std::string id;
    std::string reporterId;
    std::string targetUserId;
    std::string reason;
    std::string details;
};

struct ProfileUpdate {
    std::string userName;
    std::string bio;
};

struct StoredAsset {
    std::string id;
    std::string storageName;
    std::string contentType;
    std::size_t size{0};
};

class MongoStore {
  public:
    MongoStore(const std::string& uri, const std::string& database);

    void ensureIndexes();
    void ping();

    Json::Value upsertUser(const UserIdentity& identity);
    std::optional<Json::Value> getUser(const std::string& id);
    Json::Value listUsers(std::size_t limit = 100);
    Json::Value updateProfile(const std::string& userId, const ProfileUpdate& update);
    Json::Value updateAvatar(const std::string& userId,
                             const std::string& storageName,
                             const std::string& contentType);
    std::optional<StoredAsset> getAvatar(const std::string& userId);
    bool isSuspended(const std::string& userId);

    void createSession(const SessionRecord& session);
    std::optional<SessionRecord> getSession(const std::string& tokenHash);
    void rotateCsrf(const std::string& tokenHash, const std::string& csrfHash);
    void revokeSession(const std::string& tokenHash);

    Json::Value listPosts(const std::optional<std::string>& topic = std::nullopt,
                          const std::optional<std::string>& search = std::nullopt,
                          std::size_t limit = 100);
    Json::Value listFeed(const std::string& viewerId,
                         const std::string& mode,
                         const std::optional<std::string>& topic,
                         const std::optional<std::string>& hashtag,
                         const std::string& cursor,
                         std::size_t limit);
    std::optional<Json::Value> getPost(const std::string& id);
    bool canViewPost(const std::string& postId, const std::string& viewerId);
    bool canViewMedia(const std::string& mediaId, const std::string& viewerId);
    Json::Value getProfile(const std::string& userId, const std::string& viewerId = {});
    Json::Value search(const std::string& term);
    Json::Value getRelationship(const std::string& actingUserId,
                                const std::string& targetUserId);
    Json::Value setFollow(const std::string& actingUserId,
                          const std::string& targetUserId,
                          bool follow);
    Json::Value getSafetyRelationship(const std::string& actingUserId,
                                      const std::string& targetUserId);
    Json::Value setBlocked(const std::string& actingUserId,
                           const std::string& targetUserId,
                           bool blocked);
    Json::Value setMuted(const std::string& actingUserId,
                         const std::string& targetUserId,
                         bool muted);
    bool isBlocked(const std::string& firstUserId, const std::string& secondUserId);
    Json::Value createReport(const ReportInput& input);
    Json::Value listConversations(const std::string& userId, std::size_t limit = 100);
    Json::Value listMessages(const std::string& userId,
                             const std::string& otherUserId,
                             std::size_t limit = 100);
    Json::Value createMessage(const MessageInput& input);
    std::size_t markConversationRead(const std::string& userId,
                                     const std::string& otherUserId);
    Json::Value setMessageReaction(const std::string& messageId,
                                   const std::string& userId,
                                   const std::string& emoji);
    Json::Value deleteMessage(const std::string& messageId, const std::string& userId);
    Json::Value setConversationSettings(const std::string& userId,
                                        const std::string& otherUserId,
                                        bool muted,
                                        bool archived);
    Json::Value listMessageHistory(const std::string& userId,
                                   const std::string& otherUserId,
                                   const std::string& before,
                                   std::size_t limit = 50);
    Json::Value searchMessages(const std::string& userId,
                               const std::string& otherUserId,
                               const std::string& term,
                               std::size_t limit = 50);
    Json::Value editMessage(const std::string& messageId,
                            const std::string& userId,
                            const std::string& text);
    std::optional<StoredAsset> getChatAsset(const std::string& mediaId,
                                            const std::string& userId);
    Json::Value listNotifications(const std::string& userId, std::size_t limit = 100);
    std::size_t markNotificationsRead(const std::string& userId);
    std::size_t markNotificationRead(const std::string& userId,
                                     const std::string& notificationId);
    std::size_t deleteNotification(const std::string& userId,
                                   const std::string& notificationId);
    Json::Value getNotificationPreferences(const std::string& userId);
    Json::Value updateNotificationPreferences(const std::string& userId,
                                               const Json::Value& preferences);
    Json::Value getPrivacySettings(const std::string& userId);
    Json::Value updatePrivacySettings(const std::string& userId,
                                      const Json::Value& settings);
    bool interactionAllowed(const std::string& actingUserId,
                            const std::string& targetUserId,
                            const std::string& kind);
    Json::Value setFeedFeedback(const std::string& userId,
                                const std::string& postId,
                                const std::string& type,
                                bool enabled);
    Json::Value creatorAnalytics(const std::string& userId);
    Json::Value listSessions(const std::string& userId,
                             const std::string& currentTokenHash);
    std::size_t revokeOtherSessions(const std::string& userId,
                                    const std::string& currentTokenHash);
    Json::Value exportAccount(const std::string& userId);
    Json::Value deleteAccount(const std::string& userId);
    Json::Value createContentReport(const std::string& reportId,
                                    const std::string& reporterId,
                                    const std::string& targetType,
                                    const std::string& targetId,
                                    const std::string& reason,
                                    const std::string& details);
    Json::Value listFollowRequests(const std::string& userId);
    Json::Value respondToFollowRequest(const std::string& userId,
                                       const std::string& requesterId,
                                       bool accept);
    Json::Value listSecurityEvents(const std::string& userId,
                                   std::size_t limit = 30);
    void recordSecurityEvent(const std::string& userId,
                             const std::string& type,
                             const std::string& detail = {});
    void notifyMentions(const std::string& text,
                        const std::string& actorId,
                        const std::string& postId = {},
                        const std::string& commentId = {});
    Json::Value savePushSubscription(const std::string& userId,
                                     const std::string& endpoint,
                                     const std::string& key,
                                     const std::string& auth);
    std::size_t deletePushSubscription(const std::string& userId,
                                       const std::string& endpoint);
    Json::Value createPost(const PostInput& input);
    Json::Value setLike(const std::string& postId, const std::string& userId, bool like);
    Json::Value addComment(const std::string& postId,
                           const std::string& userId,
                           const std::string& comment,
                           const std::string& commentId);
    Json::Value setCommentLike(const std::string& postId,
                               const std::string& commentId,
                               const std::string& userId,
                               bool like);
    Json::Value addReply(const std::string& postId,
                         const std::string& commentId,
                         const std::string& userId,
                         const std::string& reply,
                         const std::string& replyId);
    Json::Value editComment(const std::string& postId,
                            const std::string& commentId,
                            const std::string& actingUserId,
                            const std::string& text);
    Json::Value deleteComment(const std::string& postId,
                              const std::string& commentId,
                              const std::string& actingUserId);
    Json::Value editReply(const std::string& postId,
                          const std::string& commentId,
                          const std::string& replyId,
                          const std::string& actingUserId,
                          const std::string& text);
    Json::Value deleteReply(const std::string& postId,
                            const std::string& commentId,
                            const std::string& replyId,
                            const std::string& actingUserId);
    Json::Value setCommentPinned(const std::string& postId,
                                 const std::string& commentId,
                                 const std::string& actingUserId,
                                 bool pinned);
    Json::Value setSaved(const std::string& postId, const std::string& userId, bool saved);
    Json::Value listSaved(const std::string& userId, std::size_t limit = 100);
    Json::Value recordView(const std::string& postId,
                           const std::string& userId,
                           std::uint32_t watchSeconds);
    Json::Value listHistory(const std::string& userId, std::size_t limit = 100);
    Json::Value recordShare(const std::string& postId, const std::string& userId);
    Json::Value listReports(const std::string& status, std::size_t limit = 100);
    Json::Value updateReportStatus(const std::string& reportId,
                                   const std::string& moderatorId,
                                   const std::string& status);
    Json::Value setUserSuspended(const std::string& userId,
                                 const std::string& moderatorId,
                                 bool suspended);
    Json::Value adminStatistics();
    MediaRecord deletePost(const std::string& postId, const std::string& actingUserId);
    std::optional<MediaRecord> getMedia(const std::string& mediaId);

    const std::string& databaseName() const noexcept { return database_; }

  private:
    Json::Value userToJson(const bsoncxx::document::view& user) const;
    Json::Value postToJson(mongocxx::database& database, const bsoncxx::document::view& post) const;
    Json::Value messageToJson(const bsoncxx::document::view& message) const;
    Json::Value notificationToJson(mongocxx::database& database,
                                   const bsoncxx::document::view& notification) const;
    void upsertNotification(mongocxx::database& database,
                            const std::string& id,
                            const std::string& userId,
                            const std::string& actorId,
                            const std::string& type,
                            const std::string& postId = {},
                            const std::string& commentId = {});
    void removeNotification(mongocxx::database& database, const std::string& id);

    mongocxx::pool pool_;
    std::string database_;
};

}  // namespace tiktok
