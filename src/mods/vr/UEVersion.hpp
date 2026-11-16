#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace uevr {

enum class UEVersion {
    Unknown,
    UE4_10,
    UE4_11,
    UE4_12,
    UE4_13,
    UE4_14,
    UE4_15,
    UE4_16,
    UE4_17,
    UE4_18,
    UE4_19,
    UE4_20,
    UE4_21,
    UE4_22,
    UE4_23,
    UE4_24,
    UE4_25,
    UE4_26,
    UE4_27,
    UE5_0,
    UE5_1,
    UE5_2,
    UE5_3,
    UE5_4,
    UE5_5,
    UE5_6,
    UE5_7,
};

struct UEVersionRule {
    uint32_t file_version_ms;
    std::string_view prefix;
    UEVersion version;
};

inline constexpr std::array<UEVersionRule, 26> k_ue_version_rules{{
    {0x50007, "5.7", UEVersion::UE5_7},
    {0x50006, "5.6", UEVersion::UE5_6},
    {0x50005, "5.5", UEVersion::UE5_5},
    {0x50004, "5.4", UEVersion::UE5_4},
    {0x50003, "5.3", UEVersion::UE5_3},
    {0x50002, "5.2", UEVersion::UE5_2},
    {0x50001, "5.1", UEVersion::UE5_1},
    {0x50000, "5.0", UEVersion::UE5_0},
    {0x4001B, "4.27", UEVersion::UE4_27},
    {0x4001A, "4.26", UEVersion::UE4_26},
    {0x40019, "4.25", UEVersion::UE4_25},
    {0x40018, "4.24", UEVersion::UE4_24},
    {0x40017, "4.23", UEVersion::UE4_23},
    {0x40016, "4.22", UEVersion::UE4_22},
    {0x40015, "4.21", UEVersion::UE4_21},
    {0x40014, "4.20", UEVersion::UE4_20},
    {0x40013, "4.19", UEVersion::UE4_19},
    {0x40012, "4.18", UEVersion::UE4_18},
    {0x40011, "4.17", UEVersion::UE4_17},
    {0x40010, "4.16", UEVersion::UE4_16},
    {0x4000F, "4.15", UEVersion::UE4_15},
    {0x4000E, "4.14", UEVersion::UE4_14},
    {0x4000D, "4.13", UEVersion::UE4_13},
    {0x4000C, "4.12", UEVersion::UE4_12},
    {0x4000B, "4.11", UEVersion::UE4_11},
    {0x4000A, "4.10", UEVersion::UE4_10},
}};

[[nodiscard]] constexpr UEVersion detect_ue_version(uint32_t file_version_ms, std::string_view str_version) noexcept {
    // Check string prefix first so explicit version overrides or embedded engine strings
    // take precedence over disk file version metadata.
    if (!str_version.empty() && str_version != "0.00") {
        for (const auto& rule : k_ue_version_rules) {
            if (str_version.starts_with(rule.prefix)) {
                return rule.version;
            }
        }
    }

    if (file_version_ms != 0) {
        for (const auto& rule : k_ue_version_rules) {
            if (file_version_ms == rule.file_version_ms) {
                return rule.version;
            }
        }
    }

    return UEVersion::Unknown;
}

} // namespace uevr
