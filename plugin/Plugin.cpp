// Plugin.cpp — OGSA-TS telemetry for UEVR (praydog's Universal UE VR mod).
//
// Publishes the LOCAL PAWN's world pose (the object the player controls —
// not the HMD/camera: the camera is engine-smoothed and rig-following it
// would feed platform motion back into telemetry; see the OGSA knowledge
// base, projects/uevr, ruling C-20261010-MBP-08) as OGSA-TS motion
// telemetry over UDP multicast. Zero game modification; LAN only.
//
// Install: drop the DLL next to this file's build output into
//   <uevr>/plugins/   (per-game: <game>/uevr/plugins/)
// Config: ogsa_telemetry.ini beside the DLL (optional):
//   group=239.255.17.17  port=40123  ttl=1  motionHz=60
//   objectName=player    objectType=humanoid|vehicle|aircraft|watercraft|spacecraft
//   dynamics=on|off (default on: motion frames carry the bodyDynamics
//                    variant — specific force + angular rates derived
//                    from the pawn pose and the engine-measured velocity)
//   pause=on|off    (default on: report the game's pause state on
//                    session.paused, standard CONVENTIONS C-11)
//   gameName=<exe name by default, pack/build tokens stripped ("Game-Win64-Shipping"
//             -> "Game")>   engine=auto|ue4|ue5 (FVector ABI)
//
// The FVector/FRotator ABI is auto-detected at runtime (default) by
// measuring K2_GetActorLocation's UFunction params size: 12 bytes =
// UE4 floats, 24 = UE5 LWC doubles. engine=ue4|ue5 forces it. A wrong
// ABI silently corrupts pose: UEVR writes the native return into the
// caller's buffer, so UE4 floats read as UE5 doubles produce 0,0,1e30
// garbage (first observed in-game on Stray/UE4, 2026-10-12).
//
// UEVR plugin API vendored under vendor/uevr (MIT, praydog, pinned
// @4ee5c6b); serializer vendored from ogsa-ts-unreal (corpus-locked).
#include <winsock2.h>
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <string>

#include "uevr/API.h"
#include "ogsa_publisher.h"

static const UEVR_PluginInitializeParam* g_api = nullptr;
static ogsa::Publisher g_publisher;
// FVector/FRotator ABI: UE5 = doubles (LWC, 24 bytes), UE4 = floats (12).
// -1 = auto-detect pending (default), 1 = UE4, 0 = UE5. INI forces via engine=.
static int g_engineIsUE4 = -1;
static bool g_detectPause = true; // INI pause=off disables (C-11)
static HMODULE g_module = nullptr;

// ---- UE math (transcribed from FRotator::Quaternion(), UnrealMath) ----
// FRotator: Pitch about Y, Yaw about Z, Roll about X; degrees; UE frame.
static ogsa::telemetry::Quat RotatorToQuat(double pitchDeg, double yawDeg,
                                           double rollDeg)
{
    const double half = 3.14159265358979323846 / 180.0 / 2.0;
    const double sp = std::sin(pitchDeg * half), cp = std::cos(pitchDeg * half);
    const double sy = std::sin(yawDeg * half),   cy = std::cos(yawDeg * half);
    const double sr = std::sin(rollDeg * half),  cr = std::cos(rollDeg * half);
    return {
        (float)(cr * sp * sy - sr * cp * cy),  // x
        (float)(-cr * sp * cy - sr * cp * sy), // y
        (float)(cr * cp * sy - sr * sp * cy),  // z
        (float)(cr * cp * cy + sr * sp * sy),  // w
    };
}

// ---- pawn sampling ----
struct FVectorUE5 { double X, Y, Z; };
struct FRotatorUE5 { double Pitch, Yaw, Roll; };
struct FVectorUE4 { float X, Y, Z; };
struct FRotatorUE4 { float Pitch, Yaw, Roll; };

// K2_GetActorLocation has no parameters and returns FVector, so the
// UFunction's properties size IS the FVector size: 12 bytes = UE4
// floats, 24 = UE5 LWC doubles. Measuring beats trusting a config
// guess — UEVR writes only that many bytes into our buffer, and a
// mismatch reads as denormals/garbage doubles, not zeros.
static void DetectEngineAbi(const UEVR_SDKData* sdk, UEVR_UObjectHandle pawn)
{
    UEVR_UStructHandle fn = nullptr;
    if (UEVR_UStructHandle cls =
            (UEVR_UStructHandle)sdk->uobject->get_class(pawn))
        fn = (UEVR_UStructHandle)sdk->ustruct->find_function(
            cls, L"K2_GetActorLocation");
    // handles are opaque strong typedefs; UFunction IS a UStruct
    const int size = fn ? sdk->ustruct->get_properties_size(fn) : 0;
    if (size <= 0) return; // not resolvable yet — retry next tick
    g_engineIsUE4 = (size < 24) ? 1 : 0;
    g_api->functions->log_info("[ogsa] FVector ABI auto-detected: params "
                               "size %d -> %s", size,
                               g_engineIsUE4 ? "UE4 (floats)" : "UE5 (doubles)");
}

