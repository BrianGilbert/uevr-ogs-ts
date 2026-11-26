// ogs_publisher.h — engine-agnostic OGS-TS publisher for the UEVR plugin.
//
// Owns the winsock multicast socket and the cadence logic (discovery every
// N seconds, motion at the configured rate), and serializes with the
// vendored, corpus-locked ogs-ts-unreal core (OgsTelemetry/). Input pose
// is UE world space (cm, LH X-fwd Y-right Z-up); conversion to the OGS-TS
// frame happens here via ToStandard. Shared with Test/smoke.cpp so the
// wire path is testable without UEVR.
//
// Body dynamics: motion frames carry the bodyDynamics union variant
// (specific force + angular velocity — the direct motion-cueing inputs,
// GameConn docs/cueing.md), derived per engine tick by the core's
// BodyDynamicsDeriver from the sampled pose plus the engine-measured
// velocity when the caller has one (UEVR GetVelocity). Senders with
// native accelerometer-class data would bind that instead; injected
// senders derive, which still beats receiver-side Euler differentiation
// (no representation-flip artifacts). Disable via config (dynamics=off).
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <cstdint>
#include "OgsTelemetry/OgsTelemetryWire.h"
#include "OgsTelemetry/OgsTelemetryFrame.h"

namespace ogs {

struct PublisherConfig
{
    const char* group = "239.255.17.17";
    int port = 40123;
    int ttl = 1;
    double motionHz = 60.0;
    double discoverySeconds = 2.0;
    const char* gameName = "unknown";
    const char* objectName = "player";
    uint16_t objectType = 5; // ObjectType::humanoid (capnp); INI-overridable
    bool sendDynamics = true; // emit the bodyDynamics union variant
};

class VelocityCheck
{
public:
    static constexpr int FailuresBeforeDistrust = 3;
    static constexpr int MaxFrozenWindowsInARow = 6;

    bool trusted() const { return trusted_; }
    int windows() const { return windows_; }
    int failures_in_a_row() const { return failures_; }
    float unexplained_metres() const { return lastUnexplainedMetres_; }
    float allowed_metres() const { return lastAllowedMetres_; }
    float path_metres() const { return lastPathMetres_; }
    float claimed_metres() const { return lastClaimedMetres_; }
    bool skipped_as_frozen() const { return skippedAsFrozen_; }
    void Restart();
    bool Update(double dt, const telemetry::Vec3& positionM,
                const telemetry::Vec3& velocityM);

private:
    static constexpr double WindowSeconds = 0.5;
    static constexpr float AllowedErrorOfPath = 0.5f;
    static constexpr float AllowedErrorMetres = 0.5f;
    static constexpr float FrozenShareOfClaimed = 0.1f;

    telemetry::Vec3 previous_{}, unexplained_{};
    float pathMetres_ = 0.0f, claimedMetres_ = 0.0f, lastClaimedMetres_ = 0.0f;
    int frozenInARow_ = 0;
    bool skippedAsFrozen_ = false;
    float lastUnexplainedMetres_ = 0.0f, lastAllowedMetres_ = 0.0f, lastPathMetres_ = 0.0f;
    double seconds_ = 0.0;
    int windows_ = 0, failures_ = 0;
    bool trusted_ = true, hasPrevious_ = false;
};

class Publisher
{
public:
    ~Publisher() { stop(); }

    bool start(const PublisherConfig& config);
    void stop();
    bool started() const { return socket_ != INVALID_SOCKET; }
    /// Datagrams the socket refused (interface down, no route). Telemetry is
    /// lossy by design: a failed send is counted, never fatal.
    int64_t sendFailures() const { return sendFailures_; }

    /// Updates the tracked object's discovery typeLabel (e.g. the pawn
    /// class name an injected mod is following). The object NAME stays
    /// stable — receivers match motion to discovery by it — while the
    /// label shows what is actually being tracked. The label is object
    /// identity, so a change once discovery has gone out starts a new
    /// generation (implementation-udp.md § Paged discovery) and re-sends
    /// discovery at the next tick. Input is stripped by
    /// StripInstanceSuffix first.
    void set_type_label(const char* label);

    /// Descriptor-set generation carried by discovery and motion frames.
    uint32_t generation() const { return generation_; }

    /// Reports the game's pause state on the ratified session.paused
    /// channel (standard CONVENTIONS C-11). Keep ticking while paused:
    /// motion keeps flowing with the frozen pose, the flag set and no
    /// dynamics, so receivers bring hardware to rest instead of reading
    /// silence as a dead session. Resuming restarts the derivation.
    void set_paused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    /// Advances the clock by dt seconds and, if a pose is available,
    /// publishes at the configured cadence. posCm is UE world centimetres;
    /// quat is UE world orientation (x,y,z,w). velCm (UE world cm/s) is
    /// the engine-measured velocity when the caller has one. Without it, or
    /// while it disagrees with how the pose moves, the deriver uses position
    /// deltas. Derivation runs on EVERY tick with a pose, not just at
    /// publish cadence.
    void tick(double dt, bool hasPose,
              const telemetry::Vec3& posCm, const telemetry::Quat& quat,
              bool hasVel = false, const telemetry::Vec3& velCm = {});

    /// Last derived specific force (m/s², C-4 body frame, accelerometer-
    /// style: +9.80665 heave at rest) — diagnostics/smoke-test access.
    const telemetry::Vec3& last_specific_force() const { return deriver_.SpecificForce; }
    /// Last derived angular velocity (rad/s, C-4 body frame).
    const telemetry::Vec3& last_angular_velocity() const { return deriver_.AngularVelocity; }

    bool using_engine_velocity() const { return velocityCheck_.trusted(); }
    const VelocityCheck& velocity_check() const { return velocityCheck_; }
    double stream_time() const { return clock_; }

private:
    static constexpr UINT_PTR INVALID_SOCKET_VALUE = (UINT_PTR)~0;
    void sendDiscovery();
    void sendMotion(const telemetry::Vec3& posStdM, const telemetry::Quat& stdQ);
    void send(const telemetry::FrameType type, const std::vector<uint8_t>& datagram)
    {
        if (socket_ != INVALID_SOCKET_VALUE && !datagram.empty() &&
            ::sendto((SOCKET)socket_, (const char*)datagram.data(),
                     (int)datagram.size(), 0,
                     (const sockaddr*)&dst_, sizeof dst_) == SOCKET_ERROR)
            ++sendFailures_;
    }

    SOCKET socket_ = INVALID_SOCKET;
    struct sockaddr_in dst_ {};
    telemetry::WireBuilder wire_;
    telemetry::BodyDynamicsDeriver deriver_;
    bool hadPose_ = false;
    VelocityCheck velocityCheck_;
    bool paused_ = false, wasPaused_ = false;
    PublisherConfig cfg_;
    std::string typeLabel_;
    uint32_t generation_ = 0;
    bool discoverySent_ = false;
    double clock_ = 0.0, lastDiscovery_ = -1e18, lastMotion_ = -1e18;
    int64_t sessionUs_ = 0;
    int64_t sendFailures_ = 0;
};

/// Strips a UE instance-number suffix ("BP_Foo_C_3" -> "BP_Foo_C",
/// "Sportscar_12" -> "Sportscar") so runtime-tracked class names stay
/// stable across respawn/GC; returns the input unchanged when the last
/// underscore segment is not purely numeric.
std::string StripInstanceSuffix(const std::string& name);

} // namespace ogs
