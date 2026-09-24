#include "tiktok/Validation.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("captions, comments, and topics are trimmed and bounded") {
    const auto caption = tiktok::validateCaption("  school demo  ");
    REQUIRE(caption.ok);
    REQUIRE(caption.value == "school demo");
    REQUIRE_FALSE(tiktok::validateCaption("   ").ok);
    REQUIRE_FALSE(tiktok::validateCaption(std::string(151, 'a')).ok);
    REQUIRE_FALSE(tiktok::validateComment(std::string(501, 'a')).ok);

    const auto message = tiktok::validateMessage("  Hello from chat!  ");
    REQUIRE(message.ok);
    REQUIRE(message.value == "Hello from chat!");
    REQUIRE_FALSE(tiktok::validateMessage("   ").ok);
    REQUIRE_FALSE(tiktok::validateMessage(std::string(1'001, 'a')).ok);

    REQUIRE(tiktok::validateReportReason(" Harassment ").value == "harassment");
    REQUIRE_FALSE(tiktok::validateReportReason("anything").ok);
    REQUIRE(tiktok::validateReportDetails("").ok);
    REQUIRE_FALSE(tiktok::validateReportDetails(std::string(501, 'a')).ok);

    const auto topic = tiktok::validateTopic("  gAmInG ");
    REQUIRE(topic.ok);
    REQUIRE(topic.value == "Gaming");
    REQUIRE_FALSE(tiktok::validateTopic("not-a-topic").ok);
}

TEST_CASE("regular expression input is escaped") {
    REQUIRE(tiktok::escapeRegex("a.*[b]") == R"(a\.\*\[b\])");
}

TEST_CASE("video validation checks size, extension, MIME, and signature") {
    std::string mp4(16, '\0');
    mp4.replace(4, 4, "ftyp");
    const auto validMp4 = tiktok::validateVideo("demo.mp4", "video/mp4", mp4, 100);
    REQUIRE(validMp4.ok);
    REQUIRE(validMp4.extension == "mp4");

    REQUIRE_FALSE(tiktok::validateVideo("demo.exe", "application/octet-stream", mp4, 100).ok);
    REQUIRE_FALSE(tiktok::validateVideo("demo.mp4", "video/mp4", "not a video", 100).ok);
    REQUIRE_FALSE(tiktok::validateVideo("demo.mp4", "video/mp4", mp4, 4).ok);

    const std::string webm{"\x1a\x45\xdf\xa3payload", 11};
    REQUIRE(tiktok::validateVideo("demo.webm", "video/webm", webm, 100).ok);
    REQUIRE(tiktok::validateVideo("demo.ogv", "application/ogg", "OggSpayload", 100).ok);
}

TEST_CASE("HTTP byte ranges support normal, open-ended, and suffix requests") {
    auto range = tiktok::parseByteRange("", 100);
    REQUIRE(range.status == tiktok::RangeStatus::kNone);

    range = tiktok::parseByteRange("bytes=10-19", 100);
    REQUIRE(range.status == tiktok::RangeStatus::kValid);
    REQUIRE(range.start == 10);
    REQUIRE(range.end == 19);

    range = tiktok::parseByteRange("bytes=90-", 100);
    REQUIRE(range.status == tiktok::RangeStatus::kValid);
    REQUIRE(range.start == 90);
    REQUIRE(range.end == 99);

    range = tiktok::parseByteRange("bytes=-10", 100);
    REQUIRE(range.status == tiktok::RangeStatus::kValid);
    REQUIRE(range.start == 90);
    REQUIRE(range.end == 99);

    REQUIRE(tiktok::parseByteRange("items=1-2", 100).status == tiktok::RangeStatus::kMalformed);
    REQUIRE(tiktok::parseByteRange("bytes=100-110", 100).status ==
            tiktok::RangeStatus::kUnsatisfiable);
    REQUIRE(tiktok::parseByteRange("bytes=30-20", 100).status ==
            tiktok::RangeStatus::kUnsatisfiable);
    REQUIRE(tiktok::parseByteRange("bytes=0-1,5-6", 100).status ==
            tiktok::RangeStatus::kMalformed);
}
