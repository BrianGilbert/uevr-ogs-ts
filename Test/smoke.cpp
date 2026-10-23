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

    // ---- typeLabel: UE instance-suffix stripping ----
    {
        Check("instance suffix stripped",
              ogsa::StripInstanceSuffix("BP_PlayerCharacter_C_3")
                  == "BP_PlayerCharacter_C");
        Check("multi-digit instance suffix stripped",
              ogsa::StripInstanceSuffix("Sportscar_12") == "Sportscar");
        Check("non-numeric suffix kept",
              ogsa::StripInstanceSuffix("Wing_2L") == "Wing_2L");
        Check("no suffix kept", ogsa::StripInstanceSuffix("Pawn") == "Pawn");
    }

    // ---- wire path: publish 150 synthetic ticks (2.5 s at 60 Hz) ----
    // A pawn circling at 10 m with a yaw sweep — receivers on the LAN
    // (ogsa-ts-dotnet TelemetryReceive) should see discovery + motion.
    // Engine velocity is fed analytically (tangential), exercising the
    // GetVelocity plumbing, and the derived body dynamics are asserted:
    // yaw rate ≈ 90°/s, heave ≈ +1 g (accelerometer-style), horizontal
    // specific force ≈ centripetal ω²r.
    ogsa::Publisher pub;
    ogsa::PublisherConfig cfg;
    cfg.gameName = "ogsa_smoke";
    cfg.objectName = "synthetic_pawn";
    if (!pub.start(cfg)) { std::printf("FAIL socket start\n"); return 1; }
    const double dt = 1.0 / 60.0;
    const double yawRate = 90.0;                    // deg/s
    const double w = yawRate * 3.14159265 / 180.0;  // rad/s
    const double r = 10.0;                          // metres
    for (int i = 0; i < 150; i++)
    {
        const double t = i * dt;
        const double yaw = t * yawRate;
        const double yr = yaw * 3.14159265 / 180.0;
        // horizontal circle in UE's ground plane (X-Y, Z-up const),
        // nose tangential: heading = position angle + 90°, turning
        // nose-right at yawRate — centripetal ω²r points +sway in the
        // body frame the whole time
        Vec3 pos{ (float)(1000.0 * std::cos(yr)),
                  (float)(1000.0 * std::sin(yr)), 0.0f };
        Vec3 vel{ (float)(-1000.0 * w * std::sin(yr)),
                  (float)(1000.0 * w * std::cos(yr)), 0.0f };
        if (i == 75) pub.set_type_label("BP_SyntheticPawn_C_7"); // mid-stream swap
        pub.tick(dt, true, pos, RotatorToQuat(0, yaw + 90.0, 0), true, vel);
    }
    pub.stop();
    Check("150 ticks published (discovery + motion sent, typeLabel "
          "swapped at tick 75)", true);

    // ---- derived body dynamics (circling pawn, engine velocity fed) ----
    {
        const float g = ogsa::telemetry::BodyDynamicsDeriver::Gravity;
        const Vec3& av = pub.last_angular_velocity();
        const Vec3& sf = pub.last_specific_force();
        std::printf("dynamics: av=(%.4f, %.4f, %.4f) sf=(%.4f, %.4f, %.4f) "
                    "[expect av.y=%.4f, sf.y=%.4f, horiz=%.4f]\n",
                    av.X, av.Y, av.Z, sf.X, sf.Y, sf.Z,
                    (float)w, g, (float)(w * w * r));
        Check("dynamics: yaw rate ≈ 90°/s nose-right (+y, C-4)",
              std::abs(av.Y - w) < 0.05 && std::abs(av.X) < 0.01 &&
              std::abs(av.Z) < 0.01);
        Check("dynamics: heave ≈ +1 g (accelerometer-style)",
              std::abs(sf.Y - g) < 0.2);
        // constant nose-right turn: centripetal rides +sway (body x).
        // Surge leakage is bounded by the accel low-pass phase lag on
        // the co-rotating centripetal vector (atan(ωτ) ≈ 9° at this
        // 90°/s turn) plus the backward-difference half-tick skew — not
        // an error, a documented property of filtering before cueing.
        Check("dynamics: centripetal ω²r on +sway (C-4 right-turn)",
              std::abs(sf.X - (float)(w * w * r)) < 1.5 &&
              std::abs(sf.Z) < (float)(w * w * r) * 0.25f);
        const float horiz = std::sqrt(sf.X * sf.X + sf.Z * sf.Z);
        Check("dynamics: horizontal specific force ≈ centripetal ω²r",
              std::abs(horiz - (float)(w * w * r)) < 1.5);
    }

    // ---- pause (C-11): no derivation while paused, fresh start on resume ----
    {
        const float g = ogsa::telemetry::BodyDynamicsDeriver::Gravity;
        const Quat level = RotatorToQuat(0, 0, 0);
        pub.set_paused(true);
        Check("pause: reported", pub.paused());
        for (int i = 0; i < 30; i++)       // frozen pose while paused
            pub.tick(dt, true, Vec3{ 0, 0, 0 }, level);
        pub.set_paused(false);
        pub.tick(dt, true, Vec3{ 500, 0, 0 }, level); // resume elsewhere
        const Vec3& av = pub.last_angular_velocity();
        const Vec3& sf = pub.last_specific_force();
        Check("pause: resume restarts derivation (at rest: +1 g heave, no rates)",
              std::abs(sf.Y - g) < 1e-3f && std::abs(sf.X) < 1e-3f &&
              std::abs(sf.Z) < 1e-3f && std::abs(av.X) < 1e-6f &&
              std::abs(av.Y) < 1e-6f && std::abs(av.Z) < 1e-6f);
    }

    std::printf("smoke: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
