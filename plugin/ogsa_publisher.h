// ogsa_publisher.h — engine-agnostic OGSA-TS publisher for the UEVR plugin.
//
// Owns the winsock multicast socket and the cadence logic (discovery every
// N seconds, motion at the configured rate), and serializes with the
// vendored, corpus-locked ogsa-ts-unreal core (OgsaTelemetry/). Input pose
// is UE world space (cm, LH X-fwd Y-right Z-up); conversion to the OGSA-TS
// frame happens here via ToStandard. Shared with Test/smoke.cpp so the
// wire path is testable without UEVR.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <cstdint>
#include "OgsaTelemetry/OgsaTelemetryWire.h"
#include "OgsaTelemetry/OgsaTelemetryFrame.h"

namespace ogsa {

struct PublisherConfig
{
    const char* group = "239.255.17.17";
    int port = 40123;
    int ttl = 1;
    double motionHz = 60.0;
    double discoverySeconds = 2.0;
    const char* gameName = "unknown";
    const char* objectName = "player";
    uint16_t objectType = 4; // ObjectType::humanoid (capnp); INI-overridable
};

class Publisher
{
public:
    ~Publisher() { stop(); }

    bool start(const PublisherConfig& config);
    void stop();
    bool started() const { return socket_ != INVALID_SOCKET; }

    /// Updates the tracked object's discovery typeLabel (e.g. the pawn
    /// class name an injected mod is following). The object NAME stays
    /// stable — receivers match motion to discovery by it — while the
    /// label shows what is actually being tracked; a changed label forces
    /// the next discovery re-send per the standard's re-send-on-change
    /// rule. Input is stripped by StripInstanceSuffix first.
    void set_type_label(const char* label);

    /// Advances the clock by dt seconds and, if a pose is available,
    /// publishes at the configured cadence. posCm is UE world centimetres;
    /// quat is UE world orientation (x,y,z,w).
    void tick(double dt, bool hasPose,
              const telemetry::Vec3& posCm, const telemetry::Quat& quat);

private:
    static constexpr UINT_PTR INVALID_SOCKET_VALUE = (UINT_PTR)~0;
    void sendDiscovery();
    void sendMotion(const telemetry::Vec3& posCm, const telemetry::Quat& quat);
    void send(const telemetry::FrameType type, const std::vector<uint8_t>& datagram)
    {
        if (socket_ != INVALID_SOCKET_VALUE && !datagram.empty())
            ::sendto((SOCKET)socket_, (const char*)datagram.data(),
                     (int)datagram.size(), 0,
                     (const sockaddr*)&dst_, sizeof dst_);
    }

    SOCKET socket_ = INVALID_SOCKET;
    struct sockaddr_in dst_ {};
    telemetry::WireBuilder wire_;
    PublisherConfig cfg_;
    std::string typeLabel_;
    double clock_ = 0.0, lastDiscovery_ = -1e18, lastMotion_ = -1e18;
    int64_t sessionUs_ = 0;
};

/// Strips a UE instance-number suffix ("BP_Foo_C_3" -> "BP_Foo_C",
/// "Sportscar_12" -> "Sportscar") so runtime-tracked class names stay
/// stable across respawn/GC; returns the input unchanged when the last
/// underscore segment is not purely numeric.
std::string StripInstanceSuffix(const std::string& name);

} // namespace ogsa