// ---- debug trace (ogsa_telemetry_debug.log beside the DLL) ----
// UEVR's own log needs its overlay/console; the mod writes its own small
// trace (rewritten each launch): config as loaded, the first FName
// resolution, and every label change.
static void DebugLog(const char* fmt, ...)
{
    static FILE* f = nullptr;
    if (!f)
    {
        char path[MAX_PATH]{};
        GetModuleFileNameA(g_module, path, MAX_PATH);
        std::string p(path);
        const auto slash = p.find_last_of("\\/");
        f = fopen((slash == std::string::npos
            ? std::string("ogsa_telemetry_debug.log")
            : p.substr(0, slash + 1) + "ogsa_telemetry_debug.log").c_str(), "w");
        if (!f) return;
    }
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);
}

// Surfaces what we're tracking as the discovery typeLabel: pawn FName ->
// string -> UTF-8, instance suffix stripped by the publisher
// ("BP_Foo_C_3" -> "BP_Foo_C"); the object NAME stays "player" (stable —
// receivers match motion to discovery by it). The buffer is
// zero-initialized and its CONTENT is the source of truth: the API docs
// say nothing about to_string's return semantics (length? bool? bytes?),
// and gating on it never set the label in the field. The resolved string
// is cached because FName handles may be per-call values.
static void UpdateTypeLabel(const UEVR_SDKData* sdk, UEVR_UObjectHandle pawn)
{
    static std::wstring lastName;
    static bool loggedApi = false;
    if (!loggedApi)
    {
        loggedApi = true;
        DebugLog("fname api: uobject=%s fname=%s\n",
                 sdk->uobject ? "ok" : "NULL",
                 sdk->fname ? "ok" : "NULL");
    }
    if (sdk->uobject == nullptr || sdk->fname == nullptr) return;
    UEVR_FNameHandle fname = sdk->uobject->get_fname(pawn);
    if (fname == nullptr) return;
    wchar_t wname[160]{};
    const unsigned int ret = sdk->fname->to_string(
        fname, wname, (unsigned int)(sizeof wname / sizeof wname[0]));
    const size_t len = wcslen(wname); // zeroed buffer: valid whatever ret means
    static bool loggedFirst = false;
    if (!loggedFirst)
    {
        loggedFirst = true;
        DebugLog("first fname: ret=%u len=%llu '%ls'\n",
                 ret, (unsigned long long)len, wname);
    }
    if (len == 0 || lastName == wname) return;
    lastName = wname;
    char utf8[640]{};
    if (WideCharToMultiByte(CP_UTF8, 0, wname, (int)len, utf8, sizeof utf8,
                            nullptr, nullptr) > 0)
    {
        g_publisher.set_type_label(utf8);
        DebugLog("typeLabel set: '%s'\n", utf8);
    }
}

static bool SamplePawn(ogsa::telemetry::Vec3& posCm,
                       ogsa::telemetry::Quat& quat,
                       ogsa::telemetry::Vec3& velCm)
{
    const UEVR_SDKData* sdk = g_api->sdk;
    UEVR_UObjectHandle pawn = sdk->functions->get_local_pawn(0);
    if (pawn == nullptr) return false;
    UpdateTypeLabel(sdk, pawn);
    if (g_engineIsUE4 < 0)
    {
        DetectEngineAbi(sdk, pawn);
        if (g_engineIsUE4 < 0) return false; // never publish unresolved ABI
    }

    if (g_engineIsUE4)
    {
        FVectorUE4 loc{}, vel{};
        FRotatorUE4 rot{};
        sdk->uobject->call_function(pawn, L"K2_GetActorLocation", &loc);
        sdk->uobject->call_function(pawn, L"K2_GetActorRotation", &rot);
        // AActor::GetVelocity (BlueprintPure): movement-component velocity
        // in cm/s — engine-measured, one differentiation better than
        // deriving it from position deltas
        sdk->uobject->call_function(pawn, L"GetVelocity", &vel);
        posCm = { loc.X, loc.Y, loc.Z };
        velCm = { vel.X, vel.Y, vel.Z };
        quat = RotatorToQuat(rot.Pitch, rot.Yaw, rot.Roll);
    }
    else
    {
        FVectorUE5 loc{}, vel{};
        FRotatorUE5 rot{};
        sdk->uobject->call_function(pawn, L"K2_GetActorLocation", &loc);
        sdk->uobject->call_function(pawn, L"K2_GetActorRotation", &rot);
        sdk->uobject->call_function(pawn, L"GetVelocity", &vel);
        posCm = { (float)loc.X, (float)loc.Y, (float)loc.Z };
        velCm = { (float)vel.X, (float)vel.Y, (float)vel.Z };
        quat = RotatorToQuat(rot.Pitch, rot.Yaw, rot.Roll);
    }
    return true;
}

// Pause (standard CONVENTIONS C-11): APlayerController::IsPaused is
// BlueprintCallable and reports the world's pause state. It takes no
// inputs, so the params buffer is just the return value; a missing
// function leaves it zeroed, which reads as running. The engine tick
// keeps firing while the game is paused, so the stream continues.
static bool IsGamePaused(const UEVR_SDKData* sdk)
{
    UEVR_UObjectHandle pc = sdk->functions->get_player_controller(0);
    if (pc == nullptr) return false;
    struct { bool ReturnValue; uint8_t Pad[15]; } parms{};
    sdk->uobject->call_function(pc, L"IsPaused", &parms);
    return parms.ReturnValue;
}

