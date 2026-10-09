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

} // namespace ogsa::telemetry
