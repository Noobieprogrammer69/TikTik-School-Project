#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace tiktok {

struct ValidationResult {
    bool ok{false};
    std::string value;
    std::string error;
};

ValidationResult validateCaption(std::string_view caption);
ValidationResult validateComment(std::string_view comment);
ValidationResult validateMessage(std::string_view message);
ValidationResult validateReportReason(std::string_view reason);
ValidationResult validateReportDetails(std::string_view details);
ValidationResult validateTopic(std::string_view topic);
std::string escapeRegex(std::string_view value);

struct MediaValidationResult {
    bool ok{false};
    std::string extension;
    std::string contentType;
    std::string error;
};

MediaValidationResult validateVideo(std::string_view originalName,
                                    std::string_view contentType,
                                    std::string_view bytes,
                                    std::size_t maximumBytes);

enum class RangeStatus { kNone, kValid, kMalformed, kUnsatisfiable };

struct ByteRange {
    RangeStatus status{RangeStatus::kNone};
    std::size_t start{0};
    std::size_t end{0};
};

ByteRange parseByteRange(std::string_view header, std::size_t fileSize);

}  // namespace tiktok
