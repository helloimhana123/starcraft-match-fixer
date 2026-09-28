#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr std::uintptr_t kLatencyFramesAddress = 0x0051CE70;
constexpr std::uintptr_t kDerivationAddress = 0x004D92A0;
constexpr std::uintptr_t kCreateDataAddress = 0x004A68D0;
constexpr std::size_t kSpeedCount = 7;
constexpr std::size_t kDetourLength = 6;
constexpr std::uint32_t kMaxGameSpeed = 6;
constexpr std::uint32_t kMinLatencyFrames = 1;
constexpr std::uint32_t kMaxLatencyFrames = 20;
// A configured value of -1 disables that step explicitly and silently.
constexpr std::uint32_t kDisabledValue = 0xFFFFFFFFu;
constexpr char kConfigSection[] = "MatchFixer";
constexpr char kConfigFileName[] = "MatchFixer.ini";
constexpr std::array<std::uint8_t, kDetourLength> kDerivationSignature{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24};
constexpr std::array<std::uint8_t, 6> kCreateDataSignature{
    0x55, 0x8B, 0xEC, 0x53, 0x56, 0x57};
// Verified creation-data field that carries the chosen game speed to room
// advertisement (observed 0 for Slowest and 6 for Fastest on host and peer).
constexpr std::uintptr_t kCreationSpeedOffset = 0x26;

HANDLE g_thread = nullptr;
std::atomic<void*> g_derivation_trampoline{nullptr};
void* g_create_data_trampoline = nullptr;
std::atomic<std::uintptr_t> g_create_data_buffer{0};
std::atomic<std::uint32_t> g_game_speed{0};
std::atomic<std::uint32_t> g_latency_frames{0};

