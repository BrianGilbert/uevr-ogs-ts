// ogsa_publisher.cpp — see header. The wire layout mirrors
// ogsa-ts-unreal's UTelemetryPublisherComponent exactly (same allocation
// order, corpus-locked serializer).
#include "ogsa_publisher.h"
#include <ctime>

namespace ogsa {

using namespace telemetry;

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
                     const Vec3& posCm, const Quat& quat)
{
    clock_ += dt;
    if (clock_ - lastDiscovery_ >= cfg_.discoverySeconds)
    {
        sendDiscovery();
        lastDiscovery_ = clock_;
    }
    if (hasPose && clock_ - lastMotion_ >= 1.0 / cfg_.motionHz)
    {
        sendMotion(posCm, quat);
        lastMotion_ = clock_;
    }
}

void Publisher::sendDiscovery()
{
    wire_.W.clear();
    ObjectDecl obj;
    obj.Name = cfg_.objectName;
    obj.Location = "local";
    obj.Type = cfg_.objectType;
    obj.Primary = true;
    BuildDiscovery(wire_, 1, sessionUs_, cfg_.gameName, Environment{}, { obj });
    send(FrameType::Discovery, wire_.Wrap(FrameType::Discovery));
}

void Publisher::sendMotion(const Vec3& posCm, const Quat& quat)
{
    // UE world (cm, X-fwd Y-right Z-up) -> standard (m, Z-fwd X-right Y-up)
    Vec3 std = ToStandard(posCm);
    std.X /= 100.0f; std.Y /= 100.0f; std.Z /= 100.0f;
    Quat stdQ = ToStandard(quat);

    MotionObjectInput o;
    o.Name = cfg_.objectName;
    o.Position = std;
    o.Orientation = stdQ;
    wire_.W.clear();
    BuildMotion(wire_, clock_, { o });
    send(FrameType::Motion, wire_.Wrap(FrameType::Motion));
}

} // namespace ogsa
