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
constexpr std::uintptr_t kCurrentTurnLengthAddress = 0x0051CEA0;
constexpr std::uintptr_t kNetworkLatencyAddress = 0x006556E4;
constexpr std::size_t kSpeedCount = 7;
constexpr std::uint32_t kFastestSpeedIndex = 6;
constexpr std::uint32_t kTargetLatencyFrames = 1;
constexpr bool kCorrectionWritesEnabled = false;
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

void log_tick_line(const char* event) {
    char message[256]{};
    std::snprintf(message, sizeof(message), "%s: tick_ms=%llu", event,
                  static_cast<unsigned long long>(GetTickCount64()));
    log_line(message);
}

bool read_bot_measurement(std::uint32_t& measurement) {
    char value[16]{};
    const DWORD length = GetEnvironmentVariableA(
        "PLUTO_BOT_MEASURED_LATENCY", value, static_cast<DWORD>(sizeof(value)));
    if (length == 0 || length >= sizeof(value)) {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed > UINT_MAX) {
        return false;
    }

    measurement = static_cast<std::uint32_t>(parsed);
    return true;
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

void log_host_executable() {
    char module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, module_path, sizeof(module_path));
    if (length == 0 || length >= sizeof(module_path)) {
        log_line("startup: unable to identify host executable; validating memory signature");
        return;
    }

    const char* executable = std::strrchr(module_path, '\\');
    executable = executable == nullptr ? module_path : executable + 1;
    char message[MAX_PATH + 48]{};
    std::snprintf(message, sizeof(message), "startup: host executable=%s; validating memory signature", executable);
    log_line(message);
}

bool validate_image_state(bool& observation_changed) {
    struct ObservedState {
        std::uint32_t speed;
        std::uint32_t current_turn;
        std::uint32_t scheduler_turn;
        std::uint32_t network_latency;
        std::uint32_t target_turn;
        std::array<std::uint32_t, kSpeedCount> speed_modifiers;
        std::array<std::uint32_t, kSpeedCount> latency_frames;

        bool operator==(const ObservedState& other) const {
            return speed == other.speed && current_turn == other.current_turn &&
                   scheduler_turn == other.scheduler_turn &&
                   network_latency == other.network_latency &&
                   target_turn == other.target_turn &&
                   speed_modifiers == other.speed_modifiers &&
                   latency_frames == other.latency_frames;
        }
    };
    static bool has_last_observation = false;
    static bool reported_waiting_for_target = false;
    static bool reported_waiting_for_timing = false;
    static bool reported_timing_ready = false;
    static ObservedState last_observation{};
    observation_changed = false;

    std::uint32_t speed_modifiers[kSpeedCount]{};
    std::uint32_t latency_frames[kSpeedCount]{};

    for (std::size_t i = 0; i < kSpeedCount; ++i) {
        if (!read_memory(kSpeedModifiersAddress + i * sizeof(std::uint32_t),
                         speed_modifiers[i]) ||
            speed_modifiers[i] != kExpectedSpeedModifiers[i]) {
            log_line("refused: unsupported or inaccessible speed modifier table; no writes attempted");
            return false;
        }
        if (!read_memory(kLatencyFramesAddress + i * sizeof(std::uint32_t),
                         latency_frames[i])) {
            log_line("refused: inaccessible latency frame table; no writes attempted");
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
                     target_latency) || target_latency == 0 || target_latency > 20) {
        if (!reported_waiting_for_target) {
            log_tick_line("waiting: target latency frame value is not initialized");
            reported_waiting_for_target = true;
        }
        return false;
    }

    std::uint32_t network_latency = 0;
    if (!read_memory(kNetworkLatencyAddress, network_latency)) {
        log_line("refused: network latency setting is inaccessible; no writes attempted");
        return false;
    }

    const std::uint32_t current_turn = latency_frames[speed];
    std::uint32_t scheduler_turn = 0;
    if (!read_memory(kCurrentTurnLengthAddress, scheduler_turn)) {
        log_line("refused: current scheduler turn length is inaccessible; no writes attempted");
        return false;
    }
    if (current_turn == 0 || current_turn > 20 || scheduler_turn == 0 ||
        scheduler_turn > 20) {
        if (!reported_waiting_for_timing) {
            log_tick_line("waiting: engine timing state is not initialized; no writes attempted");
            reported_waiting_for_timing = true;
        }
        return false;
    }
    if (!reported_timing_ready) {
        log_tick_line("ready: engine timing state is initialized");
        reported_timing_ready = true;
    }

    ObservedState observation{
        speed,
        current_turn,
        scheduler_turn,
        network_latency,
        target_latency,
        {},
        {},
    };
    std::memcpy(observation.speed_modifiers.data(), speed_modifiers, sizeof(speed_modifiers));
    std::memcpy(observation.latency_frames.data(), latency_frames, sizeof(latency_frames));

    if (has_last_observation && observation == last_observation) {
        return true;
    }
    last_observation = observation;
    has_last_observation = true;
    observation_changed = true;

    std::uint32_t bot_measurement = 0;
    char message[768]{};
    int offset = std::snprintf(
        message, sizeof(message),
        "observed: tick_ms=%llu speed=%u table_turn=%u scheduler_turn=%u "
        "network_latency=%u target_turn=%u "
        "speed_modifiers=",
        static_cast<unsigned long long>(GetTickCount64()), speed, current_turn,
        scheduler_turn, network_latency, target_latency);
    for (std::size_t i = 0; i < kSpeedCount && offset > 0 &&
                              static_cast<std::size_t>(offset) < sizeof(message); ++i) {
        offset += std::snprintf(message + offset, sizeof(message) - offset, "%s%u",
                                i == 0 ? "" : ",", speed_modifiers[i]);
    }
    offset += std::snprintf(message + offset, sizeof(message) - offset, " latency_frames=");
    for (std::size_t i = 0; i < kSpeedCount && offset > 0 &&
                              static_cast<std::size_t>(offset) < sizeof(message); ++i) {
        offset += std::snprintf(message + offset, sizeof(message) - offset, "%s%u",
                                i == 0 ? "" : ",", latency_frames[i]);
    }
    log_line(message);

    if (read_bot_measurement(bot_measurement)) {
        std::snprintf(message, sizeof(message),
                      "calibration: bot_measured_latency=%u engine_turn=%u "
                      "mapping=selected_scheduler_turn target=Fastest/1 trained_latency=4 "
                      "target_satisfied=%s",
                      bot_measurement, current_turn,
                      speed == kFastestSpeedIndex && target_latency == kTargetLatencyFrames &&
                              bot_measurement == 4
                          ? "yes"
                          : "no");
    } else {
        std::snprintf(message, sizeof(message),
                      "calibration: bot_measured_latency=unavailable "
                      "target=Fastest/1 trained_latency=4");
    }
    log_line(message);

    return true;
}