static void OnEngineTick(UEVR_UGameEngineHandle, float deltaSeconds)
{
    ogsa::telemetry::Vec3 pos, vel;
    ogsa::telemetry::Quat quat;
    const bool hasPose = SamplePawn(pos, quat, vel);
    if (g_detectPause && hasPose)
    {
        const bool paused = IsGamePaused(g_api->sdk);
        if (paused != g_publisher.paused())
        {
            g_publisher.set_paused(paused);
            DebugLog("pause: %s\n", paused ? "paused" : "running");
        }
    }
    g_publisher.tick((double)deltaSeconds, hasPose, pos, quat, hasPose, vel);
}

// ---- config ----
static std::string ConfigPath()
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    std::string p(path);
    const auto slash = p.find_last_of("\\/");
    return slash == std::string::npos ? "ogsa_telemetry.ini"
                                      : p.substr(0, slash + 1) + "ogsa_telemetry.ini";
}

static std::string DefaultGameName()
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string p(path);
    const auto slash = p.find_last_of("\\/");
    const auto dot = p.find_last_of('.');
    std::string name = p.substr(slash + 1, dot - slash - 1);
    // "Game-Win64-Shipping" -> "Game": receivers display gameName and key
    // per-game state on it, so strip the pack/build tokens (INI overrides)
    for (;;)
    {
        const auto dash = name.find_last_of('-');
        if (dash == std::string::npos) break;
        const std::string tok = name.substr(dash + 1);
        const bool known = tok == "Shipping" || tok == "Test"
                        || tok == "DebugGame" || tok == "Win64"
                        || tok == "WinGDK" || tok == "Win32";
        if (!known) break;
        name.resize(dash);
    }
    return name;
}

static uint16_t ObjectTypeFromName(const std::string& s)
{
    // ObjectType ordinals since corpus v3: `other` holds 0 so the
    // catch-all never interleaves with real classes — 0 other, 1 vehicle,
    // 2 aircraft, 3 watercraft, 4 spacecraft, 5 humanoid, 6 camera,
    // 7 creature
    if (s == "vehicle") return 1;
    if (s == "aircraft") return 2;
    if (s == "watercraft") return 3;
    if (s == "spacecraft") return 4;
    if (s == "creature") return 7;
    if (s == "other") return 0;
    if (s == "camera") return 6;
    return 5; // humanoid
}

static void LoadConfig(ogsa::PublisherConfig& cfg)
{
    std::ifstream ini(ConfigPath());
    if (!ini) return;
    std::string line;
    while (std::getline(ini, line))
    {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "group") cfg.group = _strdup(val.c_str());
        else if (key == "port") cfg.port = atoi(val.c_str());
        else if (key == "ttl") cfg.ttl = atoi(val.c_str());
        else if (key == "motionHz") cfg.motionHz = atof(val.c_str());
        else if (key == "objectName") cfg.objectName = _strdup(val.c_str());
        else if (key == "objectType") cfg.objectType = ObjectTypeFromName(val);
        else if (key == "gameName") cfg.gameName = _strdup(val.c_str());
        else if (key == "dynamics")
            cfg.sendDynamics = (val != "off" && val != "0");
        else if (key == "pause")
            g_detectPause = (val != "off" && val != "0");
        else if (key == "engine")
        {
            if (val == "ue4") g_engineIsUE4 = 1;
            else if (val == "ue5") g_engineIsUE4 = 0;
            else g_engineIsUE4 = -1; // auto (default)
        }
    }
}

// ---- plugin entry point ----
extern "C" __declspec(dllexport) bool uevr_plugin_initialize(
    const UEVR_PluginInitializeParam* param)
{
    g_api = param;

    ogsa::PublisherConfig cfg;
    cfg.gameName = _strdup(DefaultGameName().c_str());
    LoadConfig(cfg);
    DebugLog("ogsa_telemetry uevr start: game='%s' object='%s' -> %s:%d\n",
             cfg.gameName, cfg.objectName, cfg.group, cfg.port);

    if (!g_publisher.start(cfg))
    {
        param->functions->log_error(
            "[ogsa] failed to create multicast socket — plugin disabled");
        return false;
    }
    param->sdk->callbacks->on_post_engine_tick(OnEngineTick);
    param->functions->log_info("[ogsa] OGSA-TS telemetry loaded: pawn '%s' "
                               "from '%s' -> %s:%d (engine ABI: %s, "
                               "dynamics: %s)",
                               cfg.objectName, cfg.gameName, cfg.group,
                               cfg.port,
                               g_engineIsUE4 == 1 ? "ue4 (forced)"
                               : g_engineIsUE4 == 0 ? "ue5 (forced)"
                               : "auto-detect on first pawn",
                               cfg.sendDynamics ? "on" : "off");
    return true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) g_module = module;
    return TRUE;
}
