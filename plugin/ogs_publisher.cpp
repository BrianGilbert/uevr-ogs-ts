// ogs_publisher.cpp — see header. The wire layout mirrors
// ogs-ts-unreal's UTelemetryPublisherComponent exactly (same allocation
// order, corpus-locked serializer).
#include "ogs_publisher.h"
#include <ctime>

namespace ogs {

using namespace telemetry;

std::string StripInstanceSuffix(const std::string& name)
{
    const auto us = name.find_last_of('_');
    if (us == std::string::npos || us + 1 == name.size()) return name;
    if (name.find_first_not_of("0123456789", us + 1) != std::string::npos)
        return name; // last segment not purely numeric — not an instance no.
    return name.substr(0, us);
}

void Publisher::set_type_label(const char* label)
{
    std::string s = StripInstanceSuffix(label ? label : "");
    if (s != typeLabel_)
    {
        typeLabel_ = std::move(s);
        lastDiscovery_ = -1e18; // force discovery re-send on the next tick
    }
}

bool Publisher::start(const PublisherConfig& config)
{
    cfg_ = config;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) return false;
    DWORD ttl = (DWORD)cfg_.ttl;
    setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_TTL,
               (const char*)&ttl, sizeof ttl);
    int loop = 1;
    setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_LOOP,
               (const char*)&loop, sizeof loop);
    dst_.sin_family = AF_INET;
    dst_.sin_port = htons((u_short)cfg_.port);
    inet_pton(AF_INET, cfg_.group, &dst_.sin_addr);

    sessionUs_ = (int64_t)time(nullptr) * 1000000;
    return true;
}

void Publisher::stop()
{
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        WSACleanup();
    }
}

void Publisher::tick(double dt, bool hasPose,
                     const Vec3& posCm, const Quat& quat,
                     bool hasVel, const Vec3& velCm)
{
    clock_ += dt;
    if (clock_ - lastDiscovery_ >= cfg_.discoverySeconds)
    {
        sendDiscovery();
        lastDiscovery_ = clock_;
    }
    if (hasPose)
    {
        // derive dynamics in the standard frame, metres — every tick with
        // a pose, not just at publish cadence, so derivative denominators
        // stay small (UE world cm -> standard m)
        Vec3 std = ToStandard(posCm);
        std.X /= 100.0f; std.Y /= 100.0f; std.Z /= 100.0f;
        Vec3 vel = ToStandard(velCm);
        vel.X /= 100.0f; vel.Y /= 100.0f; vel.Z /= 100.0f;
        Quat stdQ = ToStandard(quat);
        // C-11: nothing is measured while paused; resuming starts the
        // derivation fresh rather than differentiating across the pause
        if (!paused_)
        {
            if (wasPaused_) deriver_ = telemetry::BodyDynamicsDeriver{};
            deriver_.Sample(dt, std, stdQ, hasVel, vel);
        }
        if (clock_ - lastMotion_ >= 1.0 / cfg_.motionHz)
        {
            sendMotion(std, stdQ);
            lastMotion_ = clock_;
        }
    }
    else if (hadPose_)
    {
        // pawn loss: the next pose must start fresh — scene changes can
        // land the new pawn near the old world position, which the
        // teleport detector alone would not catch
        deriver_ = telemetry::BodyDynamicsDeriver{};
    }
    hadPose_ = hasPose;
    wasPaused_ = paused_;
}

void Publisher::sendDiscovery()
{
    wire_.W.clear();
    ObjectDecl obj;
    obj.Name = cfg_.objectName;
    obj.Location = "local";
    obj.Type = cfg_.objectType;
    obj.TypeLabel = typeLabel_;
    obj.Primary = true;
    ChannelDecl paused;
    paused.Name = "session.paused"; // ratified, C-11; id 0
    paused.Description = "Simulation paused for the player (C-11)";
    obj.Channels.push_back(paused);
    Environment env{};
    // declare the gravity the derivation actually uses, so consumers
    // scaling tilt cues (deg/g) divide by the sender's real value
    env.Gravity = BodyDynamicsDeriver::Gravity;
    BuildDiscovery(wire_, 1, sessionUs_, cfg_.gameName, env, { obj });
    send(FrameType::Discovery, wire_.Wrap(FrameType::Discovery));
}

void Publisher::sendMotion(const Vec3& posStdM, const Quat& stdQ)
{
    MotionObjectInput o;
    o.Name = cfg_.objectName;
    o.Position = posStdM; // already standard frame, metres
    o.Orientation = stdQ;
    ChannelValueInput paused;
    paused.Id = 0; // session.paused
    paused.IsBool = true;
    paused.Boolean = paused_;
    o.Channels.push_back(paused);
    if (cfg_.sendDynamics && !paused_)
    {
        o.HasBodyDynamics = true;
        o.SpecificForce = deriver_.SpecificForce;
        o.AngularVelocity = deriver_.AngularVelocity;
    }
    wire_.W.clear();
    BuildMotion(wire_, clock_, { o });
    send(FrameType::Motion, wire_.Wrap(FrameType::Motion));
}

} // namespace ogs
