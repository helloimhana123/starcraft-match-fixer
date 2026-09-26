#include <windows.h>
#include <psapi.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr std::uintptr_t kGameSpeed = 0x006CDFD4;
constexpr std::uintptr_t kSpeedModifiers = 0x005124D8;
constexpr std::uintptr_t kLatencyFrames = 0x0051CE70;
constexpr std::uintptr_t kNetworkLatency = 0x006556E4;
constexpr std::size_t kSpeedCount = 7;
constexpr std::array<std::uint32_t, kSpeedCount> kExpected{167, 111, 83, 67, 56, 48, 42};

bool read(HANDLE process, std::uintptr_t address, void* value, std::size_t size) {
    SIZE_T read_bytes = 0;
    return ReadProcessMemory(process, reinterpret_cast<const void*>(address), value, size,
                              &read_bytes) != FALSE && read_bytes == size;
}

DWORD find_process(const char* requested) {
    if (requested != nullptr) {
        return std::strtoul(requested, nullptr, 10);
    }
    const char* names[] = {"StarCraft.exe", "Starcraft-BWAPI.exe", "StarCraft-SL.BWAPI.exe",
                           "StarCraft-SL.pluto.exe"};
    for (const char* name : names) {
        DWORD processes[1024]{};
        DWORD bytes = 0;
        if (!EnumProcesses(processes, sizeof(processes), &bytes)) continue;
        for (DWORD i = 0; i < bytes / sizeof(DWORD); ++i) {
            HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processes[i]);
            if (process == nullptr) continue;
            char path[MAX_PATH]{};
            DWORD length = sizeof(path);
            const bool match = QueryFullProcessImageNameA(process, 0, path, &length) != FALSE &&
                _stricmp(std::strrchr(path, '\\') ? std::strrchr(path, '\\') + 1 : path, name) == 0;
            CloseHandle(process);
            if (match) return processes[i];
        }
    }
    return 0;
}

void usage() { std::puts("Usage: PlutoLatencyInspector [PID]"); }
}  // namespace

int main(int argc, char** argv) {
    if (argc > 2) { usage(); return 2; }
    const DWORD pid = find_process(argc == 2 ? argv[1] : nullptr);
    if (pid == 0) { std::puts("error: no supported StarCraft client found"); return 1; }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == nullptr) {
        std::printf("error: cannot read PID %lu (run at the game's integrity level, usually elevated)\n", pid);
        return 1;
    }

    std::uint32_t speed = 0, network_latency = 0;
    std::array<std::uint32_t, kSpeedCount> modifiers{}, turns{};
    const bool ok = read(process, kGameSpeed, &speed, sizeof(speed)) &&
        read(process, kSpeedModifiers, modifiers.data(), sizeof(modifiers)) &&
        read(process, kLatencyFrames, turns.data(), sizeof(turns)) &&
        read(process, kNetworkLatency, &network_latency, sizeof(network_latency));
    CloseHandle(process);
    if (!ok) {
        std::puts("error: client memory is inaccessible; retry elevated, without attempting changes");
        return 1;
    }

    const bool supported = modifiers == kExpected;
    std::printf("PID: %lu\nBuild/address layout: %s (validated StarCraft 1.16.1)\n",
                pid, supported ? "supported" : "unsupported");
    std::printf("Game speed index: %u\nMillisecond table:", speed);
    for (auto value : modifiers) std::printf(" %u", value);
    std::printf("\nTurn-length table:");
    for (auto value : turns) std::printf(" %u", value);
    std::printf("\nNetwork latency setting: %u\n", network_latency);
    if (speed < kSpeedCount) {
        std::printf("Current turn length: %u\n", turns[speed]);
        std::printf("Calibration: Fastest/3 produces trained pluto latency 4; current target %s\n",
                    speed == 6 && turns[6] == 3 ? "satisfied" : "not satisfied");
    }
    std::puts("Bot measured latency: unavailable (provide the bot session log for correlation)");
    return supported ? 0 : 1;
}