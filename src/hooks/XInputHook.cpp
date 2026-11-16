#include <chrono>
#include <string_view>
#include <thread>
#include <spdlog/spdlog.h>
#include <utility/String.hpp>
#include <utility/Scan.hpp>

#include <SafetyHook.hpp>

#include "Framework.hpp"
#include "Mods.hpp"
#include "XInputHook.hpp"

XInputHook* g_hook{nullptr};

namespace {
HMODULE find_dll(const std::string& name) {
    const auto start_time = std::chrono::steady_clock::now();

    while (true) {
        if (const auto found_dll = GetModuleHandleA(name.c_str()); found_dll != nullptr) {
            return found_dll;
        }

        const auto elapsed_time = std::chrono::steady_clock::now() - start_time;
        if (elapsed_time > std::chrono::seconds(10)) {
            spdlog::error("[XInputHook] Failed to find {} after 10 seconds", name);
            return nullptr;
        }

        // Avoid spinning a core while waiting for delayed XInput module load.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

uintptr_t recursive_resolve_jmp(uint8_t* instr) {
    try {
        const auto decoded = utility::decode_one(instr);

        if (decoded) {
            const auto mnem = std::string_view{decoded->Mnemonic};

            if (mnem.starts_with("JMP")) {
                const auto target = utility::resolve_displacement((uintptr_t)instr);

                if (target.has_value()) {
                    if (instr[0] == 0xFF && instr[1] == 0x25) {
                        const auto real_target = *(uintptr_t*)*target;

                        if (real_target == 0) {
                            return (uintptr_t)instr;
                        }

                        return recursive_resolve_jmp((uint8_t*)real_target);
                    }

                    return recursive_resolve_jmp((uint8_t*)target.value());
                }
            }
        }
    } catch (...) {
        SPDLOG_ERROR("[XInputHook] recursive_resolve_jmp exception");
    }

    return (uintptr_t)instr;
}

void hook_with_jmp_fallback(
    SafetyHookInline& out_hook,
    void* target_fn,
    void* hook_fn,
    const char* api_name,
    std::string_view tag
) {
    if (target_fn == nullptr) {
        spdlog::error("[XInputHook] Failed to find {}", api_name);
        return;
    }

    out_hook = safetyhook::create_inline(target_fn, hook_fn);
    if (out_hook) {
        return;
    }

    spdlog::error("Failed to hook {} ({}), trying jmp", api_name, tag);

    // Check if there is a jmp instruction at the start of the function and try to hook that instead.
    const auto jmp_addr = recursive_resolve_jmp((uint8_t*)target_fn);
    if (jmp_addr == (uintptr_t)target_fn) {
        spdlog::error("Cannot try jmp hook for {} ({}) (jmp) (recursive_resolve_jmp failed)", api_name, tag);
        return;
    }

    out_hook = safetyhook::create_inline((void*)jmp_addr, hook_fn);
    if (!out_hook) {
        spdlog::error("Failed to hook {} ({}) (jmp)", api_name, tag);
    }
}

void hook_xinput_dll(
    const std::string& dll_name,
    std::string_view tag,
    SafetyHookInline& get_state_hook,
    void* get_state_detour,
    SafetyHookInline& set_state_hook,
    void* set_state_detour
) {
    const auto dll = find_dll(dll_name);
    if (dll != nullptr) {
        std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

        const auto get_state_fn = (void*)GetProcAddress(dll, "XInputGetState");
        const auto set_state_fn = (void*)GetProcAddress(dll, "XInputSetState");

        hook_with_jmp_fallback(get_state_hook, get_state_fn, get_state_detour, "XInputGetState", tag);
        hook_with_jmp_fallback(set_state_hook, set_state_fn, set_state_detour, "XInputSetState", tag);
    }

    spdlog::info("[XInputHook] Done ({})", tag);
}

uint32_t dispatch_get_state(SafetyHookInline& hook, uint32_t user_index, XINPUT_STATE* state) {
    auto ret = hook.call<uint32_t>(user_index, state);

    if (!g_framework->is_ready()) {
        return ret;
    }

    for (auto& mod : g_framework->get_mods()->get_mods()) {
        mod->on_xinput_get_state(&ret, user_index, state);
    }

    return ret;
}

uint32_t dispatch_set_state(SafetyHookInline& hook, uint32_t user_index, XINPUT_VIBRATION* vibration) {
    auto ret = hook.call<uint32_t>(user_index, vibration);

    if (!g_framework->is_ready()) {
        return ret;
    }

    for (auto& mod : g_framework->get_mods()->get_mods()) {
        mod->on_xinput_set_state(&ret, user_index, vibration);
    }

    return ret;
}
} // namespace

XInputHook::XInputHook() {
    g_hook = this;
    spdlog::info("[XInputHook] Entry");

    // We use a thread because this may possibly take a while to search for the DLLs
    // and it shouldn't cause any issues anyway
    spdlog::info("[XInputHook] Starting hook thread");
    m_hook_thread_1_4 = std::make_unique<std::jthread>([this]() {
        hook_xinput_dll(
            "xinput1_4.dll",
            "1_4",
            m_xinput_1_4_get_state_hook,
            (void*)&get_state_hook_1_4,
            m_xinput_1_4_set_state_hook,
            (void*)&set_state_hook_1_4
        );
    });
    m_hook_thread_1_3 = std::make_unique<std::jthread>([this]() {
        hook_xinput_dll(
            "xinput1_3.dll",
            "1_3",
            m_xinput_1_3_get_state_hook,
            (void*)&get_state_hook_1_3,
            m_xinput_1_3_set_state_hook,
            (void*)&set_state_hook_1_3
        );
    });
    spdlog::info("[XInputHook] Hook thread started");
}

uint32_t XInputHook::get_state_hook_1_4(uint32_t user_index, XINPUT_STATE* state) {
    return dispatch_get_state(g_hook->m_xinput_1_4_get_state_hook, user_index, state);
}

uint32_t XInputHook::set_state_hook_1_4(uint32_t user_index, XINPUT_VIBRATION* vibration) {
    return dispatch_set_state(g_hook->m_xinput_1_4_set_state_hook, user_index, vibration);
}

uint32_t XInputHook::get_state_hook_1_3(uint32_t user_index, XINPUT_STATE* state) {
    return dispatch_get_state(g_hook->m_xinput_1_3_get_state_hook, user_index, state);
}

uint32_t XInputHook::set_state_hook_1_3(uint32_t user_index, XINPUT_VIBRATION* vibration) {
    return dispatch_set_state(g_hook->m_xinput_1_3_set_state_hook, user_index, vibration);
}