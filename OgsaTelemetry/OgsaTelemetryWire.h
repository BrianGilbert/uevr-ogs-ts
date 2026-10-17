// OgsaTelemetryWire.h — OGSA-TS wire encoding (Cap'n Proto layout as emitted
// by the reference implementation), envelope, and frame builders.
//
// This is a focused serializer, not a general Cap'n Proto implementation:
// it reproduces the exact byte layout of the standard's conformance corpus
// (OGSA-Telemetry-Standard/conformance/), which the C# reference stack
// (Capnp.Net.Runtime) produces. The layout was reverse-engineered from the
// golden vectors and proven byte-identical by tools/blueprint (C# mirror,
// 6/6 MATCH) before transcription to C++.
//
// Pointer encoding (little-endian words):
//   struct: (u32)(offsetWords << 2) | dataSize << 32 | ptrCount << 48   (kind 0)
//   list:   1 | (u32)(offsetWords << 2) | ((count << 3) | sizeCode) << 32 (kind 1)
//   composite body = tag word (elementCount << 2 | dataW << 32 | ptrW << 48)
//                    followed by the elements; list upper32 = elementWords<<3|7
//   text: byte list, sizeCode 2, content + NUL (count includes NUL)
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ogsa::telemetry
{

enum class FrameType : uint8_t { Discovery = 0, Motion = 1, Event = 2 };

struct Vec3 { float X = 0, Y = 0, Z = 0; };
struct Quat { float X = 0, Y = 0, Z = 0, W = 1; };

/// Builds one single-segment Cap'n Proto message word vector with the
/// reference implementation's allocation order, then wraps it in the
/// 8-byte OMT1 envelope.
class WireBuilder
{
public:
    std::vector<uint64_t> W;

    int Alloc(int words)
    {
        W.insert(W.end(), (size_t)words, 0);
        return (int)W.size() - words;
    }
    void SetU16(int word, int byteOff, uint16_t v)
    {
        uint64_t cur = W[word];
        uint64_t mask = 0xFFFFull << (byteOff * 8);
        W[word] = (cur & ~mask) | ((uint64_t)v << (byteOff * 8));
    }
    void SetU32(int word, int byteOff, uint32_t v)
    {
        SetU16(word, byteOff, (uint16_t)(v & 0xFFFF));
        SetU16(word, byteOff + 2, (uint16_t)(v >> 16));
    }
    void SetF32(int word, int byteOff, float v)
    {
        uint32_t b; std::memcpy(&b, &v, 4); SetU32(word, byteOff, b);
    }
    void SetF64(int word, double v)
    {
        uint64_t b; std::memcpy(&b, &v, 8); W[word] = b;
    }

    // pointer constructors (offset = bodyWord - ptrWord - 1; may be negative)
    static uint64_t StructPtr(int ptrWord, int bodyWord, int dataW, int ptrW)
    {
        return (uint64_t)(uint32_t)((bodyWord - ptrWord - 1) << 2)
             | ((uint64_t)dataW << 32) | ((uint64_t)ptrW << 48);
    }
    static uint64_t TextPtr(int ptrWord, int bodyWord, int byteCount)
    {
        return 1ull | (uint64_t)(uint32_t)((bodyWord - ptrWord - 1) << 2)
             | ((uint64_t)byteCount << 35) | (2ull << 32);
    }
    static uint64_t StructListPtr(int ptrWord, int bodyWord, int elementWords)
    {
        return 1ull | (uint64_t)(uint32_t)((bodyWord - ptrWord - 1) << 2)
             | ((uint64_t)elementWords << 35) | (7ull << 32);
    }
    static uint64_t CompositeTag(int elementCount, int dataW, int ptrW)
    {
        return (uint64_t)(uint32_t)(elementCount << 2)
             | ((uint64_t)dataW << 32) | ((uint64_t)ptrW << 48);
    }

    int Text(const std::string& s) // returns body word; count includes NUL
    {
        int bytes = (int)s.size() + 1;
        int at = Alloc((bytes + 7) / 8);
        for (int i = 0; i < (int)s.size(); i++)
            W[at + i / 8] |= (uint64_t)(uint8_t)s[i] << (8 * (i % 8));
        return at;
    }
    static int TextBytes(const std::string& s) { return (int)s.size() + 1; }

    int Vec3Word(Vec3 v, uint32_t pad = 0) // 2 words: [x|y] [z|pad]
    {
        int a = Alloc(2);
        SetF32(a, 0, v.X); SetF32(a, 4, v.Y);
        SetF32(a + 1, 0, v.Z); SetU32(a + 1, 4, pad);
        return a;
    }
    int QuatWord(Quat q) // 2 words: [x|y] [z|w]
    {
        int a = Alloc(2);
        SetF32(a, 0, q.X); SetF32(a, 4, q.Y);
        SetF32(a + 1, 0, q.Z); SetF32(a + 1, 4, q.W);
        return a;
    }

    /// OMT1 datagram: magic, frame type, 3 reserved bytes, segment table
    /// (count-1, sizes..., pad to even), segment words.
    std::vector<uint8_t> Wrap(FrameType type) const
    {
        std::vector<uint8_t> d;
        d.reserve(8 + 16 + W.size() * 8);
        d.push_back('O'); d.push_back('M'); d.push_back('T'); d.push_back('1');
        d.push_back((uint8_t)type); d.push_back(0); d.push_back(0); d.push_back(0);
        auto put32 = [&d](uint32_t v) {
            for (int i = 0; i < 4; i++) d.push_back((uint8_t)(v >> (8 * i)));
        };
        put32(0); put32((uint32_t)W.size()); // 1-segment table: 1 word, no pad (canonical)
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
    double Number = 0; // used when !Boolean
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
    std::vector<ChannelValueInput> Channels;
    uint32_t PositionPad = 0; // corpus quirk: motion-pose carries ffffffff
};

struct ObjectDecl
{
    std::string Name, Location, TypeLabel, RefPointName = "origin";
    uint16_t Type = 1; bool Primary = true; // ObjectType ordinal; other
                         // holds 0 so the catch-all never interleaves with
                         // real classes: 0 other, 1 vehicle, 2 aircraft,
                         // 3 watercraft, 4 spacecraft, 5 humanoid, 6 camera,
                         // 7 creature
    std::vector<ChannelDecl> Channels;
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
    float AirDensity = 1.25f, Temperature = 20, Pressure = 1, Gravity = 9.5f;
    uint16_t Medium = 2; // ContactMedium ordinal; other holds 0 so the
                         // catch-all never interleaves with real mediums:
                         // 0 other, 1 air, 2 asphalt, 3 dirt, 4 grass,
                         // 5 gravel, 6 rumbleStrip, 7 water, 8 snow,
                         // 9 ice, 10 vacuum
};

// ---- frame builders (allocation order matches the reference stack) ----

inline void BuildDiscovery(WireBuilder& b, uint16_t schemaVersion,
                            int64_t sessionUnixUs, const std::string& gameName,
                            const Environment& env,
                            const std::vector<ObjectDecl>& objects)
{
    b.W.clear();
    b.W.push_back(0);                    // root pointer placeholder
    int body = b.Alloc(5);               // 2 data + 3 ptr
    b.SetU16(body, 0, schemaVersion);
    b.W[body + 1] = (uint64_t)sessionUnixUs;
    int g = b.Text(gameName);
    b.W[body + 2] = WireBuilder::TextPtr(body + 2, g, WireBuilder::TextBytes(gameName));
    int e = b.Alloc(3);
    b.SetF32(e, 0, env.AirDensity); b.SetF32(e, 4, env.Temperature);
    b.SetF32(e + 1, 0, env.Pressure); b.SetF32(e + 1, 4, env.Gravity);
    b.SetU16(e + 2, 0, env.Medium);
    b.W[body + 3] = WireBuilder::StructPtr(body + 3, e, 3, 0);
    int tag = b.Alloc(1 + (int)objects.size() * 9); // each: 1 data + 8 ptr
    b.W[tag] = WireBuilder::CompositeTag((int)objects.size(), 1, 8);
    b.W[body + 4] = WireBuilder::StructListPtr(body + 4, tag, (int)objects.size() * 9);
    b.W[0] = ((uint64_t)2 << 32) | ((uint64_t)3 << 48); // root: data2 ptr3, off 0

    for (size_t oi = 0; oi < objects.size(); oi++)
    {
        const ObjectDecl& o = objects[oi];
        int w = tag + 1 + (int)oi * 9;
        b.SetU16(w, 0, o.Type);
        if (o.Primary) b.SetU16(w, 2, 1); // primary bit 16
        int nameAt = b.Text(o.Name);
        b.W[w + 1] = WireBuilder::TextPtr(w + 1, nameAt, WireBuilder::TextBytes(o.Name));
        int locAt = b.Text(o.Location);
        b.W[w + 2] = WireBuilder::TextPtr(w + 2, locAt, WireBuilder::TextBytes(o.Location));
        if (!o.TypeLabel.empty())
        {
            int tl = b.Text(o.TypeLabel);
            b.W[w + 3] = WireBuilder::TextPtr(w + 3, tl, WireBuilder::TextBytes(o.TypeLabel));
        }
        int rnAt = b.Text(o.RefPointName);
        int rpAt = b.Alloc(2); // refPoint struct: 0 data, 2 ptr
        b.W[rpAt] = WireBuilder::TextPtr(rpAt, rnAt, WireBuilder::TextBytes(o.RefPointName));
        int rpPos = b.Vec3Word(Vec3{});
        b.W[rpAt + 1] = WireBuilder::StructPtr(rpAt + 1, rpPos, 2, 0);
        b.W[w + 4] = WireBuilder::StructPtr(w + 4, rpAt, 0, 2);
        // w+5 namedPoints, w+6 drivePoints, w+8 values: NULL
        if (!o.Channels.empty())
        {
            int n = (int)o.Channels.size();
            int ltag = b.Alloc(1 + n * 5); // each: 1 data + 4 ptr
            b.W[ltag] = WireBuilder::CompositeTag(n, 1, 4);
            for (int i = 0; i < n; i++)
                b.SetU16(ltag + 1 + i * 5, 0, (uint16_t)i); // ids; range tag next pass
            for (int i = 0; i < n; i++)
            {
                int c = ltag + 1 + i * 5;
                const ChannelDecl& ch = o.Channels[i];
                b.SetU16(c, 2, ch.HasRange ? 1 : 0xFFFF);
                if (ch.HasRange)
                {
                    int r = b.Alloc(2);
                    b.SetF64(r, ch.Min); b.SetF64(r + 1, ch.Max);
                    b.W[c + 3] = WireBuilder::StructPtr(c + 3, r, 2, 0);
                }
                int nAt = b.Text(ch.Name);
                b.W[c + 1] = WireBuilder::TextPtr(c + 1, nAt, WireBuilder::TextBytes(ch.Name));
                int uAt = b.Text(ch.Unit);
                b.W[c + 2] = WireBuilder::TextPtr(c + 2, uAt, WireBuilder::TextBytes(ch.Unit));
                // c+4 description: NULL
            }
            b.W[w + 7] = WireBuilder::StructListPtr(w + 7, ltag, n * 5);
        }
    }
}

inline void BuildMotion(WireBuilder& b, double timestamp,
                        const std::vector<MotionObjectInput>& objects)
{
    b.W.clear();
    b.W.push_back(0);
    int body = b.Alloc(2); // 1 data + 1 ptr
    b.SetF64(body, timestamp);
    int n = (int)objects.size();
    int tag = b.Alloc(1 + n * 7); // each: 1 data + 6 ptr
    b.W[tag] = WireBuilder::CompositeTag(n, 1, 6);
    b.W[body + 1] = WireBuilder::StructListPtr(body + 1, tag, n * 7);
    b.W[0] = ((uint64_t)1 << 32) | ((uint64_t)1 << 48);

    for (int i = 0; i < n; i++)
    {
        const MotionObjectInput& o = objects[i];
        int e = tag + 1 + i * 7;
        if (o.HasBodyDynamics)
        {
            // union discriminant 2 = bodyDynamics; allocation order
            // mirrors the reference stack exactly (locked by the corpus
            // motion-bodydynamics vector): specificForce words, then the
            // 0-data/2-ptr BodyDynamics struct (ptr0 points BACKWARD to
            // specificForce), then angularVelocity, then name/pos/quat
            int sfAt = b.Vec3Word(o.SpecificForce, 0xFFFFFFFFu);
            int bdAt = b.Alloc(2);
            int avAt = b.Vec3Word(o.AngularVelocity, 0);
            b.W[bdAt] = WireBuilder::StructPtr(bdAt, sfAt, 2, 0);
            b.W[bdAt + 1] = WireBuilder::StructPtr(bdAt + 1, avAt, 2, 0);
            b.SetU16(e, 0, 2); // dynamics union: bodyDynamics
            b.W[e + 4] = WireBuilder::StructPtr(e + 4, bdAt, 0, 2);
        }
        else
        {
            b.SetU16(e, 0, 0xFFFF); // dynamics union unset
        }
        int nameAt = b.Text(o.Name);
        b.W[e + 1] = WireBuilder::TextPtr(e + 1, nameAt, WireBuilder::TextBytes(o.Name));
        int posAt = b.Vec3Word(o.Position, o.PositionPad);
        b.W[e + 2] = WireBuilder::StructPtr(e + 2, posAt, 2, 0);
        int qAt = b.QuatWord(o.Orientation);
        b.W[e + 3] = WireBuilder::StructPtr(e + 3, qAt, 2, 0);
        // e+4, e+5: NULL (dynamics variants / drivePoints)
        if (!o.Channels.empty())
        {
            int cn = (int)o.Channels.size();
            int ltag = b.Alloc(1 + cn * 2); // each: 1 data + 1 ptr
            b.W[ltag] = WireBuilder::CompositeTag(cn, 1, 1);
            for (int k = 0; k < cn; k++)
            {
                const ChannelValueInput& cv = o.Channels[k];
                int c = ltag + 1 + k * 2;
                b.SetU16(c, 0, cv.Id);
                b.SetU16(c, 2, cv.IsBool ? 1 : 0);
                if (cv.IsBool) b.SetU32(c, 4, cv.Boolean ? 1u : 0u);
                else b.SetF32(c, 4, (float)cv.Number);
            }
            b.W[e + 6] = WireBuilder::StructListPtr(e + 6, ltag, cn * 2);
        }
    }
}

inline void BuildEventFrame(WireBuilder& b, double timestamp,
                            const std::vector<EventInput>& events)
{
    b.W.clear();
    b.W.push_back(0);
    int body = b.Alloc(2);
    b.SetF64(body, timestamp);
    int n = (int)events.size();
    int tag = b.Alloc(1 + n * 5); // each: 2 data + 3 ptr
    b.W[tag] = WireBuilder::CompositeTag(n, 2, 3);
    b.W[body + 1] = WireBuilder::StructListPtr(body + 1, tag, n * 5);
    b.W[0] = ((uint64_t)1 << 32) | ((uint64_t)1 << 48);

    for (int i = 0; i < n; i++)
    {
        const EventInput& ev = events[i];
        int e = tag + 1 + i * 5;
        b.SetU32(e, 0, ev.Id);
        b.SetF32(e, 4, ev.Intensity);
        b.SetF32(e + 1, 0, ev.Duration);
        // direction union tag at u16 byte 4 of data word 1
        int dirAt = 0, nameAt = 0, objAt = 0;
        if (ev.HasDirection) { dirAt = b.Vec3Word(ev.Direction); }
        nameAt = b.Text(ev.Name);
        objAt = b.Text(ev.Object);
        b.SetU16(e + 1, 4, ev.HasDirection ? 1 : 0xFFFF);
        b.W[e + 2] = WireBuilder::TextPtr(e + 2, nameAt, WireBuilder::TextBytes(ev.Name));
        b.W[e + 3] = ev.HasDirection
            ? WireBuilder::StructPtr(e + 3, dirAt, 2, 0) : 0;
        b.W[e + 4] = WireBuilder::TextPtr(e + 4, objAt, WireBuilder::TextBytes(ev.Object));
    }
}

} // namespace ogsa::telemetry
