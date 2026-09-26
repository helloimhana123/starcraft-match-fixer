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
constexpr std::uintptr_t kNetworkLatencyAddress = 0x006556E4;
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

bool dry_run() {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA(
        "PLUTO_FASTEST_LATENCY_DRY_RUN", value, static_cast<DWORD>(sizeof(value)));
    return length != 0 && (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
}

template <typename T>
bool read_memory(std::uintptr_t address, T& value) {
    __try {
        value = *reinterpret_cast<const T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool write_memory(std::uintptr_t address, T value) {
    __try {
        *reinterpret_cast<T*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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
        _stricmp(executable, "StarCraft-SL.pluto.exe") != 0 &&
        _stricmp(executable, "Starcraft-BWAPI.exe") != 0) {
        log_line("loaded: unsupported host executable; no memory access attempted");
        return false;
    }

    return true;
}

bool validate_image_state() {
    std::uint32_t speed_modifiers[kSpeedCount]{};

    for (std::size_t i = 0; i < kSpeedCount; ++i) {
        if (!read_memory(kSpeedModifiersAddress + i * sizeof(std::uint32_t),
                         speed_modifiers[i]) ||
            speed_modifiers[i] != kExpectedSpeedModifiers[i]) {
            log_line("refused: unsupported or inaccessible speed modifier table");
            return false;
        }
    }

    std::uint32_t speed = 0;
    if (!read_memory(kGameSpeedAddress, speed)) {
        log_line("refused: game speed address is inaccessible");
        return false;
    }
    if (speed >= kSpeedCount) {
        log_line("refused: unsupported game speed index");
        return false;
    }

    std::uint32_t target_latency = 0;
    if (!read_memory(kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t),
                     target_latency) || target_latency > 20) {
        log_line("refused: observed target latency frame value is invalid");
        return false;
    }

    char message[256]{};
    std::snprintf(message, sizeof(message),
                  "observed: speed=%u target_latency=%u network_latency_address=0x%08X",
                  speed, target_latency,
                  static_cast<unsigned>(kNetworkLatencyAddress));
    log_line(message);

    return true;
}

DWORD WINAPI watcher_thread(void*) {
    log_line("startup: self-memory plugin loaded");

    if (!is_supported_host()) {
        return 0;
    }

    while (g_running.load(std::memory_order_relaxed)) {
        if (validate_image_state()) {
            std::uint32_t previous_speed = 0;
            std::uint32_t previous_latency = 0;
            if (!read_memory(kGameSpeedAddress, previous_speed) ||
                !read_memory(kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t),
                             previous_latency)) {
                log_line("refused: unable to read patch values");
                Sleep(1000);
                continue;
            }

            char message[256]{};
            std::snprintf(message, sizeof(message),
                          "decision: speed_before=%u latency_before=%u target_speed=6 "
                          "target_latency=3 dry_run=%s",
                          previous_speed, previous_latency, dry_run() ? "yes" : "no");
            log_line(message);

            if (!dry_run() &&
                (!write_memory(kGameSpeedAddress, kFastestSpeedIndex) ||
                 !write_memory(kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t),
                               kTargetLatencyFrames))) {
                log_line("refused: patch write failed");
                Sleep(1000);
                continue;
            }

            std::uint32_t applied_speed = 0;
            std::uint32_t applied_latency = 0;
            if (!read_memory(kGameSpeedAddress, applied_speed) ||
                !read_memory(kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t),
                             applied_latency)) {
                log_line("refused: patch read-back failed");
                Sleep(1000);
                continue;
            }

            if (dry_run()) {
                log_line("dry-run: no memory writes performed");
            } else if (previous_speed != kFastestSpeedIndex ||
                       previous_latency != kTargetLatencyFrames) {
                std::snprintf(message, sizeof(message),
                              "applied: speed_before=%u speed_after=%u latency_before=%u "
                              "latency_after=%u network_latency_unchanged=yes",
                              previous_speed, applied_speed, previous_latency, applied_latency);
                log_line(message);
            }
        }

        Sleep(100);
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
