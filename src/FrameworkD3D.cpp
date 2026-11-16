#include <chrono>

#include <windows.h>

#include <spdlog/spdlog.h>

#include <imgui.h>
#include "uevr-imgui/imgui_impl_dx11.h"
#include "uevr-imgui/imgui_impl_dx12.h"
#include "uevr-imgui/imgui_impl_win32.h"

#include "Mods.hpp"
#include "Framework.hpp"

bool Framework::hook_d3d11() {
    m_d3d11_hook.reset();
    m_d3d11_hook = std::make_unique<D3D11Hook>();
    m_d3d11_hook->on_present([this](D3D11Hook& hook) { on_frame_d3d11(); });
    m_d3d11_hook->on_post_present([this](D3D11Hook& hook) { on_post_present_d3d11(); });
    m_d3d11_hook->on_resize_buffers([this](D3D11Hook& hook, uint32_t w, uint32_t h) { on_reset(w, h); });

    // Making sure D3D12 is not hooked
    if (!m_is_d3d12) {
        if (m_d3d11_hook->hook()) {
            spdlog::info("Hooked DirectX 11");
            m_valid = true;
            m_is_d3d11 = true;
            return true;
        }
        // We make sure to no unhook any unwanted hooks if D3D11 didn't get hooked properly
        if (m_d3d11_hook->unhook()) {
            spdlog::info("D3D11 unhooked!");
        } else {
            spdlog::info("Cannot unhook D3D11, this might crash.");
        }

        m_valid = false;
        m_is_d3d11 = false;
        return false;
    }

    return false;
}

bool Framework::hook_d3d12() {
    // windows 7?
    if (LoadLibraryA("d3d12.dll") == nullptr) {
        spdlog::info("d3d12.dll not found, user is probably running Windows 7.");
        spdlog::info("Falling back to hooking D3D11.");

        m_is_d3d12 = false;
        return hook_d3d11();
    }

    m_d3d12_hook.reset();
    m_d3d12_hook = std::make_unique<D3D12Hook>();
    m_d3d12_hook->on_present([this](D3D12Hook& hook) { on_frame_d3d12(); });
    m_d3d12_hook->on_post_present([this](D3D12Hook& hook) { on_post_present_d3d12(); });
    m_d3d12_hook->on_resize_buffers([this](D3D12Hook& hook, uint32_t w, uint32_t h) { on_reset(w, h); });
    m_d3d12_hook->on_resize_target([this](D3D12Hook& hook, uint32_t w, uint32_t h) { on_reset(w, h); });

    // Making sure D3D11 is not hooked
    if (!m_is_d3d11) {
        if (m_d3d12_hook->hook()) {
            spdlog::info("Hooked DirectX 12");
            m_valid = true;
            m_is_d3d12 = true;
            return true;
        }
        // We make sure to no unhook any unwanted hooks if D3D12 didn't get hooked properly
        if (m_d3d12_hook->unhook()) {
            spdlog::info("D3D12 Unhooked!");
        } else {
            spdlog::info("Cannot unhook D3D12, this might crash.");
        }

        m_valid = false;
        m_is_d3d12 = false;

        // Try to hook d3d11 instead
        return hook_d3d11();
    }

    return false;
}

void Framework::on_post_present() {
    const auto update_present_time = [this]() {
        const auto now = std::chrono::steady_clock::now();
        if (m_last_present_time <= now) {
            m_last_present_time = now;
        }
    };

    if (!m_error.empty() || !m_initialized || !m_game_data_initialized) {
        update_present_time();
        return;
    }

    for (auto& mod : m_mods->get_mods()) {
        mod->on_post_present();
    }

    update_present_time();
}

