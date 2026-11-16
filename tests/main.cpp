#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <UEVRSharedMemoryData.hpp>
#include <WindowFilter.hpp>
#include <mods/vr/UEVersion.hpp>
#include <sdk/Math.hpp>
#include <utility/Config.hpp>
#include <ScriptState.hpp>
#include <uevr/API.h>
#include <uevr/API.hpp>

namespace {
int g_failures = 0;

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back({name, fn});
    }
};

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

#define TEST_CASE(name)                                                                                \
    static void CONCAT(test_fn_, __LINE__)();                                                          \
    static Registrar CONCAT(test_reg_, __LINE__)(name, &CONCAT(test_fn_, __LINE__));                  \
    static void CONCAT(test_fn_, __LINE__)()

#define REQUIRE(cond)                                                                                  \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            throw std::runtime_error(std::string("Assertion failed at line ") +                        \
                                     std::to_string(__LINE__) + ": " #cond);                           \
        }                                                                                              \
    } while (false)

#define REQUIRE_NEAR(a, b, eps) REQUIRE(std::fabs((a) - (b)) <= (eps))

// Stub UEVR_PluginInitializeParam so ScriptState/ScriptContext can initialize offline in unit tests.
struct StubLuaHost {
    UEVR_PluginVersion version{UEVR_PLUGIN_VERSION_MAJOR, UEVR_PLUGIN_VERSION_MINOR, UEVR_PLUGIN_VERSION_PATCH};
    UEVR_PluginFunctions functions{};
    UEVR_PluginCallbacks callbacks{};
    UEVR_RendererData renderer{};
    UEVR_VRData vr{};
    UEVR_SDKCallbacks sdk_callbacks{};
    UEVR_SDKData sdk{};
    UEVR_LuaData lua_data{};
    UEVR_PluginInitializeParam param{};

    StubLuaHost() {
        functions.log_error = [](const char*, ...) {};
        functions.log_warn = [](const char*, ...) {};
        functions.log_info = [](const char*, ...) {};
        functions.remove_callback = [](void*) { return true; };

        callbacks.on_xinput_get_state = [](UEVR_OnXInputGetStateCb) { return true; };
        callbacks.on_xinput_set_state = [](UEVR_OnXInputSetStateCb) { return true; };

        sdk_callbacks.on_pre_engine_tick = [](UEVR_Engine_TickCb) { return true; };
        sdk_callbacks.on_post_engine_tick = [](UEVR_Engine_TickCb) { return true; };
        sdk_callbacks.on_pre_slate_draw_window_render_thread = [](UEVR_Slate_DrawWindow_RenderThreadCb) { return true; };
        sdk_callbacks.on_post_slate_draw_window_render_thread = [](UEVR_Slate_DrawWindow_RenderThreadCb) { return true; };
        sdk_callbacks.on_pre_calculate_stereo_view_offset = [](UEVR_Stereo_CalculateStereoViewOffsetCb) { return true; };
        sdk_callbacks.on_post_calculate_stereo_view_offset = [](UEVR_Stereo_CalculateStereoViewOffsetCb) { return true; };
        sdk_callbacks.on_pre_viewport_client_draw = [](UEVR_ViewportClient_DrawCb) { return true; };
        sdk_callbacks.on_post_viewport_client_draw = [](UEVR_ViewportClient_DrawCb) { return true; };
        sdk_callbacks.on_early_calculate_stereo_view_offset = [](UEVR_Stereo_CalculateStereoViewOffsetCb) { return true; };

        sdk.callbacks = &sdk_callbacks;
        lua_data.add_additional_bindings = [](lua_State*) {};

        param.version = &version;
        param.functions = &functions;
        param.callbacks = &callbacks;
        param.renderer = &renderer;
        param.vr = &vr;
        param.sdk = &sdk;
        param.lua = &lua_data;
    }
};

StubLuaHost& shared_stub_host() {
    static StubLuaHost host{};
    return host;
}
} // namespace

TEST_CASE("SharedMemoryData ABI layout") {
    UEVRSharedMemoryData data{};
    REQUIRE(sizeof(data) == 533);
    REQUIRE(data.pid == 0);
    REQUIRE(data.main_thread_id == 0);
    REQUIRE(data.command_thread_id == 0);
    REQUIRE(data.signal_frontend_config_setup == false);
    REQUIRE(data.path[0] == L'\0');
}

