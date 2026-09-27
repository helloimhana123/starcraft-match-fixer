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
constexpr std::uintptr_t kFrameCountAddress = 0x0057F23C;
constexpr std::uintptr_t kDerivationAddress = 0x004D92A0;
constexpr std::size_t kSpeedCount = 7;
constexpr std::size_t kDetourLength = 6;
constexpr std::uint32_t kFastestSpeedIndex = 6;
constexpr std::uint32_t kTargetLatencyFrames = 1;
constexpr std::array<std::uint32_t, kSpeedCount> kExpectedSpeedModifiers{
    167, 111, 83, 67, 56, 48, 42};
constexpr std::array<std::uint8_t, kDetourLength> kDerivationSignature{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24};
// Diagnostic anchors only: these are not approved speed-write sites.
constexpr std::uintptr_t kSelectMapAddress = 0x004A8050;
constexpr std::uintptr_t kCreateDataAddress = 0x004A68D0;
constexpr std::uintptr_t kCreateGameAddress = 0x004D3FC0;
constexpr std::uintptr_t kCreateLadderGameAddress = 0x004D3910;
constexpr std::uintptr_t kSNetCallAddress = 0x004D409E;
constexpr std::uintptr_t kSNetLadderCallAddress = 0x004D3B0B;
constexpr std::array<std::uint8_t, 9> kSelectMapSignature{
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00};
constexpr std::array<std::uint8_t, 6> kCreateDataSignature{
    0x55, 0x8B, 0xEC, 0x53, 0x56, 0x57};
constexpr std::array<std::uint8_t, 9> kCreateGameSignature{
    0x53, 0x56, 0x8B, 0xF0, 0xA1, 0xD0, 0x24, 0x51, 0x00};
constexpr std::array<std::uint8_t, 9> kCreateLadderGameSignature{
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x84, 0x00, 0x00, 0x00};
constexpr std::array<std::uint8_t, 5> kSNetCallSignature{
    0xE8, 0x69, 0xC0, 0xF3, 0xFF};
constexpr std::array<std::uint8_t, 5> kSNetLadderCallSignature{
    0xE8, 0x08, 0xC6, 0xF3, 0xFF};
constexpr std::size_t kTraceCapacity = 2048;
// Verified creation-data field that carries the chosen game speed to room
// advertisement (observed 0 for Slowest and 6 for Fastest on host and peer).
constexpr std::uintptr_t kCreationSpeedOffset = 0x26;

std::atomic<bool> g_running{false};
std::atomic<void*> g_derivation_trampoline{nullptr};
std::atomic<std::uint32_t> g_derivation_override_count{0};
std::atomic<bool> g_derivation_override_failed{false};
HANDLE g_thread = nullptr;
void* g_select_map_trampoline = nullptr;
void* g_create_data_trampoline = nullptr;
void* g_create_game_trampoline = nullptr;
void* g_create_ladder_game_trampoline = nullptr;
std::atomic<std::uint32_t> g_creation_number{0};
std::atomic<std::uintptr_t> g_last_create_data{0};
std::atomic<std::uintptr_t> g_create_data_buffer{0};

struct TraceEvent {
    std::atomic<bool> ready{false};
    const char* stage = nullptr;
    std::uint64_t tick = 0;
    std::uint32_t thread = 0;
    std::uint32_t creation = 0;
    std::uintptr_t caller = 0;
    std::uintptr_t data = 0;
    std::uint32_t argument = UINT_MAX;
    std::uint32_t field26 = UINT_MAX;
    std::uint32_t field27 = UINT_MAX;
    std::uint32_t game_speed = UINT_MAX;
    std::uint32_t selection_byte = UINT_MAX;
    std::uint32_t mode = UINT_MAX;
    std::uint32_t result = UINT_MAX;
};