DWORD WINAPI watcher_thread(void*) {
    log_line("startup: self-memory plugin loaded");
    log_host_executable();
    log_line("diagnostic-only: live timing correction disabled pending pre-initialization validation");

    bool timing_state_ready = false;
    while (g_running.load(std::memory_order_relaxed)) {
        bool observation_changed = false;
        if (validate_image_state(observation_changed)) {
            timing_state_ready = true;
            if (!kCorrectionWritesEnabled) {
                Sleep(100);
                continue;
            }
            std::uint32_t previous_speed = 0;
            std::uint32_t previous_latency = 0;
            std::uint32_t previous_scheduler_turn = 0;
            if (!read_memory(kGameSpeedAddress, previous_speed) ||
                !read_memory(kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t),
                             previous_latency) ||
                !read_memory(kCurrentTurnLengthAddress, previous_scheduler_turn)) {
                log_line("refused: unable to read patch values");
                Sleep(1000);
                continue;
            }

            char message[256]{};
            if (observation_changed) {
                std::snprintf(message, sizeof(message),
                              "decision: speed_before=%u latency_before=%u target_speed=6 "
                              "scheduler_before=%u target_latency=1 target_scheduler=1 dry_run=%s",
                              previous_speed, previous_latency, previous_scheduler_turn,
                              dry_run() ? "yes" : "no");
                log_line(message);
            }

            if (!dry_run() &&
                (!write_memory(kGameSpeedAddress, kFastestSpeedIndex) ||
                 !write_memory(kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t),
                               kTargetLatencyFrames) ||
                 !write_memory(kCurrentTurnLengthAddress, kTargetLatencyFrames))) {
                log_line("refused: patch write failed");
                Sleep(1000);
                continue;
            }

            std::uint32_t applied_speed = 0;
            std::uint32_t applied_latency = 0;
            std::uint32_t applied_scheduler_turn = 0;
            if (!read_memory(kGameSpeedAddress, applied_speed) ||
                !read_memory(kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t),
                             applied_latency) ||
                !read_memory(kCurrentTurnLengthAddress, applied_scheduler_turn)) {
                log_line("refused: patch read-back failed");
                Sleep(1000);
                continue;
            }

            if (dry_run()) {
                if (observation_changed) {
                    log_line("dry-run: no memory writes performed");
                }
                 } else if (previous_speed != kFastestSpeedIndex ||
                      previous_latency != kTargetLatencyFrames ||
                      previous_scheduler_turn != kTargetLatencyFrames) {
                std::snprintf(message, sizeof(message),
                              "applied: speed_before=%u speed_after=%u latency_before=%u "
                          "latency_after=%u scheduler_before=%u scheduler_after=%u "
                          "network_latency_unchanged=yes",
                          previous_speed, applied_speed, previous_latency, applied_latency,
                          previous_scheduler_turn, applied_scheduler_turn);
                log_line(message);
                log_tick_line("applied: correction read-back complete");
            }
        }

        Sleep(timing_state_ready ? 100 : 10);
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
