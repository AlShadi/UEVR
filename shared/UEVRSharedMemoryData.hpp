#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr size_t UEVR_SHARED_MEMORY_MAX_PATH = 260;

#pragma pack(push, 1)
struct UEVRSharedMemoryData {
    wchar_t path[UEVR_SHARED_MEMORY_MAX_PATH]{}; // Path to the game exe
    uint32_t pid{};                              // Process ID of the game
    uint32_t main_thread_id{};                   // Main thread ID of the game
    uint32_t command_thread_id{};                // Thread ID commands are sent to (via PostThreadMessage)
    bool signal_frontend_config_setup{false};
};
#pragma pack(pop)

// Must stay byte-for-byte compatible with frontend/UEVR/UEVRSharedMemory.cs (SharedMemory.Data).
static_assert(sizeof(UEVRSharedMemoryData) == 533, "UEVRSharedMemoryData ABI size mismatch");
static_assert(offsetof(UEVRSharedMemoryData, path) == 0);
static_assert(offsetof(UEVRSharedMemoryData, pid) == 520);
static_assert(offsetof(UEVRSharedMemoryData, main_thread_id) == 524);
static_assert(offsetof(UEVRSharedMemoryData, command_thread_id) == 528);
static_assert(offsetof(UEVRSharedMemoryData, signal_frontend_config_setup) == 532);