TEST_CASE("utility::Config round-trip") {
    const auto temp_path = std::filesystem::temp_directory_path() / "uevr_test_config.txt";
    std::filesystem::remove(temp_path);

    {
        utility::Config cfg;
        cfg.set<bool>("VR_UncapFramerate", true);
        cfg.set<bool>("VR_DisableBlur", false);
        cfg.set<int>("VR_RenderingMethod", 1);
        cfg.set<float>("VR_WorldScale", 1.25f);
        cfg.set("Frontend_RequestedRuntime", std::string{"openxr_loader.dll"});
        REQUIRE(cfg.save(temp_path.string()));
    }

    {
        utility::Config loaded;
        REQUIRE(loaded.load(temp_path.string()));
        REQUIRE(loaded.get<bool>("VR_UncapFramerate") == true);
        REQUIRE(loaded.get<bool>("VR_DisableBlur") == false);
        REQUIRE(loaded.get<int>("VR_RenderingMethod") == 1);
        REQUIRE(loaded.get<float>("VR_WorldScale").has_value());
        REQUIRE_NEAR(*loaded.get<float>("VR_WorldScale"), 1.25f, 1e-4f);
        REQUIRE(loaded.get("Frontend_RequestedRuntime") == std::string{"openxr_loader.dll"});
        REQUIRE(!loaded.get("NonExistentKey").has_value());
    }

    std::filesystem::remove(temp_path);
}

TEST_CASE("sdk::Math coordinate conversions") {
    const glm::vec3 v{1.5f, -2.25f, 4.0f};
    const auto ue_v = utility::math::glm_to_ue4(v);
    const auto back_v = utility::math::ue4_to_glm(ue_v);
    REQUIRE_NEAR(back_v.x, v.x, 1e-5f);
    REQUIRE_NEAR(back_v.y, v.y, 1e-5f);
    REQUIRE_NEAR(back_v.z, v.z, 1e-5f);

    const glm::quat q = glm::normalize(glm::quat{0.9238795f, 0.1f, -0.2f, 0.3f});
    const auto ue_q = utility::math::glm_to_ue4(q);
    const auto back_q = utility::math::ue4_to_glm(ue_q);
    REQUIRE_NEAR(back_q.w, q.w, 1e-5f);
    REQUIRE_NEAR(back_q.x, q.x, 1e-5f);
    REQUIRE_NEAR(back_q.y, q.y, 1e-5f);
    REQUIRE_NEAR(back_q.z, q.z, 1e-5f);

    const float wrapped = utility::math::fix_angle(glm::radians(540.0f));
    REQUIRE_NEAR(wrapped, glm::radians(180.0f), 1e-4f);
}

TEST_CASE("ScriptState os sandbox and GC lifecycle") {
    auto& host = shared_stub_host();
    uevr::ScriptState::GarbageCollectionData gc{};
    gc.gc_handler = uevr::ScriptState::GarbageCollectionHandler::UEVR_MANAGED;
    gc.gc_type = uevr::ScriptState::GarbageCollectionType::STEP;
    gc.gc_mode = uevr::ScriptState::GarbageCollectionMode::GENERATIONAL;

    uevr::ScriptState state{gc, &host.param, true};
    REQUIRE(state.context() != nullptr);
    REQUIRE(state.context()->valid());

    auto& lua = state.lua();
    REQUIRE(lua["uevr"].valid());
    REQUIRE(!lua["os"]["execute"].valid());
    REQUIRE(!lua["os"]["remove"].valid());
    REQUIRE(!lua["os"]["rename"].valid());
    REQUIRE(!lua["os"]["exit"].valid());
    REQUIRE(!lua["os"]["setlocale"].valid());
    REQUIRE(!lua["os"]["getenv"].valid());

    auto res = lua.safe_script("return 21 * 2");
    REQUIRE(res.valid());
    REQUIRE(res.get<int>() == 42);

    state.on_frame();
    gc.gc_mode = uevr::ScriptState::GarbageCollectionMode::INCREMENTAL;
    state.gc_data_changed(gc);
    state.on_frame();
    state.on_script_reset();
}