void Framework::on_frame_d3d11() {
    std::scoped_lock _{ m_imgui_mtx };

    spdlog::debug("on_frame (D3D11)");

    m_renderer_type = RendererType::D3D11;

    if (!m_initialized) {
        if (!initialize()) {
            spdlog::error("Failed to initialize Framework on DirectX 11");
            return;
        }

        spdlog::info("Framework initialized");
        m_initialized = true;
        return;
    }

    if (m_message_hook_requested) {
        initialize_windows_message_hook();
        initialize_xinput_hook();
        initialize_dinput_hook();
    }

    auto device = m_d3d11_hook->get_device();

    if (device == nullptr) {
        spdlog::error("D3D11 device was null when it shouldn't be, returning...");
        m_initialized = false;
        return;
    }

    bool is_init_ok = first_frame_initialize();

    invalidate_device_objects();

    ImGui_ImplDX11_NewFrame();
    // hooks don't run until after initialization, so we just render the imgui window while initalizing.
    if (!m_has_engine_thread) {
        run_imgui_frame(false);
    }

    if (is_init_ok) {
        m_mods->on_present();
    }

    if (auto draw_data = ImGui::GetDrawData(); draw_data != nullptr) {
        ComPtr<ID3D11DeviceContext> context{};
        float clear_color[]{0.0f, 0.0f, 0.0f, 0.0f};

        m_d3d11_hook->get_device()->GetImmediateContext(&context);
        context->ClearRenderTargetView(m_d3d11.blank_rt_rtv.Get(), clear_color);
        context->ClearRenderTargetView(m_d3d11.rt_rtv.Get(), clear_color);
        context->OMSetRenderTargets(1, m_d3d11.rt_rtv.GetAddressOf(), NULL);
        ImGui_ImplDX11_RenderDrawData(draw_data);

        for (auto& mod : m_mods->get_mods()) {
            mod->on_post_render_vr_framework_dx11(context.Get(), m_d3d11.rt.Get(), m_d3d11.rt_rtv.Get());
        }

        // Set the back buffer to be the render target.
        context->OMSetRenderTargets(1, m_d3d11.bb_rtv.GetAddressOf(), nullptr);
        ImGui_ImplDX11_RenderDrawData(draw_data);
    }

    if (is_init_ok) {
        m_mods->on_post_frame();
    }
}

void Framework::on_post_present_d3d11() {
    on_post_present();
}

