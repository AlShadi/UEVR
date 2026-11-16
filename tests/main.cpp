#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <UEVRSharedMemoryData.hpp>
#include <mods/vr/UEVersion.hpp>
#include <sdk/Math.hpp>
#include <utility/Config.hpp>
#include <ScriptState.hpp>
#include <uevr/API.h>

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
    StubLuaHost host;
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
    REQUIRE(uevr::detect_ue_version(0, "5.4.1-release") == uevr::UEVersion::UE5_4);
    REQUIRE(uevr::detect_ue_version(0, "5.3.2") == uevr::UEVersion::UE5_3);
    REQUIRE(uevr::detect_ue_version(0, "5.0.3") == uevr::UEVersion::UE5_0);
    REQUIRE(uevr::detect_ue_version(0, "4.27.2") == uevr::UEVersion::UE4_27);
    REQUIRE(uevr::detect_ue_version(0, "4.18.0") == uevr::UEVersion::UE4_18);
    REQUIRE(uevr::detect_ue_version(0, "4.10.4") == uevr::UEVersion::UE4_10);

    // String override passes file_version_ms == 0, while disk fallback checks dwFileVersionMS.
    REQUIRE(uevr::detect_ue_version(0x50003, "0.00") == uevr::UEVersion::UE5_3);
    REQUIRE(uevr::detect_ue_version(0x4001B, "0.00") == uevr::UEVersion::UE4_27);
    REQUIRE(uevr::detect_ue_version(0x40012, "0.00") == uevr::UEVersion::UE4_18);
    REQUIRE(uevr::detect_ue_version(0x4000A, "0.00") == uevr::UEVersion::UE4_10);
    REQUIRE(uevr::detect_ue_version(0x40009, "4.9.0") == uevr::UEVersion::Unknown);
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
