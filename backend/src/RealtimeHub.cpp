#include "tiktok/RealtimeHub.h"

#include "tiktok/Security.h"

#include <json/reader.h>

#include <sstream>
#include <vector>

namespace tiktok {

std::mutex RealtimeHub::mutex_;
std::unordered_map<std::string,
                   std::unordered_set<drogon::WebSocketConnectionPtr>> RealtimeHub::connections_;

void RealtimeHub::addConnection(const std::string& userId,
                                const drogon::WebSocketConnectionPtr& connection) {
    std::lock_guard lock(mutex_);
    connections_[userId].insert(connection);
}

void RealtimeHub::removeConnection(const std::string& userId,
                                   const drogon::WebSocketConnectionPtr& connection) {
    std::lock_guard lock(mutex_);
    const auto found = connections_.find(userId);
    if (found == connections_.end()) {
        return;
    }
    found->second.erase(connection);
    if (found->second.empty()) {
        connections_.erase(found);
    }
}

bool RealtimeHub::online(const std::string& userId) {
    std::lock_guard lock(mutex_);
    const auto found = connections_.find(userId);
    return found != connections_.end() && !found->second.empty();
}

void RealtimeHub::publish(const std::string& userId, const Json::Value& event) {
    std::vector<drogon::WebSocketConnectionPtr> recipients;
    {
        std::lock_guard lock(mutex_);
        const auto found = connections_.find(userId);
        if (found != connections_.end()) {
            recipients.assign(found->second.begin(), found->second.end());
        }
    }
    for (const auto& connection : recipients) {
        if (connection && connection->connected()) {
            connection->sendJson(event);
        }
    }
}

void RealtimeHub::handleNewConnection(
    const drogon::HttpRequestPtr& request,
    const drogon::WebSocketConnectionPtr& connection) {
    const auto rawToken = request->getCookie("tt_session");
    if (rawToken.empty()) {
        connection->shutdown(drogon::CloseCode::kViolation, "Authentication required");
        return;
    }
    const auto session = store_.getSession(sha256Hex(rawToken));
    if (!session || store_.isSuspended(session->userId)) {
        connection->shutdown(drogon::CloseCode::kViolation, "Session is invalid");
        return;
    }
    connection->setContext(std::make_shared<std::string>(session->userId));
    addConnection(session->userId, connection);
    Json::Value ready(Json::objectValue);
    ready["type"] = "ready";
    ready["userId"] = session->userId;
    connection->sendJson(ready);
}

void RealtimeHub::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr& connection) {
    if (connection->hasContext()) {
        removeConnection(connection->getContextRef<std::string>(), connection);
    }
}

void RealtimeHub::handleNewMessage(
    const drogon::WebSocketConnectionPtr& connection,
    std::string&& message,
    const drogon::WebSocketMessageType& type) {
    if (type != drogon::WebSocketMessageType::Text || !connection->hasContext() ||
        message.size() > 4'096) {
        return;
    }
    Json::Value body;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream input(message);
    if (!Json::parseFromStream(builder, input, &body, &errors) || !body.isObject()) {
        return;
    }
    const auto actorId = connection->getContextRef<std::string>();
    const auto eventType = body["type"].asString();
    const auto targetId = body["userId"].asString();
    if (targetId.empty() || targetId.size() > 200 || store_.isBlocked(actorId, targetId)) {
        return;
    }
    if (eventType == "presence_request") {
        Json::Value event(Json::objectValue);
        event["type"] = "presence";
        event["userId"] = targetId;
        event["online"] = online(targetId);
        connection->sendJson(event);
        return;
    }
    if (eventType == "typing") {
        Json::Value event(Json::objectValue);
        event["type"] = "typing";
        event["userId"] = actorId;
        event["typing"] = body["typing"].asBool();
        publish(targetId, event);
    }
}

}  // namespace tiktok
