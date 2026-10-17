// conformance.cpp — serializer conformance test guarding this plugin's
// vendored core (OgsaTelemetry/*.h, synced via sync-core.ps1). Rebuilds
// the standard's seven golden vectors from semantic inputs and
// byte-compares against conformance/vectors/*.hex, then checks the UE→
// standard frame conversion. Exit code 0 = all pass.
//
// Build & run (no engine required):
//   cmake -S Test -B Test/build && cmake --build Test/build
//   Test/build/ogsa_conformance <corpusDir>
// <corpusDir> defaults to ../../OGSA-Telemetry-Standard/conformance
#include "OgsaTelemetry/OgsaTelemetryWire.h"
#include "OgsaTelemetry/OgsaTelemetryFrame.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ogsa::telemetry;

static int g_fail = 0, g_pass = 0;

static void Check(const char* name, bool ok, const char* detail = "")
{
    ok ? g_pass++ : g_fail++;
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                ok || !*detail ? "" : " — ", detail);
}

static std::vector<uint8_t> HexToBytes(const std::filesystem::path& p)
{
    std::ifstream f(p);
    std::stringstream ss; ss << f.rdbuf();
    std::string hex = ss.str(), out;
    for (char c : hex)
        if (c != '\n' && c != '\r' && c != ' ') out += c;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < out.size(); i += 2)
        bytes.push_back((uint8_t)std::strtol(out.substr(i, 2).c_str(), nullptr, 16));
    return bytes;
}

