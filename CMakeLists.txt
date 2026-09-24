cmake_minimum_required(VERSION 3.25)

project(TikTokCloneBackend VERSION 2.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(mongocxx CONFIG REQUIRED)
find_package(bsoncxx CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(tiktok_backend_core
    backend/src/ApiServer.cpp
    backend/src/Config.cpp
    backend/src/GoogleVerifier.cpp
    backend/src/MongoStore.cpp
    backend/src/MongoStoreUpgrades.cpp
    backend/src/MongoStoreNext.cpp
    backend/src/RealtimeHub.cpp
    backend/src/Security.cpp
    backend/src/UpgradeRoutes.cpp
    backend/src/NextRoutes.cpp
    backend/src/Validation.cpp
)

target_include_directories(tiktok_backend_core PUBLIC backend/include)
target_link_libraries(tiktok_backend_core
    PUBLIC
        Drogon::Drogon
        $<IF:$<TARGET_EXISTS:mongo::mongocxx_static>,mongo::mongocxx_static,mongo::mongocxx_shared>
        $<IF:$<TARGET_EXISTS:mongo::bsoncxx_static>,mongo::bsoncxx_static,mongo::bsoncxx_shared>
        jwt-cpp::jwt-cpp
        OpenSSL::Crypto
)

if(MSVC)
    target_compile_options(tiktok_backend_core PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(tiktok_backend_core PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX)
else()
    target_compile_options(tiktok_backend_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(tiktok_backend backend/src/main.cpp)
target_link_libraries(tiktok_backend PRIVATE tiktok_backend_core)

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    add_executable(tiktok_backend_tests
        backend/tests/SecurityTests.cpp
        backend/tests/ValidationTests.cpp
    )
    target_link_libraries(tiktok_backend_tests PRIVATE tiktok_backend_core Catch2::Catch2WithMain)
    include(Catch)
    catch_discover_tests(tiktok_backend_tests DISCOVERY_MODE PRE_TEST)

    add_executable(tiktok_mongo_integration_tests backend/tests/MongoStoreIntegrationTests.cpp)
    target_link_libraries(tiktok_mongo_integration_tests
        PRIVATE tiktok_backend_core Catch2::Catch2WithMain
    )
    add_test(NAME tiktok_mongo_integration COMMAND tiktok_mongo_integration_tests)
    set_tests_properties(tiktok_mongo_integration PROPERTIES LABELS "integration;mongodb")
endif()
