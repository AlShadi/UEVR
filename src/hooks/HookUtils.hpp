#pragma once

#include <cstring>
#include <mutex>
#include <vector>

#include <windows.h>
#include <spdlog/spdlog.h>

#include <utility/Module.hpp>
#include <utility/Patch.hpp>

namespace hooks {
// Temporarily restores original on-disk prologue bytes of an API export while creating
// dummy D3D/DXGI devices so third-party overlays (e.g. ReShade) do not intercept them.
class ScopedPrologueRestore {
public:
    ScopedPrologueRestore(void* target, const char* name)
        : m_target{target}
        , m_name{name}
        , m_lock{patch_mutex()}
    {
        if (m_target == nullptr) {
            return;
        }

        const auto original_bytes = utility::get_original_bytes(m_target);
        if (!original_bytes || original_bytes->empty()) {
            return;
        }

        spdlog::info("{} appears to be hooked, temporarily unhooking", m_name);
        m_hooked_bytes.resize(original_bytes->size());
        std::memcpy(m_hooked_bytes.data(), m_target, original_bytes->size());

        ProtectionOverride protection_override{m_target, original_bytes->size(), PAGE_EXECUTE_READWRITE};
        std::memcpy(m_target, original_bytes->data(), original_bytes->size());
        FlushInstructionCache(GetCurrentProcess(), m_target, original_bytes->size());
        m_active = true;
    }

    ~ScopedPrologueRestore() {
        if (!m_active || m_target == nullptr || m_hooked_bytes.empty()) {
            return;
        }

        spdlog::info("Restoring hooked bytes for {}", m_name);
        ProtectionOverride protection_override{m_target, m_hooked_bytes.size(), PAGE_EXECUTE_READWRITE};
        std::memcpy(m_target, m_hooked_bytes.data(), m_hooked_bytes.size());
        FlushInstructionCache(GetCurrentProcess(), m_target, m_hooked_bytes.size());
    }

    ScopedPrologueRestore(const ScopedPrologueRestore&) = delete;
    ScopedPrologueRestore& operator=(const ScopedPrologueRestore&) = delete;

private:
    static std::mutex& patch_mutex() {
        static std::mutex mtx;
        return mtx;
    }

    void* m_target{nullptr};
    const char* m_name{""};
    std::unique_lock<std::mutex> m_lock;
    std::vector<uint8_t> m_hooked_bytes{};
    bool m_active{false};
};
} // namespace hooks
