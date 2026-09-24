#include "tiktok/Validation.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace tiktok {
namespace {

std::string trim(std::string_view value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool parseSize(std::string_view value, std::size_t& result) {
    if (value.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size() ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    result = static_cast<std::size_t>(parsed);
    return true;
}

ValidationResult validateText(std::string_view input, std::size_t maximum, std::string_view label) {
    auto value = trim(input);
    if (value.empty()) {
        return {false, {}, std::string(label) + " is required"};
    }
    if (value.size() > maximum) {
        return {false, {}, std::string(label) + " must be at most " + std::to_string(maximum) + " bytes"};
    }
    if (value.find('\0') != std::string::npos) {
        return {false, {}, std::string(label) + " contains an invalid character"};
    }
    return {true, std::move(value), {}};
}

}  // namespace

ValidationResult validateCaption(std::string_view caption) {
    return validateText(caption, 150, "Caption");
}

ValidationResult validateComment(std::string_view comment) {
    return validateText(comment, 500, "Comment");
}

ValidationResult validateMessage(std::string_view message) {
    return validateText(message, 1'000, "Message");
}

ValidationResult validateReportReason(std::string_view reason) {
    static const std::unordered_map<std::string, std::string> reasons{
        {"spam", "spam"},
        {"harassment", "harassment"},
        {"impersonation", "impersonation"},
        {"inappropriate", "inappropriate"},
        {"other", "other"},
    };
    const auto normalized = lower(trim(reason));
    const auto found = reasons.find(normalized);
    if (found == reasons.end()) {
        return {false, {}, "Report reason is not supported"};
    }
    return {true, found->second, {}};
}

ValidationResult validateReportDetails(std::string_view details) {
    auto value = trim(details);
    if (value.size() > 500) {
        return {false, {}, "Report details must be at most 500 bytes"};
    }
    if (value.find('\0') != std::string::npos) {
        return {false, {}, "Report details contain an invalid character"};
    }
    return {true, std::move(value), {}};
}

ValidationResult validateTopic(std::string_view topic) {
    static const std::unordered_map<std::string, std::string> topics{
        {"anime", "Anime"},       {"hentai", "Hentai"}, {"gaming", "Gaming"},
        {"good", "Good"},         {"dance", "Dance"},   {"edits", "Edits"},
        {"animals", "Animals"},   {"sports", "Sports"},
    };
    const auto normalized = lower(trim(topic));
    const auto found = topics.find(normalized);
    if (found == topics.end()) {
        return {false, {}, "Topic is not one of the supported categories"};
    }
    return {true, found->second, {}};
}

std::string escapeRegex(std::string_view value) {
    static constexpr std::string_view special = R"(\.^$|()[]{}*+?)";
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (const char character : value) {
        if (special.find(character) != std::string_view::npos) {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

MediaValidationResult validateVideo(std::string_view originalName,
                                    std::string_view contentType,
                                    std::string_view bytes,
                                    std::size_t maximumBytes) {
    if (bytes.empty()) {
        return {false, {}, {}, "Video file is empty"};
    }
    if (bytes.size() > maximumBytes) {
        return {false, {}, {}, "Video file exceeds the configured upload limit"};
    }

    const auto dot = originalName.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= originalName.size()) {
        return {false, {}, {}, "Video filename must have an extension"};
    }
    const auto extension = lower(originalName.substr(dot + 1));
    const auto mime = lower(contentType.substr(0, contentType.find(';')));

    bool signatureMatches = false;
    if (extension == "mp4" && mime == "video/mp4") {
        signatureMatches = bytes.size() >= 12 && bytes.substr(4, 4) == "ftyp";
    } else if (extension == "webm" && mime == "video/webm") {
        signatureMatches = bytes.size() >= 4 &&
                           static_cast<unsigned char>(bytes[0]) == 0x1a &&
                           static_cast<unsigned char>(bytes[1]) == 0x45 &&
                           static_cast<unsigned char>(bytes[2]) == 0xdf &&
                           static_cast<unsigned char>(bytes[3]) == 0xa3;
    } else if ((extension == "ogg" || extension == "ogv") &&
               (mime == "video/ogg" || mime == "application/ogg")) {
        signatureMatches = bytes.size() >= 4 && bytes.substr(0, 4) == "OggS";
    } else {
        return {false, {}, {}, "Only MP4, WebM, and Ogg video files are accepted"};
    }

    if (!signatureMatches) {
        return {false, {}, {}, "Video contents do not match the selected file type"};
    }
    const auto canonicalExtension = extension == "ogv" ? "ogg" : extension;
    const auto canonicalMime = canonicalExtension == "ogg" ? "video/ogg" : mime;
    return {true, canonicalExtension, canonicalMime, {}};
}

ByteRange parseByteRange(std::string_view header, std::size_t fileSize) {
    if (header.empty()) {
        return {RangeStatus::kNone, 0, fileSize == 0 ? 0 : fileSize - 1};
    }
    if (!header.starts_with("bytes=") || header.find(',') != std::string_view::npos) {
        return {RangeStatus::kMalformed, 0, 0};
    }
    if (fileSize == 0) {
        return {RangeStatus::kUnsatisfiable, 0, 0};
    }

    const auto specification = header.substr(6);
    const auto dash = specification.find('-');
    if (dash == std::string_view::npos || specification.find('-', dash + 1) != std::string_view::npos) {
        return {RangeStatus::kMalformed, 0, 0};
    }
    const auto first = specification.substr(0, dash);
    const auto second = specification.substr(dash + 1);

    if (first.empty()) {
        std::size_t suffix = 0;
        if (!parseSize(second, suffix) || suffix == 0) {
            return {RangeStatus::kMalformed, 0, 0};
        }
        suffix = std::min(suffix, fileSize);
        return {RangeStatus::kValid, fileSize - suffix, fileSize - 1};
    }

    std::size_t start = 0;
    if (!parseSize(first, start)) {
        return {RangeStatus::kMalformed, 0, 0};
    }
    if (start >= fileSize) {
        return {RangeStatus::kUnsatisfiable, 0, 0};
    }

    if (second.empty()) {
        return {RangeStatus::kValid, start, fileSize - 1};
    }

    std::size_t end = 0;
    if (!parseSize(second, end)) {
        return {RangeStatus::kMalformed, 0, 0};
    }
    if (end < start) {
        return {RangeStatus::kUnsatisfiable, 0, 0};
    }
    return {RangeStatus::kValid, start, std::min(end, fileSize - 1)};
}

}  // namespace tiktok
