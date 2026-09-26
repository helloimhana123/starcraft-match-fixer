#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::uintptr_t kGameSpeedAddress = 0x006CDFD4;
constexpr std::uintptr_t kSpeedModifiersAddress = 0x005124D8;
constexpr std::uintptr_t kLatencyFramesAddress = 0x0051CE70;
constexpr std::size_t kSpeedCount = 7;
constexpr std::uint32_t kFastestSpeedIndex = 6;
constexpr std::uint32_t kTargetLatencyFrames = 3;
constexpr std::array<std::uint32_t, kSpeedCount> kExpectedSpeedModifiers{
    167, 111, 83, 67, 56, 48, 42};

std::atomic<bool> g_running{false};
HANDLE g_thread = nullptr;

std::FILE* open_log() {
    char path[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "PLUTO_FASTEST_LATENCY_LOG", path, static_cast<DWORD>(sizeof(path)));

    const char* log_path =
        (length == 0 || length >= sizeof(path)) ? "PlutoFastestLatencyFix.log" : path;
    std::FILE* log = nullptr;
    if (fopen_s(&log, log_path, "a") != 0) {
        return nullptr;
    }
    return log;
}

void log_line(const char* message) {
    if (std::FILE* log = open_log()) {
        std::fprintf(log, "%s\n", message);
        std::fflush(log);
        std::fclose(log);
    }
}

bool is_supported_host() {
    char module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, module_path, sizeof(module_path));
    if (length == 0 || length >= sizeof(module_path)) {
        log_line("refused: unable to identify host executable");
        return false;
    }

    const char* executable = std::strrchr(module_path, '\\');
    executable = executable == nullptr ? module_path : executable + 1;

    if (_stricmp(executable, "StarCraft-SL.BWAPI.exe") != 0 &&
        _stricmp(executable, "Starcraft-BWAPI.exe") != 0) {
        log_line("loaded: unsupported host executable; no memory access attempted");
        return false;
    }

    return true;
}

bool validate_image_state() {
    const auto* speed_modifiers =
        reinterpret_cast<const std::uint32_t*>(kSpeedModifiersAddress);

    for (std::size_t i = 0; i < kSpeedCount; ++i) {
        if (speed_modifiers[i] != kExpectedSpeedModifiers[i]) {
            log_line("refused: unsupported speed modifier table");
            return false;
        }
    }

    const auto speed = *reinterpret_cast<const std::uint32_t*>(kGameSpeedAddress);
    if (speed >= kSpeedCount) {
        log_line("refused: unsupported game speed index");
        return false;
    }

    return true;
}

DWORD WINAPI watcher_thread(void*) {
    log_line("startup: self-memory plugin loaded");

    if (!is_supported_host()) {
        return 0;
    }

    while (g_running.load(std::memory_order_relaxed)) {
        if (validate_image_state()) {
            auto* game_speed = reinterpret_cast<std::uint32_t*>(kGameSpeedAddress);
            auto* latency_frames = reinterpret_cast<std::uint32_t*>(kLatencyFramesAddress);

            const auto previous_speed = *game_speed;
            const auto previous_latency = latency_frames[kFastestSpeedIndex];

            *game_speed = kFastestSpeedIndex;
            latency_frames[kFastestSpeedIndex] = kTargetLatencyFrames;

            if (previous_speed != kFastestSpeedIndex ||
                previous_latency != kTargetLatencyFrames) {
                log_line("applied: speed=Fastest index=6 latency_frames=3");
            }
        }

        Sleep(10);
    }

    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_running.store(true, std::memory_order_relaxed);
        g_thread = CreateThread(nullptr, 0, watcher_thread, nullptr, 0, nullptr);
        if (g_thread == nullptr) {
            g_running.store(false, std::memory_order_relaxed);
            log_line("fatal: unable to start watcher thread");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running.store(false, std::memory_order_relaxed);
        // Do not wait in DllMain; the host may be unloading under the loader lock.
        g_thread = nullptr;
    }

    return TRUE;
}
