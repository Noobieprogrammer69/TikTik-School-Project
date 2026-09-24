#include "tiktok/ApiServer.h"
#include "tiktok/Config.h"
#include "tiktok/MongoStore.h"
#include "tiktok/RealtimeHub.h"

#include <drogon/drogon.h>
#include <mongocxx/instance.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

int main() {
    try {
        static mongocxx::instance mongoInstance{};
        auto config = tiktok::AppConfig::fromEnvironment();
        config.mediaRoot = std::filesystem::absolute(config.mediaRoot).lexically_normal();
        std::filesystem::create_directories(config.mediaRoot);

        tiktok::MongoStore store(config.mongoUri, config.mongoDatabase);
        store.ping();
        store.ensureIndexes();

        tiktok::ApiServer server(config, store);
        server.registerRoutes();
        drogon::app().registerController(std::make_shared<tiktok::RealtimeHub>(store));

        drogon::app()
            .addListener(config.listenAddress, config.port)
            .setThreadNum(4)
            .setClientMaxBodySize(config.maxUploadBytes + 1024ULL * 1024ULL)
            // Larger multipart bodies are backed by a temporary file instead
            // of reserving the whole (potentially 2 GiB) request in RAM.
            .setClientMaxMemoryBodySize(16ULL * 1024ULL * 1024ULL)
            .setUploadPath(config.mediaRoot.string())
            .setLogLevel(trantor::Logger::kInfo)
            .run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Backend startup failed: " << error.what() << '\n';
        return 1;
    }
}