// D3D12 Draw function
void Framework::on_frame_d3d12() {
    std::scoped_lock _{ m_imgui_mtx };

    m_renderer_type = RendererType::D3D12;

    auto command_queue = m_d3d12_hook->get_command_queue();

    if (!m_initialized) {
        if (!initialize()) {
            spdlog::error("Failed to initialize Framework on DirectX 12");
            return;
        }

        spdlog::info("Framework initialized");
        m_initialized = true;
        return;
    }

    if (command_queue == nullptr) {
        spdlog::error("Null Command Queue");
        return;
    }

    if (m_message_hook_requested) {
        initialize_windows_message_hook();
        initialize_xinput_hook();
        initialize_dinput_hook();
    }

    auto device = m_d3d12_hook->get_device();

    if (device == nullptr) {
        spdlog::error("D3D12 Device was null when it shouldn't be, returning...");
        m_initialized = false;
        return;
    }

    bool is_init_ok = first_frame_initialize();

    ImGui::GetIO().BackendRendererUserData = m_d3d12.imgui_backend_datas[0];
    const auto prev_cleanup = m_wants_device_object_cleanup;
    invalidate_device_objects();
    ImGui_ImplDX12_NewFrame();

    ImGui::GetIO().BackendRendererUserData = m_d3d12.imgui_backend_datas[1];
    m_wants_device_object_cleanup = prev_cleanup;
    invalidate_device_objects();
    ImGui_ImplDX12_NewFrame();
    // hooks don't run until after initialization, so we just render the imgui window while initalizing.
    if (!m_has_engine_thread) {
        run_imgui_frame(false);
    }

    if (is_init_ok) {
        m_mods->on_present();
    }

    if (m_d3d12.cmd_ctxs.empty()) {
        return;
    }

    auto& cmd_ctx = m_d3d12.cmd_ctxs[m_d3d12.cmd_ctx_index++ % m_d3d12.cmd_ctxs.size()];

    if (cmd_ctx == nullptr) {
        return;
    }

    cmd_ctx->wait(INFINITE);
    if (auto draw_data = ImGui::GetDrawData(); draw_data != nullptr) {
        std::scoped_lock _{ cmd_ctx->mtx };
        cmd_ctx->has_commands = true;

        // Draw to our render target.
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_d3d12.get_rt(D3D12::RTV::IMGUI).Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmd_ctx->cmd_list->ResourceBarrier(1, &barrier);

        float clear_color[]{0.0f, 0.0f, 0.0f, 0.0f};
        D3D12_CPU_DESCRIPTOR_HANDLE rts[1]{};
        cmd_ctx->cmd_list->ClearRenderTargetView(m_d3d12.get_cpu_rtv(device, D3D12::RTV::IMGUI), clear_color, 0, nullptr);
        rts[0] = m_d3d12.get_cpu_rtv(device, D3D12::RTV::IMGUI);
        cmd_ctx->cmd_list->OMSetRenderTargets(1, rts, FALSE, NULL);
        cmd_ctx->cmd_list->SetDescriptorHeaps(1, m_d3d12.srv_desc_heap.GetAddressOf());

        ImGui::GetIO().BackendRendererUserData = m_d3d12.imgui_backend_datas[1];
        ImGui_ImplDX12_RenderDrawData(draw_data, cmd_ctx->cmd_list.Get());

        for (auto& mod : m_mods->get_mods()) {
            rts[0] = m_d3d12.get_cpu_rtv(device, D3D12::RTV::IMGUI);
            mod->on_post_render_vr_framework_dx12(cmd_ctx->cmd_list.Get(), m_d3d12.get_rt(D3D12::RTV::IMGUI).Get(), &rts[0]);
        }

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cmd_ctx->cmd_list->ResourceBarrier(1, &barrier);

        // Draw to the back buffer.
        auto swapchain = m_d3d12_hook->get_swap_chain();
        auto bb_index = swapchain->GetCurrentBackBufferIndex();
        barrier.Transition.pResource = m_d3d12.rts[bb_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmd_ctx->cmd_list->ResourceBarrier(1, &barrier);
        rts[0] = m_d3d12.get_cpu_rtv(device, (D3D12::RTV)bb_index);
        cmd_ctx->cmd_list->OMSetRenderTargets(1, rts, FALSE, NULL);
        cmd_ctx->cmd_list->SetDescriptorHeaps(1, m_d3d12.srv_desc_heap.GetAddressOf());

        ImGui::GetIO().BackendRendererUserData = m_d3d12.imgui_backend_datas[0];
        ImGui_ImplDX12_RenderDrawData(draw_data, cmd_ctx->cmd_list.Get());

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmd_ctx->cmd_list->ResourceBarrier(1, &barrier);

        cmd_ctx->execute();
    }

    if (is_init_ok) {
        m_mods->on_post_frame();
    }
}

void Framework::on_post_present_d3d12() {
    on_post_present();
}

void Framework::on_reset(uint32_t w, uint32_t h) {
    std::scoped_lock _{ m_imgui_mtx };

    spdlog::info("Reset! {} {}", w, h);

    if (m_initialized) {
        // fixes text boxes not being able to receive input
        imgui::reset_keystates();
        ImGui_ImplWin32_Shutdown();
    }

    // Crashes if we don't release it at this point.
    if (m_is_d3d11) {
        deinit_d3d11();
        m_d3d11.rt_width = w;
        m_d3d11.rt_height = h;
    }

    if (m_is_d3d12) {
        deinit_d3d12();
        m_d3d12.rt_width = w;
        m_d3d12.rt_height = h;
    }

    if (m_game_data_initialized) {
        m_mods->on_device_reset();
    }

    m_has_frame = false;
    m_first_initialize = false;
    m_initialized = false;
}

void Framework::invalidate_device_objects() {
    if (!m_wants_device_object_cleanup) {
        return;
    }

    if (m_renderer_type == RendererType::D3D11) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    } else if (m_renderer_type == RendererType::D3D12) {
        ImGui_ImplDX12_InvalidateDeviceObjects();
    }

    m_wants_device_object_cleanup = false;
}

// DirectX 11 Initialization methods