std::array<TraceEvent, kTraceCapacity> g_trace_events{};
std::atomic<std::uint32_t> g_trace_next{0};
std::uint32_t g_trace_flushed = 0;  // Watcher thread only.
std::atomic<bool> g_creation_trace_enabled{false};

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

void log_tick_line(const char* event, std::uint32_t frame = UINT_MAX) {
    char message[256]{};
    if (frame == UINT_MAX) {
        std::snprintf(message, sizeof(message), "%s: tick_ms=%llu", event,
                      static_cast<unsigned long long>(GetTickCount64()));
    } else {
        std::snprintf(message, sizeof(message), "%s: tick_ms=%llu frame=%u", event,
                      static_cast<unsigned long long>(GetTickCount64()), frame);
    }
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

bool creation_trace_requested() {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA(
        "PLUTO_FASTEST_LATENCY_TRACE_CREATION", value, sizeof(value));
    return length != 0 && length < sizeof(value) &&
           (_stricmp(value, "1") == 0 || _stricmp(value, "true") == 0);
}

// Enabled by default because forcing Fastest is this build's purpose. Set
// PLUTO_FASTEST_LATENCY_FORCE_CREATION_SPEED=0 to keep the hooks diagnostic-only.
bool creation_force_enabled() {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA(
        "PLUTO_FASTEST_LATENCY_FORCE_CREATION_SPEED", value, sizeof(value));
    if (length == 0 || length >= sizeof(value)) {
        return true;
    }
    return !(_stricmp(value, "0") == 0 || _stricmp(value, "false") == 0);
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

void record_creation_trace(const char* stage, std::uintptr_t caller,
                           std::uintptr_t data, std::uint32_t argument = UINT_MAX,
                           std::uint32_t result = UINT_MAX, bool new_creation = false,
                           bool read_fields = true) {
    const auto index = g_trace_next.fetch_add(1, std::memory_order_relaxed);
    if (index >= kTraceCapacity) {
        return;
    }
    auto& event = g_trace_events[index];
    event.stage = stage;
    event.tick = GetTickCount64();
    event.thread = GetCurrentThreadId();
    event.creation = new_creation
        ? g_creation_number.fetch_add(1, std::memory_order_relaxed) + 1
        : g_creation_number.load(std::memory_order_relaxed);
    event.caller = caller;
    event.data = data;
    event.argument = argument;
    event.result = result;
    read_memory(kGameSpeedAddress, event.game_speed);
    std::uint8_t selection_byte = 0;
    if (read_memory(static_cast<std::uintptr_t>(0x0059BB6C), selection_byte)) {
        event.selection_byte = selection_byte;
    }
    std::uint8_t mode = 0;
    if (read_memory(static_cast<std::uintptr_t>(0x0057F0B4), mode)) {
        event.mode = mode;
    }
    if (data != 0 && read_fields) {
        std::uint8_t value = 0;
        if (read_memory(data + 0x26, value)) {
            event.field26 = value;
        }
        if (read_memory(data + 0x27, value)) {
            event.field27 = value;
        }
    }
    event.ready.store(true, std::memory_order_release);
}

void flush_creation_trace() {
    const auto count = g_trace_next.load(std::memory_order_acquire);
    const auto end = count < kTraceCapacity ? count : static_cast<std::uint32_t>(kTraceCapacity);
    while (g_trace_flushed < end &&
           g_trace_events[g_trace_flushed].ready.load(std::memory_order_acquire)) {
        const auto& event = g_trace_events[g_trace_flushed++];
        char message[320]{};
        std::snprintf(message, sizeof(message),
                      "creation-trace: stage=%s tick_ms=%llu pid=%lu tid=%u creation=%u "
                      "caller=%08X data=%08X argument=%u field26=%u field27=%u "
                      "game_speed=%u selection_byte=%u mode=%u result=%u",
                      event.stage, static_cast<unsigned long long>(event.tick),
                      static_cast<unsigned long>(GetCurrentProcessId()), event.thread,
                      event.creation, static_cast<unsigned>(event.caller),
                      static_cast<unsigned>(event.data), event.argument, event.field26,
                      event.field27, event.game_speed, event.selection_byte,
                      event.mode, event.result);
        log_line(message);
    }
}

// pushfd/pushad layout: eax=[7], return address=[9], stack argument n=[10+n].
void __cdecl trace_select_map(const std::uint32_t* saved) {
    if (!g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    record_creation_trace("select-map", saved[9], saved[7], saved[13],
                          UINT_MAX, false, false);
}

// pushad layout: [0]=EDI,[1]=ESI,[2]=EBP,[3]=ESP,[4]=EBX,[5]=EDX,[6]=ECX,[7]=EAX,
// [8]=EFLAGS, [9]=return address, [10]=first stack argument. 0x004A68D0 receives the
// creation buffer in EAX and the chosen speed as its single stack argument.
void __cdecl trace_create_data(const std::uint32_t* saved) {
    g_create_data_buffer.store(saved[7], std::memory_order_relaxed);
    g_last_create_data.store(saved[7], std::memory_order_relaxed);
    if (g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        record_creation_trace("create-data-before", saved[9], saved[7], saved[10]);
    }
}

// Writes Fastest into the verified creation-speed field before the room is
// created. Refuses out-of-range or inaccessible data; no-op when already Fastest.
void apply_creation_speed_override() {
    if (!creation_force_enabled()) {
        return;
    }
    const std::uintptr_t data = g_create_data_buffer.load(std::memory_order_relaxed);
    if (data == 0) {
        record_creation_trace("override-refused-no-buffer", 0, 0);
        return;
    }

    std::uint8_t original = 0;
    if (!read_memory(data + kCreationSpeedOffset, original)) {
        record_creation_trace("override-refused-inaccessible", 0, data);
        return;
    }
    if (original > kFastestSpeedIndex) {
        record_creation_trace("override-refused-range", 0, data, original);
        return;
    }
    if (original == kFastestSpeedIndex) {
        record_creation_trace("override-noop-fastest", 0, data, original,
                              static_cast<std::uint32_t>(original));
        return;
    }
    if (!write_memory<std::uint8_t>(data + kCreationSpeedOffset,
                                    static_cast<std::uint8_t>(kFastestSpeedIndex))) {
        record_creation_trace("override-refused-write", 0, data, original);
        return;
    }
    std::uint8_t readback = 0;
    read_memory(data + kCreationSpeedOffset, readback);
    record_creation_trace("override-applied", 0, data, original, readback);
}

void __cdecl trace_create_data_after(const std::uint32_t* saved) {
    if (g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        record_creation_trace("create-data-after", saved[10], saved[9], saved[11], saved[7]);
    }
    apply_creation_speed_override();
}

void __cdecl trace_create_game(const std::uint32_t* saved) {
    if (!g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    g_last_create_data.store(saved[7], std::memory_order_relaxed);
    record_creation_trace("create-game-before", saved[9], saved[7], UINT_MAX,
                          UINT_MAX, true);
}

void __cdecl trace_create_ladder_game(const std::uint32_t* saved) {
    if (!g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    g_last_create_data.store(saved[10], std::memory_order_relaxed);
    record_creation_trace("create-ladder-before", saved[9], saved[10], UINT_MAX,
                          UINT_MAX, true);
}

__declspec(naked) void select_map_detour() {
    __asm {
        pushfd
        pushad
        push esp
        call trace_select_map
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_select_map_trampoline]
    }
}

__declspec(naked) void create_data_detour() {
    __asm {
        pushfd
        pushad
        push esp
        call trace_create_data
        add esp, 4
        popad
        popfd
        push eax
        push dword ptr [esp + 8]
        call dword ptr [g_create_data_trampoline]
        pushfd
        pushad
        push esp
        call trace_create_data_after
        add esp, 4
        popad
        popfd
        add esp, 4
        ret 4
    }
}

__declspec(naked) void create_game_detour() {
    __asm {
        pushfd
        pushad
        push esp
        call trace_create_game
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_create_game_trampoline]
    }
}

__declspec(naked) void create_ladder_game_detour() {
    __asm {
        pushfd
        pushad
        push esp
        call trace_create_ladder_game
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_create_ladder_game_trampoline]
    }
}

// The call-site is the only SNetCreateGame call on the traced CreateGame path.
using SNetCreateGameFunction = BOOL(__stdcall*)(const char*, const char*, const char*,
    DWORD, char*, int, int, char*, char*, int*);

BOOL __stdcall snet_create_game_trace(const char* name, const char* password,
                                      const char* stat, DWORD type, char* templ,
                                      int templ_size, int players, char* creator,
                                      char* extra, int* player_id) {
    const auto original = reinterpret_cast<SNetCreateGameFunction>(0x0041010C);
    if (!g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        return original(name, password, stat, type, templ, templ_size, players,
                        creator, extra, player_id);
    }
    const auto data = g_last_create_data.load(std::memory_order_relaxed);
    record_creation_trace("snet-before", kSNetCallAddress, data, type);
    const BOOL result = original(name, password, stat, type, templ, templ_size,
                                 players, creator, extra, player_id);
    record_creation_trace("snet-after", kSNetCallAddress, data, type,
                          static_cast<std::uint32_t>(result));
    return result;
}

using SNetCreateLadderGameFunction = BOOL(__stdcall*)(const char*, const char*,
    const char*, DWORD, DWORD, DWORD, char*, int, int, char*, char*, int*);

BOOL __stdcall snet_create_ladder_game_trace(const char* name, const char* password,
                                              const char* stat, DWORD type, DWORD ladder_type,
                                              DWORD mode_flags, char* templ, int templ_size,
                                              int players, char* creator, char* extra,
                                              int* player_id) {
    const auto original = reinterpret_cast<SNetCreateLadderGameFunction>(0x00410118);
    if (!g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        return original(name, password, stat, type, ladder_type, mode_flags, templ,
                        templ_size, players, creator, extra, player_id);
    }
    const auto data = g_last_create_data.load(std::memory_order_relaxed);
    record_creation_trace("snet-ladder-before", kSNetLadderCallAddress, data, type);
    const BOOL result = original(name, password, stat, type, ladder_type, mode_flags,
                                 templ, templ_size, players, creator, extra, player_id);
    record_creation_trace("snet-ladder-after", kSNetLadderCallAddress, data, type,
                          static_cast<std::uint32_t>(result));
    return result;
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
    log_line("validated: derivation entry signature=55 8B EC 83 EC 24 overwrite_length=6");
    return true;
}

using DerivationFunction = void(__cdecl*)();

void __cdecl derivation_detour() {
    const auto original = reinterpret_cast<DerivationFunction>(
        g_derivation_trampoline.load(std::memory_order_acquire));
    if (original != nullptr) {
        original();
        __try {
            *reinterpret_cast<volatile std::uint32_t*>(
                kLatencyFramesAddress + kFastestSpeedIndex * sizeof(std::uint32_t)) =
                kTargetLatencyFrames;
            g_derivation_override_count.fetch_add(1, std::memory_order_relaxed);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_derivation_override_failed.store(true, std::memory_order_relaxed);
        }
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

template <std::size_t N>
bool validate_trace_signature(std::uintptr_t address,
                              const std::array<std::uint8_t, N>& signature,
                              const char* label) {
    std::array<std::uint8_t, N> actual{};
    if (!read_memory(address, actual) || actual != signature) {
        char message[160]{};
        std::snprintf(message, sizeof(message),
                      "creation-hook refused: %s signature mismatch or inaccessible", label);
        log_line(message);
        return false;
    }
    return true;
}

template <std::size_t N>
bool install_trace_entry(std::uintptr_t address, const std::array<std::uint8_t, N>& signature,
                         const void* detour, void*& trampoline, const char* label) {
    auto* const entry = reinterpret_cast<std::uint8_t*>(address);
    auto* const stub = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, N + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (stub == nullptr) {
        log_line("creation-hook refused: trampoline allocation failed");
        return false;
    }
    std::memcpy(stub, signature.data(), N);  // Whole-instruction signature, no relative instructions.
    if (!write_relative_jump(stub + N, entry + N)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        log_line("creation-hook refused: trampoline jump out of range");
        return false;
    }
    trampoline = stub;
    DWORD protection = 0;
    if (!VirtualProtect(entry, N, PAGE_EXECUTE_READWRITE, &protection)) {
        trampoline = nullptr;
        VirtualFree(stub, 0, MEM_RELEASE);
        log_line("creation-hook refused: entry protection change failed");
        return false;
    }
    const bool written = write_relative_jump(entry, detour);
    if (written) {
        std::memset(entry + 5, 0x90, N - 5);
        FlushInstructionCache(GetCurrentProcess(), entry, N);
    } else {
        std::memcpy(entry, signature.data(), N);
    }
    DWORD ignored = 0;
    VirtualProtect(entry, N, protection, &ignored);
    if (!written) {
        trampoline = nullptr;
        VirtualFree(stub, 0, MEM_RELEASE);
        log_line("creation-hook refused: entry jump out of range");
        return false;
    }
    char message[128]{};
    std::snprintf(message, sizeof(message), "creation-hook installed: %s", label);
    log_line(message);
    return true;
}

bool install_trace_call(std::uintptr_t address,
                        const std::array<std::uint8_t, 5>& signature,
                        const void* detour, const char* label) {
    auto* const call = reinterpret_cast<std::uint8_t*>(address);
    DWORD protection = 0;
    if (!VirtualProtect(call, signature.size(), PAGE_EXECUTE_READWRITE, &protection)) {
        log_line("creation-hook refused: call-site protection change failed");
        return false;
    }
    // A CALL preserves the return address and the original stdcall arguments.
    const bool written = write_relative_jump(call, detour, 0xE8);
    if (written) {
        FlushInstructionCache(GetCurrentProcess(), call, signature.size());
    } else {
        std::memcpy(call, signature.data(), signature.size());
    }
    DWORD ignored = 0;
    VirtualProtect(call, signature.size(), protection, &ignored);
    if (!written) {
        log_line("creation-hook refused: call-site jump out of range");
        return false;
    }
    char message[128]{};
    std::snprintf(message, sizeof(message), "creation-hook installed: %s", label);
    log_line(message);
    return true;
}

bool install_creation_hooks() {
    if (!validate_trace_signature(kSelectMapAddress, kSelectMapSignature, "select-map") ||
        !validate_trace_signature(kCreateDataAddress, kCreateDataSignature, "create-data") ||
        !validate_trace_signature(kCreateGameAddress, kCreateGameSignature, "create-game") ||
        !validate_trace_signature(kCreateLadderGameAddress, kCreateLadderGameSignature,
                                  "create-ladder") ||
        !validate_trace_signature(kSNetCallAddress, kSNetCallSignature, "snet-call") ||
        !validate_trace_signature(kSNetLadderCallAddress, kSNetLadderCallSignature,
                                  "snet-ladder-call")) {
        return false;
    }
    if (!install_trace_entry(kSelectMapAddress, kSelectMapSignature, &select_map_detour,
                             g_select_map_trampoline, "select-map") ||
        !install_trace_entry(kCreateDataAddress, kCreateDataSignature, &create_data_detour,
                             g_create_data_trampoline, "create-data") ||
        !install_trace_entry(kCreateGameAddress, kCreateGameSignature, &create_game_detour,
                             g_create_game_trampoline, "create-game") ||
        !install_trace_entry(kCreateLadderGameAddress, kCreateLadderGameSignature,
                             &create_ladder_game_detour, g_create_ladder_game_trampoline,
                             "create-ladder")) {
        log_line("creation-hook partial installation: override not fully armed");
        return false;
    }

    if (!install_trace_call(kSNetCallAddress, kSNetCallSignature,
                            &snet_create_game_trace, "snet-call") ||
        !install_trace_call(kSNetLadderCallAddress, kSNetLadderCallSignature,
                            &snet_create_ladder_game_trace, "snet-ladder-call")) {
        log_line("creation-hook partial installation: override not fully armed");
        return false;
    }
    log_line("creation-speed: Fastest creation override armed; no-op when already Fastest");
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

    log_line("installed: process-lifetime derivation detour trampoline=active");
    return true;
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
    static bool reported_invalid_speed_modifiers = false;
    static ObservedState last_observation{};
    observation_changed = false;

    std::uint32_t speed_modifiers[kSpeedCount]{};
    std::uint32_t latency_frames[kSpeedCount]{};

    for (std::size_t i = 0; i < kSpeedCount; ++i) {
        if (!read_memory(kSpeedModifiersAddress + i * sizeof(std::uint32_t),
                          speed_modifiers[i]) ||
            speed_modifiers[i] != kExpectedSpeedModifiers[i]) {
            if (!reported_invalid_speed_modifiers) {
                log_line("refused: unsupported or inaccessible speed modifier table; no writes attempted");
                reported_invalid_speed_modifiers = true;
            }
            return false;
        }
        if (!read_memory(kLatencyFramesAddress + i * sizeof(std::uint32_t),
                         latency_frames[i])) {
            log_line("refused: inaccessible latency frame table; no writes attempted");
            return false;
        }
    }
    reported_invalid_speed_modifiers = false;

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

    std::uint32_t frame = 0;
    if (!read_memory(kFrameCountAddress, frame)) {
        log_line("refused: game frame counter is inaccessible; no writes attempted");
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
        log_tick_line("ready: engine timing state is initialized", frame);
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
        "observed: tick_ms=%llu frame=%u speed=%u table_turn=%u scheduler_turn=%u "
        "network_latency=%u target_turn=%u "
        "speed_modifiers=",
        static_cast<unsigned long long>(GetTickCount64()), frame, speed, current_turn,
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

    const std::uint32_t override_count =
        g_derivation_override_count.load(std::memory_order_relaxed);
    if (g_derivation_override_failed.load(std::memory_order_relaxed)) {
        log_line("refused: derivation-time Fastest table override failed");
    } else if (override_count != 0) {
        std::snprintf(message, sizeof(message),
                      "derivation: Fastest table override count=%u target_turn=1 "
                      "game_speed_unchanged=yes scheduler_untouched=yes "
                      "network_latency_unchanged=yes",
                      override_count);
        log_line(message);
    }

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
    if (!install_derivation_detour()) {
        return 0;
    }
    log_line("installed: derivation-time Fastest table override enabled; scheduler untouched");

    g_creation_trace_enabled.store(creation_trace_requested(), std::memory_order_relaxed);
    if (creation_force_enabled() || g_creation_trace_enabled.load(std::memory_order_relaxed)) {
        if (!install_creation_hooks()) {
            log_line("creation-speed: pre-lobby override unavailable; rooms will keep the selected speed");
        }
    }

    bool timing_state_ready = false;
    while (g_running.load(std::memory_order_relaxed)) {
        flush_creation_trace();
        bool observation_changed = false;
        if (validate_image_state(observation_changed)) {
            timing_state_ready = true;
            if (observation_changed) {
                log_tick_line("diagnostic-only: initialized timing state observed; no writes performed");
            }
        }

        Sleep(timing_state_ready ? 100 : 10);
    }

    flush_creation_trace();

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