static void CheckVector(const std::filesystem::path& dir, const char* id,
                        FrameType type, const std::vector<uint8_t>& built)
{
    auto want = HexToBytes(dir / "vectors" / (std::string(id) + ".hex"));
    bool ok = built == want;
    Check((std::string("corpus: ") + id + " bytes").c_str(), ok);
    if (!ok && built.size() != want.size())
    {
        char d[64]; std::snprintf(d, sizeof d, "size %zu vs %zu",
                                  built.size(), want.size());
        Check((std::string("corpus: ") + id + " size").c_str(), false, d);
    }
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    fs::path corpus = argc > 1 ? argv[1]
        : fs::path("..") / ".." / "OGSA-Telemetry-Standard" / "conformance";
    WireBuilder b;

    { // discovery-minimal
        ObjectDecl obj; obj.Name = "obj"; obj.Location = "local";
        BuildDiscovery(b, 1, 1730000000000000LL, "conformance",
                       Environment{}, { obj });
        CheckVector(corpus, "discovery-minimal", FrameType::Discovery,
                    b.Wrap(FrameType::Discovery));
    }
    { // discovery-channels
        ObjectDecl obj; obj.Name = "demo_car"; obj.Location = "local";
        obj.TypeLabel = "conformance car";
        obj.Channels = {
            { "engine.rpm", "rpm", "", 0, 9000, true },
            { "vehicle.speed", "m/s", "", 0, 30, true },
            { "lights.headlights", "", "", 0, 0, false },
        };
        BuildDiscovery(b, 1, 1730000000000000LL, "conformance",
                       Environment{}, { obj });
        CheckVector(corpus, "discovery-channels", FrameType::Discovery,
                    b.Wrap(FrameType::Discovery));
    }
    { // motion-pose (positionPad mirrors the reference generator's pad bytes)
        MotionObjectInput o; o.Name = "obj";
        o.Position = { 1.5f, 0.25f, -3.75f };
        o.Orientation = { 0, 0, 0, 1 };
        o.PositionPad = 0xFFFFFFFF;
        BuildMotion(b, 1.25, { o });
        CheckVector(corpus, "motion-pose", FrameType::Motion,
                    b.Wrap(FrameType::Motion));
    }
    { // motion-channels
        MotionObjectInput o; o.Name = "demo_car";
        o.Position = { -2, 0.5f, 3 };
        o.Orientation = { 0, 1, 0, 0 };
        o.Channels = { { 0, 3250.5 }, { 1, 8.0 }, { 2, 0, true, true } };
        BuildMotion(b, 2.5, { o });
        CheckVector(corpus, "motion-channels", FrameType::Motion,
                    b.Wrap(FrameType::Motion));
    }
    { // motion-multi
        MotionObjectInput a; a.Name = "player_car";
        a.Position = { 1, 0, 0 }; a.Orientation = { 0, 0, 0, 1 };
        MotionObjectInput c; c.Name = "wingman";
        c.Position = { -1, 0.5f, 2.5f }; c.Orientation = { 0, 1, 0, 0 };
        BuildMotion(b, 3.75, { a, c });
        CheckVector(corpus, "motion-multi", FrameType::Motion,
                    b.Wrap(FrameType::Motion));
    }
    { // motion-bodydynamics (pads mirror the reference generator's pad
      // bytes: position/specificForce carry ffffffff, angularVelocity 0)
        MotionObjectInput o; o.Name = "obj";
        o.Position = { 0.5f, 1.25f, -2.5f };
        o.Orientation = { 0, 0.5f, 0, 0.875f };
        o.HasBodyDynamics = true;
        o.SpecificForce = { 0.25f, 9.5f, -1.5f };
        o.AngularVelocity = { -0.125f, 0.375f, 0.75f };
        o.PositionPad = 0xFFFFFFFF;
        BuildMotion(b, 4.5, { o });
        CheckVector(corpus, "motion-bodydynamics", FrameType::Motion,
                    b.Wrap(FrameType::Motion));
    }
    { // event-impact
        EventInput e; e.Id = 7; e.Name = "body.impact";
        e.Intensity = 0.75f; e.Duration = 0.25f;
        e.Direction = { -1, 0, 0 }; e.HasDirection = true;
        BuildEventFrame(b, 0.125, { e });
        CheckVector(corpus, "event-impact", FrameType::Event,
                    b.Wrap(FrameType::Event));
    }

    // ---- UE frame conversion ----
    {
        Vec3 s = ToStandard(Vec3{ 1, 2, 3 }); // ue X-fwd Y-right Z-up
        Check("frame: permutation (x,y,z)->(y,z,x)",
              s.X == 2 && s.Y == 3 && s.Z == 1);

        Quat id = ToStandard({ 0, 0, 0, 1 });
        Check("frame: identity stays identity",
              id.X == 0 && id.Y == 0 && id.Z == 0 && (id.W == 1 || id.W == -1));

        // UE yaw about Z-up by +90°: q = (0,0,sin45,cos45); forward X -> Y.
        // Standard must rotate Z-forward toward X-right by the same sense.
        float s45 = 0.70710678118f;
        Quat yawStd = ToStandard({ 0, 0, s45, s45 });
        Vec3 fwd = Rotate(yawStd, { 0, 0, 1 }); // standard forward
        Check("frame: UE yaw maps to standard yaw (forward -> right)",
              fwd.X > 0.999f && std::abs(fwd.Y) < 1e-6f && std::abs(fwd.Z) < 1e-6f);

        float n = std::sqrt(yawStd.X * yawStd.X + yawStd.Y * yawStd.Y
                          + yawStd.Z * yawStd.Z + yawStd.W * yawStd.W);
        Check("frame: quaternion norm preserved", std::abs(n - 1) < 1e-6);
    }

    // ---- pose → body-dynamics derivation (mirrors the dotnet tests) ----
    {
        const float g = BodyDynamicsDeriver::Gravity;
        const double dt = 1.0 / 60.0;

        { // rest: gravity reaction +1 g on heave (accelerometer-style),
          // zero rates — also the value after any reset
            BodyDynamicsDeriver d;
            d.Sample(dt, { 5, 0, 5 }, { 0, 0, 0, 1 });
            Check("deriver: rest reads +1 g heave (accelerometer-style)",
                  std::abs(d.SpecificForce.Y - g) < 1e-3f &&
                  std::abs(d.SpecificForce.X) < 1e-3f &&
                  std::abs(d.SpecificForce.Z) < 1e-3f);
            Check("deriver: rest rates zero",
                  std::abs(d.AngularVelocity.X) + std::abs(d.AngularVelocity.Y)
                + std::abs(d.AngularVelocity.Z) < 1e-6f);
        }
        { // nose-up rest (stored −x quarter-turn): body forward points
          // world-up, so gravity reaction reads on +surge
            BodyDynamicsDeriver d;
            Quat q{ -0.70710678118f, 0, 0, 0.70710678118f };
            d.Sample(dt, { 0, 0, 0 }, q);
            Check("deriver: nose-up rest reads +1 g surge",
                  std::abs(d.SpecificForce.Z - g) < 1e-3f &&
                  std::abs(d.SpecificForce.Y) < 1e-3f);
        }
        { // yaw sweep (stored +y): positive C-4 yaw rate (nose-right)
            BodyDynamicsDeriver d;
            const double w = 1.5;
            for (int i = 0; i < 60; i++)
            {
                double t = i * dt;
                Quat q{ 0, (float)std::sin(w * t / 2), 0,
                        (float)std::cos(w * t / 2) };
                d.Sample(dt, { 0, 0, 0 }, q);
            }
            Check("deriver: yaw sweep -> +w on y (C-4 nose-right)",
                  std::abs(d.AngularVelocity.Y - w) < 1e-3 &&
                  std::abs(d.AngularVelocity.X) < 1e-3 &&
                  std::abs(d.AngularVelocity.Z) < 1e-3);
        }
        { // pitch sweep (stored +x tilts the nose DOWN): the C-4 pitch
          // rate equals the euler-rate, i.e. NEGATIVE the sweep rate —
          // pins the (−x, +y, −z) component map
            BodyDynamicsDeriver d;
            const double w = 0.8;
            for (int i = 0; i < 60; i++)
            {
                double t = i * dt;
                Quat q{ (float)std::sin(w * t / 2), 0, 0,
                        (float)std::cos(w * t / 2) };
                d.Sample(dt, { 0, 0, 0 }, q);
            }
            Check("deriver: pitch sweep -> −w on x (euler-rate sense)",
                  std::abs(d.AngularVelocity.X + w) < 1e-3 &&
                  std::abs(d.AngularVelocity.Y) < 1e-3);
        }
        { // roll sweep (stored +z rolls left-side-down): negative C-4 rate
            BodyDynamicsDeriver d;
            const double w = 1.1;
            for (int i = 0; i < 60; i++)
            {
                double t = i * dt;
                Quat q{ 0, 0, (float)std::sin(w * t / 2),
                        (float)std::cos(w * t / 2) };
                d.Sample(dt, { 0, 0, 0 }, q);
            }
            Check("deriver: roll sweep -> −w on z (euler-rate sense)",
                  std::abs(d.AngularVelocity.Z + w) < 1e-3);
        }
        { // engine-measured velocity beats stale position: accelerating
          // engine feed (UEVR GetVelocity path) drives surge
            BodyDynamicsDeriver d;
            double v = 0;
            for (int i = 0; i < 90; i++)
            {
                v += 2.0 * dt;
                d.Sample(dt, { 0, 0, 0 }, { 0, 0, 0, 1 }, true,
                         { 0, 0, (float)v });
            }
            Check("deriver: engine velocity drives +2 m/s² surge",
                  std::abs(d.SpecificForce.Z - 2.0f) < 0.1f &&
                  std::abs(d.SpecificForce.Y - g) < 0.1f);
        }
        { // teleport resets to static values instead of spiking
            BodyDynamicsDeriver d;
            for (int i = 0; i < 30; i++)
                d.Sample(dt, { 0, 0, (float)(i * dt) }, { 0, 0, 0, 1 },
                         true, { 0, 0, 1 });
            d.Sample(dt, { 10, 0, 10 }, { 0, 0, 0, 1 });
            Check("deriver: teleport resets (gravity only, zero rates)",
                  std::abs(d.SpecificForce.Y - g) < 1e-3f &&
                  std::abs(d.SpecificForce.Z) < 1e-3f &&
                  std::abs(d.AngularVelocity.Y) < 1e-6f);
        }
    }

    std::printf("conformance: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