std::FILE* open_log() {
    std::FILE* log = nullptr;
    if (fopen_s(&log, "MatchFixer.log", "a") != 0) {
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
bool write_memory(std::uintptr_t address, const T& value) {
    __try {
        *reinterpret_cast<volatile T*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Reads an integer key from MatchFixer.ini with the Win32 profile API, which
// ignores ';' comment lines. Returns the sentinel when the key is absent or its
// value is not a complete integer, so the caller can detect and refuse it.
std::uint32_t read_ini_int(const char* key, const char* path, std::uint32_t sentinel) {
    char buffer[32]{};
    if (GetPrivateProfileStringA(kConfigSection, key, "", buffer,
                                 static_cast<DWORD>(sizeof(buffer)), path) == 0) {
        return sentinel;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(buffer, &end, 10);
    if (end == buffer || *end != '\0' || parsed > 0xFFFFFFFFul) {
        return sentinel;
    }
    return static_cast<std::uint32_t>(parsed);
}

// Writes the configured speed into the verified creation-speed field before the
// room is created. Refuses out-of-range or inaccessible data; a silent no-op
// when the field already holds the configured value.
void apply_creation_speed_override() {
    const std::uintptr_t data = g_create_data_buffer.load(std::memory_order_relaxed);
    if (data == 0) {
        log_line("refused: pre-lobby speed override has no creation buffer");
        return;
    }

    std::uint8_t original = 0;
    if (!read_memory(data + kCreationSpeedOffset, original)) {
        log_line("refused: pre-lobby speed field is inaccessible");
        return;
    }
    if (static_cast<std::uint32_t>(original) > kMaxGameSpeed) {
        log_line("refused: pre-lobby speed field is out of range");
        return;
    }

    const std::uint32_t speed = g_game_speed.load(std::memory_order_relaxed);
    if (static_cast<std::uint32_t>(original) == speed) {
        return;
    }
    if (!write_memory<std::uint8_t>(data + kCreationSpeedOffset,
                                    static_cast<std::uint8_t>(speed))) {
        log_line("refused: pre-lobby speed write failed");
        return;
    }
}

// pushad layout: EAX is the creation buffer, at offset [7].
void __cdecl capture_create_data(const std::uint32_t* saved) {
    g_create_data_buffer.store(saved[7], std::memory_order_relaxed);
}

// 0x004A68D0 receives the creation buffer in EAX and the chosen speed as its
// single stack argument. The detour captures EAX, calls the original through
// the trampoline, then applies the configured speed override.
__declspec(naked) void create_data_detour() {
    __asm {
        pushfd
        pushad
        push esp
        call capture_create_data
        add esp, 4
        popad
        popfd
        push eax
        push dword ptr [esp + 8]
        call dword ptr [g_create_data_trampoline]
        pushfd
        pushad
        call apply_creation_speed_override
        popad
        popfd
        add esp, 4
        ret 4
    }
}

bool validate_create_data_signature() {
    std::array<std::uint8_t, kCreateDataSignature.size()> actual{};
    if (!read_memory(kCreateDataAddress, actual) || actual != kCreateDataSignature) {
        log_line("refused: create-data entry signature mismatch or inaccessible");
        return false;
    }
    return true;
}

bool validate_derivation_signature() {
    std::array<std::uint8_t, kDetourLength> actual{};
    if (!read_memory(kDerivationAddress, actual)) {
        log_line("refused: derivation entry is inaccessible; detour not installed");
        return false;
    }
    if (actual != kDerivationSignature) {
        log_line("refused: derivation entry signature mismatch; detour not installed");
        return false;
    }
    return true;
}

using DerivationFunction = void(__cdecl*)();

// After the original derivation runs, writes the configured value to every
// entry of the per-speed latency-frame table.
void __cdecl derivation_detour() {
    const auto original = reinterpret_cast<DerivationFunction>(
        g_derivation_trampoline.load(std::memory_order_acquire));
    if (original == nullptr) {
        return;
    }
    original();

    const std::uint32_t frames = g_latency_frames.load(std::memory_order_relaxed);
    volatile bool write_failed = false;
    __try {
        for (std::size_t i = 0; i < kSpeedCount; ++i) {
            *reinterpret_cast<volatile std::uint32_t*>(
                kLatencyFramesAddress + i * sizeof(std::uint32_t)) = frames;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        write_failed = true;
    }
    if (write_failed) {
        log_line("refused: latency-frame table write failed");
    }
}

bool write_relative_jump(std::uint8_t* source, const void* destination,
                         std::uint8_t opcode = 0xE9) {
    const std::intptr_t displacement =
        reinterpret_cast<const std::uint8_t*>(destination) - (source + 5);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        return false;
    }

    source[0] = opcode;
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(source + 1, &relative, sizeof(relative));
    return true;
}

bool install_create_data_hook() {
    if (!validate_create_data_signature()) {
        return false;
    }

    auto* const entry = reinterpret_cast<std::uint8_t*>(kCreateDataAddress);
    auto* const stub = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kCreateDataSignature.size() + 5, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (stub == nullptr) {
        log_line("refused: create-data trampoline allocation failed");
        return false;
    }
    std::memcpy(stub, kCreateDataSignature.data(), kCreateDataSignature.size());
    if (!write_relative_jump(stub + kCreateDataSignature.size(),
                             entry + kCreateDataSignature.size())) {
        VirtualFree(stub, 0, MEM_RELEASE);
        log_line("refused: create-data trampoline is out of jump range");
        return false;
    }
    g_create_data_trampoline = stub;

    DWORD protection = 0;
    if (!VirtualProtect(entry, kCreateDataSignature.size(), PAGE_EXECUTE_READWRITE,
                        &protection)) {
        g_create_data_trampoline = nullptr;
        VirtualFree(stub, 0, MEM_RELEASE);
        log_line("refused: create-data entry protection change failed");
        return false;
    }
    const bool written = write_relative_jump(entry, &create_data_detour);
    if (written) {
        std::memset(entry + 5, 0x90, kCreateDataSignature.size() - 5);
        FlushInstructionCache(GetCurrentProcess(), entry, kCreateDataSignature.size());
    } else {
        std::memcpy(entry, kCreateDataSignature.data(), kCreateDataSignature.size());
    }
    DWORD ignored = 0;
    VirtualProtect(entry, kCreateDataSignature.size(), protection, &ignored);
    if (!written) {
        g_create_data_trampoline = nullptr;
        VirtualFree(stub, 0, MEM_RELEASE);
        log_line("refused: create-data entry jump out of range");
        return false;
    }
    return true;
}

bool install_derivation_detour() {
    if (g_derivation_trampoline.load(std::memory_order_acquire) != nullptr) {
        return true;
    }
    if (!validate_derivation_signature()) {
        return false;
    }

    auto* const entry = reinterpret_cast<std::uint8_t*>(kDerivationAddress);
    auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kDetourLength + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        log_line("refused: unable to allocate derivation trampoline; detour not installed");
        return false;
    }

    std::memcpy(trampoline, entry, kDetourLength);
    if (!write_relative_jump(trampoline + kDetourLength,
                             reinterpret_cast<const void*>(kDerivationAddress + kDetourLength))) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        log_line("refused: derivation trampoline is out of jump range; detour not installed");
        return false;
    }

    g_derivation_trampoline.store(trampoline, std::memory_order_release);

    DWORD original_protection = 0;
    if (!VirtualProtect(entry, kDetourLength, PAGE_EXECUTE_READWRITE, &original_protection)) {
        g_derivation_trampoline.store(nullptr, std::memory_order_release);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        log_line("refused: unable to change derivation entry protection; detour not installed");
        return false;
    }

    std::array<std::uint8_t, kDetourLength> original_bytes{};
    std::memcpy(original_bytes.data(), entry, original_bytes.size());
    const bool jump_written = write_relative_jump(entry, reinterpret_cast<const void*>(&derivation_detour));
    if (jump_written) {
        entry[5] = 0x90;
        FlushInstructionCache(GetCurrentProcess(), entry, kDetourLength);
    } else {
        std::memcpy(entry, original_bytes.data(), original_bytes.size());
    }

    DWORD ignored_protection = 0;
    VirtualProtect(entry, kDetourLength, original_protection, &ignored_protection);
    if (!jump_written) {
        g_derivation_trampoline.store(nullptr, std::memory_order_release);
        VirtualFree(trampoline, 0, MEM_RELEASE);
        log_line("refused: derivation detour is out of jump range; detour not installed");
        return false;
    }
    return true;
}

DWORD WINAPI worker_thread(void*) {
    char config_path[MAX_PATH]{};
    const DWORD path_length = GetFullPathNameA(kConfigFileName,
                                               static_cast<DWORD>(sizeof(config_path)),
                                               config_path, nullptr);
    if (path_length == 0 || path_length >= sizeof(config_path)) {
        log_line("refused: unable to resolve MatchFixer.ini path; no fix applied");
        return 0;
    }

    if (GetFileAttributesA(config_path) == INVALID_FILE_ATTRIBUTES) {
        log_line("refused: MatchFixer.ini not found; no fix applied");
        return 0;
    }

    const std::uint32_t game_speed = read_ini_int("GameSpeed", config_path, kMaxGameSpeed + 1);
    if (game_speed == kDisabledValue) {
        // Explicitly disabled; stay silent.
    } else if (game_speed <= kMaxGameSpeed) {
        g_game_speed.store(game_speed, std::memory_order_relaxed);
        if (!install_create_data_hook()) {
            log_line("refused: pre-lobby speed override unavailable");
        }
    } else {
        log_line("refused: GameSpeed is missing or outside 0..6; pre-lobby speed override disabled");
    }

    const std::uint32_t latency_frames =
        read_ini_int("LatencyFrames", config_path, kMaxLatencyFrames + 1);
    if (latency_frames == kDisabledValue) {
        // Explicitly disabled; stay silent.
    } else if (latency_frames >= kMinLatencyFrames && latency_frames <= kMaxLatencyFrames) {
        g_latency_frames.store(latency_frames, std::memory_order_relaxed);
        if (!install_derivation_detour()) {
            log_line("refused: latency-frame correction unavailable");
        }
    } else {
        log_line("refused: LatencyFrames is missing or outside 1..20; latency-frame correction disabled");
    }

    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
        if (g_thread == nullptr) {
            log_line("fatal: unable to start worker thread");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Do not wait in DllMain; the host may be unloading under the loader lock.
        g_thread = nullptr;
    }

    return TRUE;
}
