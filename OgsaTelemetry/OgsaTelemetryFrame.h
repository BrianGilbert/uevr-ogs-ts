// OgsaTelemetryFrame.h — Unreal Engine → OGSA-TS frame conversion.
//
// UE is left-handed, X-forward, Y-right, Z-up. OGSA-TS is left-handed,
// Z-forward, X-right, Y-up. The map is the cyclic axis permutation
//   (x, y, z)_std = (y, z, x)_ue — an even permutation, so handedness
//   is preserved (both left-handed).
// For orientation, the component map is the INVERSE cycle of q_p's
// rotation (q_p cycles X→Y→Z; the map cycles X→Z→Y), so the converted
// rotation is
//   q_std = q_p⁻¹ · q_ue · q_p
// (verified: identity fixed; UE yaw about Z-up rotates standard forward
// toward standard right — equal angle, equal sense; norm preserved).
#pragma once
#include <cmath>
#include "OgsaTelemetryWire.h"

namespace ogsa::telemetry
{

inline Vec3 ToStandard(const Vec3& ue) { return { ue.Y, ue.Z, ue.X }; }

inline Quat Multiply(const Quat& a, const Quat& b)
{
    // (w1,v1)*(w2,v2) with Hamilton product
    float aw = a.W, bw = b.W;
    float ax = a.X, ay = a.Y, az = a.Z;
    float bx = b.X, by = b.Y, bz = b.Z;
    return {
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    };
}

inline Quat ToStandard(const Quat& ue)
{
    static constexpr Quat Qp{ 0.5f, 0.5f, 0.5f, 0.5f };
    static constexpr Quat QpInv{ -0.5f, -0.5f, -0.5f, 0.5f };
    return Multiply(Multiply(QpInv, ue), Qp);
}

// rotate v by q (v' = q v q*)
inline Vec3 Rotate(const Quat& q, const Vec3& v)
{
    Quat p{ v.X, v.Y, v.Z, 0.0f };
    Quat qc{ -q.X, -q.Y, -q.Z, q.W };
    Quat r = Multiply(Multiply(q, p), qc);
    return { r.X, r.Y, r.Z };
}

// ---- pose → body-dynamics derivation ---------------------------------
// C++ twin of Ogsa.Telemetry.BodyDynamicsDeriver (ogsa-ts-dotnet); the
// two must stay behaviourally identical — that project's tests pin the
// semantics, and this twin serves the injected senders (uevr-ogsa-ts,
// ue4ss-ogsa-ts) whose games expose no accelerometer-class data.
//
// Semantics (schema bodyDynamics variant, accelerometer-style):
//   specificForce = a_world − g_world — +9.80665 on +heave at rest
//   (what an IMU reads), rotated into the C-4 body frame by the inverse
//   of the reported orientation (v_world = q · v_body for actor-style
//   rotations in the standard frame).
//   angularVelocity = log(q_t⁻¹ ⊗ q_t₊₁)/dt, artifact-free at attitude
//   representation flips (unlike Euler differentiation).
//
// C-4 component sense (the subtle part): the receiver's two paths must
// agree — native ω versus d(euler)/dt of its EulerC4 extraction
// (pitch = asin(f.y) nose-up+, yaw = atan2(f.x, f.z) nose-right+,
// roll = atan2(u.x, u.y) right-side-down+). Pure-axis cases through that
// extraction show: a positive stored-Hamilton rotation about +y is
// nose-right (matches +yaw), but about +x it tilts the nose DOWN and
// about +z rolls left-side-down — so C-4 pitch/roll rates are the
// NEGATED stored-math components. ω is mapped component-wise as
// (−x, +y, −z), making sender-derived rates equal the Euler-
// differentiated rates the same receiver would otherwise compute.
//
// Velocity may be supplied by the caller when the engine measures it
// (UEVR GetVelocity); otherwise it is derived from position deltas.
// Acceleration is low-pass filtered (τ = 0.1 s) to tame sampling jitter;
// angular velocity is NOT filtered (onset cues need its edges).
// Teleports (> 2.5 m per sample) reset the estimator instead of spiking;
// after a reset the outputs are the exact static values (gravity
// reaction in the body frame, zero rates).
struct BodyDynamicsDeriver
{
    static constexpr float Gravity = 9.80665f;       // m/s²
    static constexpr double AccelFilterTau = 0.1;    // s
    static constexpr float TeleportThresholdMetres = 2.5f;