TEST_CASE("UEVersion detection from file version and string override") {
    REQUIRE(uevr::detect_ue_version(0, "5.7.0") == uevr::UEVersion::UE5_7);
    REQUIRE(uevr::detect_ue_version(0, "5.6.1") == uevr::UEVersion::UE5_6);
    REQUIRE(uevr::detect_ue_version(0, "5.5.4") == uevr::UEVersion::UE5_5);
    REQUIRE(uevr::detect_ue_version(0, "5.4.1-release") == uevr::UEVersion::UE5_4);
    REQUIRE(uevr::detect_ue_version(0, "5.3.2") == uevr::UEVersion::UE5_3);
    REQUIRE(uevr::detect_ue_version(0, "5.0.3") == uevr::UEVersion::UE5_0);
    REQUIRE(uevr::detect_ue_version(0, "4.27.2") == uevr::UEVersion::UE4_27);
    REQUIRE(uevr::detect_ue_version(0, "4.18.0") == uevr::UEVersion::UE4_18);
    REQUIRE(uevr::detect_ue_version(0, "4.10.4") == uevr::UEVersion::UE4_10);

    // String override takes precedence even if file_version_ms has a higher version value.
    REQUIRE(uevr::detect_ue_version(0x50004, "4.27.2") == uevr::UEVersion::UE4_27);

    // Disk fallback checks dwFileVersionMS when str_version is "0.00" or empty.
    REQUIRE(uevr::detect_ue_version(0x50007, "0.00") == uevr::UEVersion::UE5_7);
    REQUIRE(uevr::detect_ue_version(0x50006, "0.00") == uevr::UEVersion::UE5_6);
    REQUIRE(uevr::detect_ue_version(0x50005, "0.00") == uevr::UEVersion::UE5_5);
    REQUIRE(uevr::detect_ue_version(0x50003, "0.00") == uevr::UEVersion::UE5_3);
    REQUIRE(uevr::detect_ue_version(0x4001B, "0.00") == uevr::UEVersion::UE4_27);
    REQUIRE(uevr::detect_ue_version(0x40012, "0.00") == uevr::UEVersion::UE4_18);
    REQUIRE(uevr::detect_ue_version(0x4000A, "0.00") == uevr::UEVersion::UE4_10);
    REQUIRE(uevr::detect_ue_version(0x40009, "4.9.0") == uevr::UEVersion::Unknown);

    // AVX / SSE2 double-precision (LWC) instruction mnemonic classification.
    REQUIRE(uevr::is_double_precision_arithmetic("ADDSD") == true);
    REQUIRE(uevr::is_double_precision_arithmetic("VADDSD") == true);
    REQUIRE(uevr::is_double_precision_arithmetic("VMULSD") == true);
    REQUIRE(uevr::is_double_precision_arithmetic("VFMADD231SD") == true);
    REQUIRE(uevr::is_double_precision_arithmetic("VSUBPD") == true);
    REQUIRE(uevr::is_double_precision_arithmetic("ADDSS") == false);
    REQUIRE(uevr::is_double_precision_arithmetic("VADDSS") == false);
    REQUIRE(uevr::is_double_precision_arithmetic("CVTSS2SD") == false);
    REQUIRE(uevr::is_double_precision_arithmetic("VCVTSS2SD") == false);
}

