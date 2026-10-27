// OgsaTelemetryWire.h — OGSA-TS frame builders and the OMT1 envelope.
//
// The engine-facing API: plain input structs in, a canonical-encoded
// datagram out. Frames are written through the generated schema writers
// (OgsaTelemetrySchema.h — offsets from the official Cap'n Proto compiler,
// never hand-derived) and laid out by the canonical encoder
// (OgsaTelemetryCanonical.h), so the bytes follow the standard's canonical
// OGSA encoding and byte-match the conformance corpus
// (OGSA-Telemetry-Standard/conformance/; Test/ proves it).
//
// Frames needing schema fields these input structs do not cover can use
// the schema writers directly and encode them with wire::Encode.
#pragma once
#include "OgsaTelemetrySchema.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ogsa::telemetry
{

enum class FrameType : uint8_t { Discovery = 0, Motion = 1, Event = 2 };

struct Vec3 { float X = 0, Y = 0, Z = 0; };
struct Quat { float X = 0, Y = 0, Z = 0, W = 1; };

/// Holds one encoded message (single segment, canonical form) and wraps
/// it in the 8-byte OMT1 envelope.
class WireBuilder
{
public:
    std::vector<uint64_t> W;

    /// OMT1 datagram: magic, frame type, 3 reserved bytes, one-word segment
    /// table (count-1 = 0, segment words), segment words — all little-endian,
    /// written by shifts.
    std::vector<uint8_t> Wrap(FrameType type) const
    {
        std::vector<uint8_t> d;
        d.reserve(16 + W.size() * 8);
        d.push_back('O'); d.push_back('M'); d.push_back('T'); d.push_back('1');
        d.push_back((uint8_t)type); d.push_back(0); d.push_back(0); d.push_back(0);
        auto put32 = [&d](uint32_t v) {
            for (int i = 0; i < 4; i++) d.push_back((uint8_t)(v >> (8 * i)));
        };
        put32(0); put32((uint32_t)W.size());
        for (uint64_t w : W)
            for (int i = 0; i < 8; i++) d.push_back((uint8_t)(w >> (8 * i)));
        return d;
    }
};

// ---- frame inputs ----

struct ChannelDecl
{
    std::string Name, Unit, Description;
    double Min = 0, Max = 0; bool HasRange = false;
};

struct ChannelValueInput
{
    uint16_t Id = 0;
    double Number = 0; // used when !IsBool
    bool Boolean = false, IsBool = false;
};

struct MotionObjectInput
{
    std::string Name;
    Vec3 Position; Quat Orientation;
    Vec3 SpecificForce;   // m/s², accelerometer-style: f = a − g (+heave
                          // gravity reaction at rest); body frame, C-4 axes
    Vec3 AngularVelocity; // rad/s, body frame, C-4 axes (+x nose-up,
                          // +y nose-right, +z right-side-down)
    bool HasBodyDynamics = false;
    Vec3 LocalGravity;    // m/s², world frame, pointing the way gravity
                          // pulls; per frame (CONVENTIONS C-12)
    bool HasLocalGravity = false;
    std::vector<ChannelValueInput> Channels;
};

struct ObjectDecl
{
    std::string Name, Location, TypeLabel, RefPointName = "origin";
    uint16_t Type = 1; bool Primary = true; // ObjectType ordinal; other
                         // holds 0 so the catch-all never interleaves with
                         // real classes: 0 other, 1 vehicle, 2 aircraft,
                         // 3 watercraft, 4 spacecraft, 5 humanoid, 6 camera,
                         // 7 creature
    std::vector<ChannelDecl> Channels; // ids are assigned by position
};

struct EventInput
{
    uint32_t Id = 0;
    std::string Name, Object;
    float Intensity = 1, Duration = 0;
    Vec3 Direction; bool HasDirection = false;
};

struct Environment
{
    // standard atmosphere in the schema's SI units (kg/m^3, degC, Pa, m/s^2)
    // and standard gravity, the value BodyDynamicsDeriver assumes by default
    float AirDensity = 1.225f, Temperature = 20, Pressure = 101325, Gravity = 9.80665f;
    uint16_t Medium = 2; // ContactMedium ordinal; other holds 0 so the
                         // catch-all never interleaves with real mediums:
                         // 0 other, 1 air, 2 asphalt, 3 dirt, 4 grass,
                         // 5 gravel, 6 rumbleStrip, 7 water, 8 snow,
                         // 9 ice, 10 vacuum
    // gravity direction (CONVENTIONS C-12): unset = along world −Y with
    // magnitude Gravity; set = world-frame vector (Gravity = its length)
    Vec3 GravityVector; bool HasGravityVector = false;

    /// Session gravity as a world-frame vector (C-12 resolution).
    Vec3 GravityWorld() const
    {
        return HasGravityVector ? GravityVector : Vec3{ 0.0f, -Gravity, 0.0f };
    }

    /// Sets the session gravity from a world-frame vector: straight down
    /// rides as the scalar alone, any other direction also as the vector.
    void SetGravity(const Vec3& g)
    {
        Gravity = std::sqrt(g.X * g.X + g.Y * g.Y + g.Z * g.Z);
        HasGravityVector = !(g.X == 0.0f && g.Z == 0.0f && g.Y <= 0.0f);
        GravityVector = HasGravityVector ? g : Vec3{};
    }
};

// ---- frame builders ----

inline void SetVec3(schema::Vector3& out, Vec3 v)
{
    out.SetX(v.X); out.SetY(v.Y); out.SetZ(v.Z);
}

inline void BuildDiscovery(WireBuilder& b, uint16_t schemaVersion,
                           int64_t sessionUnixUs, const std::string& gameName,
                           const Environment& env,
                           const std::vector<ObjectDecl>& objects)
{
    schema::DiscoveryFrame d;
    d.SetSchemaVersion(schemaVersion);
    d.SetGameName(gameName);
    d.SetSessionStartUnixUs(sessionUnixUs);
    schema::Environment& e = d.Environment();
    e.SetAirDensity(env.AirDensity); e.SetTemperature(env.Temperature);
    e.SetPressure(env.Pressure); e.SetGravity(env.Gravity);
    e.SetMedium((schema::ContactMedium)env.Medium);
    if (env.HasGravityVector) SetVec3(e.InitGravityVector(), env.GravityVector);
    for (const ObjectDecl& o : objects)
    {
        schema::ObjectDescriptor& od = d.AddObjects();
        od.SetName(o.Name);
        od.SetType((schema::ObjectType)o.Type);
        od.SetLocation(o.Location);
        od.SetTypeLabel(o.TypeLabel);
        od.SetPrimary(o.Primary);
        od.ReferencePoint().SetName(o.RefPointName);
        for (size_t i = 0; i < o.Channels.size(); i++)
        {
            const ChannelDecl& c = o.Channels[i];
            schema::ChannelDescriptor& cd = od.AddChannels();
            cd.SetId((uint16_t)i);
            cd.SetName(c.Name);
            cd.SetUnit(c.Unit);
            cd.SetDescription(c.Description);
            if (c.HasRange)
            {
                schema::Range& r = cd.InitRange();
                r.SetMin(c.Min); r.SetMax(c.Max);
            }
        }
    }
    b.W = wire::Encode(d);
}

inline void BuildMotion(WireBuilder& b, double timestamp,
                        const std::vector<MotionObjectInput>& objects)
{
    schema::MotionFrame m;
    m.SetTimestamp(timestamp);
    for (const MotionObjectInput& o : objects)
    {
        schema::MotionObject& mo = m.AddObjects();
        mo.SetName(o.Name);
        SetVec3(mo.Position(), o.Position);
        schema::Quaternion& q = mo.Orientation();
        q.SetX(o.Orientation.X); q.SetY(o.Orientation.Y);
        q.SetZ(o.Orientation.Z); q.SetW(o.Orientation.W);
        if (o.HasBodyDynamics)
        {
            schema::BodyDynamics& bd = mo.InitBodyDynamics();
            SetVec3(bd.SpecificForce(), o.SpecificForce);
            SetVec3(bd.AngularVelocity(), o.AngularVelocity);
        }
        if (o.HasLocalGravity) SetVec3(mo.LocalGravity().InitVector(), o.LocalGravity);
        for (const ChannelValueInput& c : o.Channels)
        {
            schema::ChannelValue& cv = mo.AddChannels();
            cv.SetId(c.Id);
            if (c.IsBool) cv.SetBoolean(c.Boolean);
            else cv.SetNumber((float)c.Number);
        }
    }
    b.W = wire::Encode(m);
}

inline void BuildEventFrame(WireBuilder& b, double timestamp,
                            const std::vector<EventInput>& events)
{
    schema::EventFrame f;
    f.SetTimestamp(timestamp);
    for (const EventInput& ev : events)
    {
        schema::Event& e = f.AddEvents();
        e.SetId(ev.Id);
        e.SetName(ev.Name);
        e.SetIntensity(ev.Intensity);
        e.SetDuration(ev.Duration);
        e.SetObject(ev.Object);
        if (ev.HasDirection) SetVec3(e.InitDirection(), ev.Direction);
    }
    b.W = wire::Encode(f);
}

} // namespace ogsa::telemetry