    Vec3 SpecificForce{};    // m/s², body frame, C-4 axes
    Vec3 AngularVelocity{};  // rad/s, body frame, C-4 axes

    /// Feeds one pose sample (standard frame: metres, unit quaternion,
    /// world space; dt in seconds — call every host tick, not just at
    /// publish cadence, so derivative denominators stay small).
    void Sample(double dt, const Vec3& position, const Quat& orientation,
                bool hasVelocity = false, const Vec3& engineVelocity = {})
    {
        if (!(dt > 0.0) || !hasHistory_)
        {
            Reset(position, orientation);
            return;
        }
        const Vec3 delta = Sub(position, prevPosition_);
        if (Len(delta) > TeleportThresholdMetres)
        {
            Reset(position, orientation);
            return;
        }

        const Vec3 velocity = hasVelocity
            ? engineVelocity
            : Scale(delta, (float)(1.0 / dt));
        const Vec3 accel = Scale(Sub(velocity, prevVelocity_), (float)(1.0 / dt));
        // one-pole low-pass: y += (x − y)·dt/τ, stable for any dt
        const double blend = dt / AccelFilterTau < 1.0 ? dt / AccelFilterTau : 1.0;
        filteredAccel_ = Add(filteredAccel_,
                             Scale(Sub(accel, filteredAccel_), (float)blend));

        AngularVelocity = AngularVelocityFromDelta(prevOrientation_, orientation, dt);
        SpecificForce = RotateByInverse(orientation,
            Add(filteredAccel_, Vec3{ 0.0f, Gravity, 0.0f }));

        prevPosition_ = position;
        prevVelocity_ = velocity;
        prevOrientation_ = orientation;
    }

    /// Static value after any reset: gravity reaction in the body frame,
    /// zero angular velocity.
    void Reset(const Vec3& position, const Quat& orientation)
    {
        SpecificForce = RotateByInverse(orientation, Vec3{ 0.0f, Gravity, 0.0f });
        AngularVelocity = Vec3{};
        prevPosition_ = position;
        prevVelocity_ = Vec3{};
        prevOrientation_ = orientation;
        filteredAccel_ = Vec3{};
        hasHistory_ = true;
    }

    /// Body-frame angular velocity (rad/s, C-4 component sense) from the
    /// world-frame orientation delta; see the block comment above for the
    /// (−x, +y, −z) component map.
    static Vec3 AngularVelocityFromDelta(const Quat& previous,
                                         const Quat& current, double dt)
    {
        // dq = q_prev⁻¹ ⊗ q_cur  (body-frame rotation over dt)
        const Quat inv{ -previous.X, -previous.Y, -previous.Z, previous.W };
        Quat dq = Multiply(inv, current);
        if (dq.W < 0.0f) // hemisphere canonicalization: log stays in [−π, π]
            dq = Quat{ -dq.X, -dq.Y, -dq.Z, -dq.W };
        const Vec3 vec{ dq.X, dq.Y, dq.Z };
        const float vl = Len(vec);
        if (vl < 1e-8f || !(dt > 0.0)) return Vec3{};
        const float angle = 2.0f * std::atan2(vl, dq.W);
        const Vec3 stored = Scale(Scale(vec, 1.0f / vl), angle / (float)dt);
        return Vec3{ -stored.X, stored.Y, -stored.Z }; // C-4 sense
    }

    /// Rotates a world-frame vector into the body frame (v_body =
    /// q⁻¹ · v_world for the actor-style orientation q).
    static Vec3 RotateByInverse(const Quat& q, const Vec3& world)
    {
        return Rotate(Quat{ -q.X, -q.Y, -q.Z, q.W }, world);
    }

private:
    static Vec3 Add(const Vec3& a, const Vec3& b)
        { return Vec3{ a.X + b.X, a.Y + b.Y, a.Z + b.Z }; }
    static Vec3 Sub(const Vec3& a, const Vec3& b)
        { return Vec3{ a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
    static Vec3 Scale(const Vec3& v, float s)
        { return Vec3{ v.X * s, v.Y * s, v.Z * s }; }
    static float Len(const Vec3& v)
        { return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z); }

    Vec3 prevPosition_{}, prevVelocity_{}, filteredAccel_{};
    Quat prevOrientation_{};
    bool hasHistory_ = false;
};

} // namespace ogsa::telemetry