TEST_CASE("WindowFilter worker thread stays alive after empty tick") {
    WindowFilter filter{};

    auto pump_messages_for = [](std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            MSG msg{};
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    // Wait >150ms so the worker thread executes at least one empty tick.
    pump_messages_for(std::chrono::milliseconds(160));

    HWND normal_hwnd = CreateWindowExA(0, "STATIC", "NormalGameWindow", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    HWND filtered_hwnd = CreateWindowExA(0, "STATIC", "UE4SS Console", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    REQUIRE(normal_hwnd != nullptr);
    REQUIRE(filtered_hwnd != nullptr);

    // Initial check queues both windows and filters them until the background job completes.
    REQUIRE(filter.is_filtered(normal_hwnd) == true);
    REQUIRE(filter.is_filtered(filtered_hwnd) == true);

    pump_messages_for(std::chrono::milliseconds(160));

    // After the background job runs, normal_hwnd must be unfiltered while UE4SS remains filtered.
    REQUIRE(filter.is_filtered(normal_hwnd) == false);
    REQUIRE(filter.is_filtered(filtered_hwnd) == true);

    DestroyWindow(normal_hwnd);
    DestroyWindow(filtered_hwnd);
}

TEST_CASE("API::VR::trigger_haptic_vibration parameter order and Lua quaternion offset") {
    static float s_seconds = 0.0f;
    static float s_duration = 0.0f;
    static float s_frequency = 0.0f;
    static float s_amplitude = 0.0f;
    static UEVR_Quaternionf s_rot_offset{};

    auto& host = shared_stub_host();
    host.vr.trigger_haptic_vibration = [](float seconds_from_now, float duration, float frequency, float amplitude, UEVR_InputSourceHandle) {
        s_seconds = seconds_from_now;
        s_duration = duration;
        s_frequency = frequency;
        s_amplitude = amplitude;
    };
    host.vr.is_using_controllers = []() { return true; };

    UEVR_UObjectHookMotionControllerStateFunctions mc_funcs{};
    mc_funcs.set_rotation_offset = [](UEVR_UObjectHookMotionControllerStateHandle, const UEVR_Quaternionf* quat) {
        if (quat != nullptr) {
            s_rot_offset = *quat;
        }
    };
    UEVR_UObjectHookFunctions uobj_hook_funcs{};
    uobj_hook_funcs.mc_state = &mc_funcs;
    uobj_hook_funcs.get_or_add_motion_controller_state = [](UEVR_UObjectHandle) -> UEVR_UObjectHookMotionControllerStateHandle {
        static int dummy_state = 1;
        return reinterpret_cast<UEVR_UObjectHookMotionControllerStateHandle>(&dummy_state);
    };
    host.sdk.uobject_hook = &uobj_hook_funcs;

    uevr::API::initialize(&host.param);
    uevr::API::VR::trigger_haptic_vibration(0.125f, 1.75f, 160.0f, 0.85f, nullptr);
    REQUIRE_NEAR(s_seconds, 0.125f, 1e-5f);
    REQUIRE_NEAR(s_duration, 1.75f, 1e-5f);
    REQUIRE_NEAR(s_frequency, 160.0f, 1e-5f);
    REQUIRE_NEAR(s_amplitude, 0.85f, 1e-5f);
    REQUIRE(uevr::API::VR::is_using_controllers() == true);
    REQUIRE(uevr::API::VR::is_using_contriollers() == true);

    uevr::ScriptState::GarbageCollectionData gc{};
    uevr::ScriptState state{gc, &host.param, true};
    auto& lua = state.lua();

    auto* mc = uevr::API::UObjectHook::get_or_add_motion_controller_state(reinterpret_cast<uevr::API::UObject*>(0x1));
    lua["test_mc"] = mc;

    auto res = lua.safe_script("test_mc:set_rotation_offset(Vector4f.new(0.1, 0.2, 0.3, 0.9))");
    REQUIRE(res.valid());
    REQUIRE_NEAR(s_rot_offset.w, 0.9f, 1e-5f);
    REQUIRE_NEAR(s_rot_offset.x, 0.1f, 1e-5f);
    REQUIRE_NEAR(s_rot_offset.y, 0.2f, 1e-5f);
    REQUIRE_NEAR(s_rot_offset.z, 0.3f, 1e-5f);

    auto res_d = lua.safe_script("test_mc:set_rotation_offset(Vector4d.new(-0.1, 0.25, -0.35, 0.85))");
    REQUIRE(res_d.valid());
    REQUIRE_NEAR(s_rot_offset.w, 0.85f, 1e-5f);
    REQUIRE_NEAR(s_rot_offset.x, -0.1f, 1e-5f);
    REQUIRE_NEAR(s_rot_offset.y, 0.25f, 1e-5f);
    REQUIRE_NEAR(s_rot_offset.z, -0.35f, 1e-5f);
}

int main() {
    std::cout << "Running " << registry().size() << " UEVR C++ tests...\n";
    for (const auto& t : registry()) {
        try {
            t.fn();
            std::cout << "[PASS] " << t.name << "\n";
        } catch (const std::exception& e) {
            ++g_failures;
            std::cerr << "[FAIL] " << t.name << ": " << e.what() << "\n";
        } catch (...) {
            ++g_failures;
            std::cerr << "[FAIL] " << t.name << ": unknown exception\n";
        }
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All UEVR C++ tests passed.\n";
    return 0;
}
