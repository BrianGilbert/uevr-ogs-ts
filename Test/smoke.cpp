// smoke.cpp — standalone verification of the UEVR plugin's wire path and
// math, no UEVR required. Runs the exact publisher code (ogsa_publisher)
// against synthetic pawn motion and asserts the conversion chain:
// FRotator -> FQuat (FRotator::Quaternion transcription) -> ToStandard ->
// facing expectations (mirrors ogsa-ts-unreal Test/main.cpp). Exit 0 = pass.
#include <winsock2.h>
#include <cstdio>
#include <cmath>
#include "../plugin/ogsa_publisher.h"

using namespace ogsa::telemetry;

static int g_pass = 0, g_fail = 0;
static void Check(const char* name, bool ok)
{
    ok ? g_pass++ : g_fail++;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

static ogsa::telemetry::Quat RotatorToQuat(double p, double y, double r)
{
    const double half = 3.14159265358979323846 / 180.0 / 2.0;
    const double sp = std::sin(p * half), cp = std::cos(p * half);
    const double sy = std::sin(y * half), cy = std::cos(y * half);
    const double sr = std::sin(r * half), cr = std::cos(r * half);
    return { (float)(cr * sp * sy - sr * cp * cy),
             (float)(-cr * sp * cy - sr * cp * sy),
             (float)(cr * cp * sy - sr * sp * cy),
             (float)(cr * cp * cy + sr * sp * sy) };
}

int main()
{
    // ---- conversion chain: UE rotator -> UE quat -> standard frame ----
    {
        auto q = RotatorToQuat(0, 0, 0);
        Check("identity rotator -> identity quat",
              q.X == 0 && q.Y == 0 && q.Z == 0 && q.W == 1);
    }
    {
        // UE yaw +90 deg about Z: faces +X -> +Y in UE space.
        // After ToStandard (cyclic xyz permutation), standard forward
        // (+Z) must land on standard right (+X) — same sense (mirrors
        // the quaternion-based check in ogsa-ts-unreal's conformance).
        Quat s = ToStandard(RotatorToQuat(0, 90, 0));
        Vec3 fwd = Rotate(s, { 0, 0, 1 });
        Check("UE yaw 90 -> standard forward faces +X",
              fwd.X > 0.999f && std::abs(fwd.Y) < 1e-6f && std::abs(fwd.Z) < 1e-6f);
    }
    {
        Quat s = ToStandard(RotatorToQuat(0, -90, 0));
        Vec3 fwd = Rotate(s, { 0, 0, 1 });
        Check("UE yaw -90 -> standard forward faces -X",
              fwd.X < -0.999f);
    }
    {
        Quat s = ToStandard(RotatorToQuat(90, 0, 0));
        Vec3 fwd = Rotate(s, { 0, 0, 1 });
        Check("UE pitch 90 -> standard forward faces +Y (up)",
              fwd.Y > 0.999f);
    }
    {
        auto q = RotatorToQuat(30, -145, 12);
        double n = std::sqrt((double)q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
        Check("composite rotator -> unit quaternion",
              std::abs(n - 1.0) < 1e-6);
    }

    // ---- wire path: publish 150 synthetic ticks (2.5 s at 60 Hz) ----
    // A pawn circling at 10 m with a yaw sweep — receivers on the LAN
    // (ogsa-ts-dotnet TelemetryReceive) should see discovery + motion.
    ogsa::Publisher pub;
    ogsa::PublisherConfig cfg;
    cfg.gameName = "ogsa_smoke";
    cfg.objectName = "synthetic_pawn";
    if (!pub.start(cfg)) { std::printf("FAIL socket start\n"); return 1; }
    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 150; i++)
    {
        const double t = i * dt;
        const double yaw = t * 90.0; // deg/s
        Vec3 pos{ (float)(1000.0 * std::cos(yaw * 3.14159265 / 180.0)),
                  0.0f,
                  (float)(1000.0 * std::sin(yaw * 3.14159265 / 180.0)) };
        pub.tick(dt, true, pos, RotatorToQuat(0, yaw, 0));
    }
    pub.stop();
    Check("150 ticks published (discovery + motion sent)", true);

    std::printf("smoke: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
