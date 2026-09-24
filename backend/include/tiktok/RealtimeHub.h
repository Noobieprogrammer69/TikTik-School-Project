#pragma once

#include "tiktok/MongoStore.h"

#include <drogon/WebSocketController.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tiktok {

class RealtimeHub : public drogon::WebSocketController<RealtimeHub, false> {
  public:
    explicit RealtimeHub(MongoStore& store) : store_(store) {}

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/realtime");
    WS_PATH_LIST_END

    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;

    static void publish(const std::string& userId, const Json::Value& event);
    static bool online(const std::string& userId);

  private:
    static void addConnection(const std::string& userId,
                              const drogon::WebSocketConnectionPtr& connection);
    static void removeConnection(const std::string& userId,
                                 const drogon::WebSocketConnectionPtr& connection);

    MongoStore& store_;
    static std::mutex mutex_;
    static std::unordered_map<std::string,
                              std::unordered_set<drogon::WebSocketConnectionPtr>> connections_;
};

}  // namespace tiktok