bool Framework::init_d3d11() {
    deinit_d3d11();

    auto swapchain = m_d3d11_hook->get_swap_chain();
    auto device = m_d3d11_hook->get_device();

    // Get back buffer.
    spdlog::info("[D3D11] Creating RTV of back buffer...");

    ComPtr<ID3D11Texture2D> backbuffer{};

    if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
        spdlog::error("[D3D11] Failed to get back buffer!");
        return false;
    }

    // Create a render target view of the back buffer.
    if (FAILED(device->CreateRenderTargetView(backbuffer.Get(), nullptr, &m_d3d11.bb_rtv))) {
        spdlog::error("[D3D11] Failed to create back buffer render target view!");
        return false;
    }

    // Get backbuffer description.
    D3D11_TEXTURE2D_DESC backbuffer_desc{};

    backbuffer->GetDesc(&backbuffer_desc);
    backbuffer_desc.BindFlags |= D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    spdlog::info("[D3D11] Back buffer format is {}", (uint32_t)backbuffer_desc.Format);

    spdlog::info("[D3D11] Creating render targets...");
    {
        // Create our blank render target.
        auto d3d11_rt_desc = backbuffer_desc;
        d3d11_rt_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // For VR

        if (FAILED(device->CreateTexture2D(&d3d11_rt_desc, nullptr, &m_d3d11.blank_rt))) {
            spdlog::error("[D3D11] Failed to create render target texture!");
            return false;
        }

        // Create our render target
        if (FAILED(device->CreateTexture2D(&d3d11_rt_desc, nullptr, &m_d3d11.rt))) {
            spdlog::error("[D3D11] Failed to create render target texture!");
            return false;
        }
    }

    // Create our blank render target view.
    spdlog::info("[D3D11] Creating rtvs...");

    if (FAILED(device->CreateRenderTargetView(m_d3d11.blank_rt.Get(), nullptr, &m_d3d11.blank_rt_rtv))) {
        spdlog::error("[D3D11] Failed to create render target view!");
        return false;
    }

    // Create our render target view.
    if (FAILED(device->CreateRenderTargetView(m_d3d11.rt.Get(), nullptr, &m_d3d11.rt_rtv))) {
        spdlog::error("[D3D11] Failed to create render target view!");
        return false;
    }

    // Create our render target shader resource view.
    spdlog::info("[D3D11] Creating srvs...");

    if (FAILED(device->CreateShaderResourceView(m_d3d11.rt.Get(), nullptr, &m_d3d11.rt_srv))) {
        spdlog::error("[D3D11] Failed to create shader resource view!");
        return false;
    }

    m_d3d11.rt_width = backbuffer_desc.Width;
    m_d3d11.rt_height = backbuffer_desc.Height;
    m_last_rt_size = {backbuffer_desc.Width, backbuffer_desc.Height};

    spdlog::info("[D3D11] Initializing ImGui D3D11...");

    ComPtr<ID3D11DeviceContext> context{};

    device->GetImmediateContext(&context);

    if (!ImGui_ImplDX11_Init(device, context.Get())) {
        spdlog::error("[D3D11] Failed to initialize ImGui.");
        return false;
    }

    return true;
}

void Framework::deinit_d3d11() {
    ImGui_ImplDX11_Shutdown();
    m_d3d11 = {};
}

// DirectX 12 Initialization methods

bool Framework::init_d3d12() {
    deinit_d3d12();

    auto device = m_d3d12_hook->get_device();

    spdlog::info("[D3D12] Creating command allocator...");

    m_d3d12.cmd_ctxs.clear();

    for (auto i = 0; i < 3; ++i) {
        auto& ctx = m_d3d12.cmd_ctxs.emplace_back(std::make_unique<d3d12::CommandContext>());

        if (!ctx->setup(L"Framework::m_d3d12.cmd_ctx")) {
            spdlog::error("[D3D12] Failed to create command context.");
            return false;
        }
    }

    spdlog::info("[D3D12] Creating RTV descriptor heap...");

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};

        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = (int)D3D12::RTV::COUNT;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;

        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_d3d12.rtv_desc_heap)))) {
            spdlog::error("[D3D12] Failed to create RTV descriptor heap.");
            return false;
        }

        m_d3d12.rtv_desc_heap->SetName(L"Framework::m_d3d12.rtv_desc_heap");
    }

    spdlog::info("[D3D12] Creating SRV descriptor heap...");

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};

        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = (int)D3D12::SRV::COUNT;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_d3d12.srv_desc_heap)))) {
            spdlog::error("[D3D12] Failed to create SRV descriptor heap.");
            return false;
        }

        m_d3d12.srv_desc_heap->SetName(L"Framework::m_d3d12.srv_desc_heap");
    }

    spdlog::info("[D3D12] Creating render targets...");

    {
        // Create back buffer rtvs.
        auto swapchain = m_d3d12_hook->get_swap_chain();

        DXGI_SWAP_CHAIN_DESC swap_desc{};
        swapchain->GetDesc(&swap_desc);

        for (auto i = 0; i < swap_desc.BufferCount; ++i) {
            if (i > (int)D3D12::RTV::BACKBUFFER_8) {
                spdlog::warn("[D3D12] Swap chain has more back buffers than we support. Stopping at 8.");
                break; // assume max
            }

            if (SUCCEEDED(swapchain->GetBuffer(i, IID_PPV_ARGS(&m_d3d12.rts[i])))) {
                device->CreateRenderTargetView(m_d3d12.rts[i].Get(), nullptr, m_d3d12.get_cpu_rtv(device, (D3D12::RTV)i));
            } else {
                spdlog::error("[D3D12] Failed to get back buffer for rtv.");
                break; // assume max
            }
        }

        // Create our imgui and blank rts.
        auto& backbuffer = m_d3d12.get_rt(D3D12::RTV::BACKBUFFER_0);
        auto desc = backbuffer->GetDesc();

        spdlog::info("[D3D12] Back buffer format is {}", (uint32_t)desc.Format);

        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_DEFAULT;
        props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        auto d3d12_rt_desc = desc;
        d3d12_rt_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // For VR

        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format = d3d12_rt_desc.Format;

        if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &d3d12_rt_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear_value,
                IID_PPV_ARGS(&m_d3d12.get_rt(D3D12::RTV::IMGUI))))) {
            spdlog::error("[D3D12] Failed to create the imgui render target.");
            return false;
        }

        m_d3d12.get_rt(D3D12::RTV::IMGUI)->SetName(L"Framework::m_d3d12.rts[IMGUI]");

        if (FAILED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &d3d12_rt_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear_value,
                IID_PPV_ARGS(&m_d3d12.get_rt(D3D12::RTV::BLANK))))) {
            spdlog::error("[D3D12] Failed to create the blank render target.");
            return false;
        }

        m_d3d12.get_rt(D3D12::RTV::BLANK)->SetName(L"Framework::m_d3d12.rts[BLANK]");

        // Create imgui and blank rtvs and srvs.
        device->CreateRenderTargetView(m_d3d12.get_rt(D3D12::RTV::IMGUI).Get(), nullptr, m_d3d12.get_cpu_rtv(device, D3D12::RTV::IMGUI));
        device->CreateRenderTargetView(m_d3d12.get_rt(D3D12::RTV::BLANK).Get(), nullptr, m_d3d12.get_cpu_rtv(device, D3D12::RTV::BLANK));
        device->CreateShaderResourceView(
            m_d3d12.get_rt(D3D12::RTV::IMGUI).Get(), nullptr, m_d3d12.get_cpu_srv(device, D3D12::SRV::IMGUI_VR));
        device->CreateShaderResourceView(m_d3d12.get_rt(D3D12::RTV::BLANK).Get(), nullptr, m_d3d12.get_cpu_srv(device, D3D12::SRV::BLANK));

        m_d3d12.rt_width = (uint32_t)desc.Width;
        m_d3d12.rt_height = (uint32_t)desc.Height;

        m_last_rt_size = {desc.Width, desc.Height};
    }

    spdlog::info("[D3D12] Initializing ImGui...");

    auto& bb = m_d3d12.get_rt(D3D12::RTV::BACKBUFFER_0);
    auto bb_desc = bb->GetDesc();

    if (!ImGui_ImplDX12_Init(device, 3, bb_desc.Format, m_d3d12.srv_desc_heap.Get(),
            m_d3d12.get_cpu_srv(device, D3D12::SRV::IMGUI_FONT_BACKBUFFER), m_d3d12.get_gpu_srv(device, D3D12::SRV::IMGUI_FONT_BACKBUFFER))) {
        spdlog::error("[D3D12] Failed to initialize ImGui.");
        return false;
    }

    m_d3d12.imgui_backend_datas[0] = ImGui::GetIO().BackendRendererUserData;

    ImGui::GetIO().BackendRendererUserData = nullptr;

    // Now initialize another one for the VR texture.
    auto& bb_vr = m_d3d12.get_rt(D3D12::RTV::IMGUI);
    auto bb_vr_desc = bb_vr->GetDesc();

    if (!ImGui_ImplDX12_Init(device, 3, bb_vr_desc.Format, m_d3d12.srv_desc_heap.Get(),
            m_d3d12.get_cpu_srv(device, D3D12::SRV::IMGUI_FONT_VR), m_d3d12.get_gpu_srv(device, D3D12::SRV::IMGUI_FONT_VR))) {
        spdlog::error("[D3D12] Failed to initialize ImGui.");
        return false;
    }

    m_d3d12.imgui_backend_datas[1] = ImGui::GetIO().BackendRendererUserData;

    return true;
}

void Framework::deinit_d3d12() {
    for (auto& ctx : m_d3d12.cmd_ctxs) {
        if (ctx != nullptr) {
            ctx->reset();
        }
    }

    m_d3d12.cmd_ctxs.clear();

    for (auto userdata : m_d3d12.imgui_backend_datas) {
        if (userdata != nullptr) {
            ImGui::GetIO().BackendRendererUserData = userdata;
            ImGui_ImplDX12_Shutdown();
        }
    }

    ImGui::GetIO().BackendRendererUserData = nullptr;
    m_d3d12 = {};
}
