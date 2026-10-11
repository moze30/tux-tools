#include "test_content.h"

#include <cmath>

namespace render {

namespace {

inline uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(r)) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

void putPixel(Texture& t, int x, int y, uint32_t rgba) {
    const size_t o = (static_cast<size_t>(y) * t.width + x) * 4;
    t.pixels[o + 0] = static_cast<uint8_t>(rgba & 0xFF);
    t.pixels[o + 1] = static_cast<uint8_t>((rgba >> 8) & 0xFF);
    t.pixels[o + 2] = static_cast<uint8_t>((rgba >> 16) & 0xFF);
    t.pixels[o + 3] = static_cast<uint8_t>((rgba >> 24) & 0xFF);
}

float smoothstepf(float e0, float e1, float x) {
    if (e1 == e0) return x < e0 ? 0.0f : 1.0f;
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

uint32_t lerpColor(uint32_t a, uint32_t b, float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    uint8_t out[4];
    for (int i = 0; i < 4; ++i) {
        const int ca = static_cast<int>((a >> (i * 8)) & 0xFF);
        const int cb = static_cast<int>((b >> (i * 8)) & 0xFF);
        out[i] = static_cast<uint8_t>(ca + (cb - ca) * f);
    }
    return static_cast<uint32_t>(out[0]) | (static_cast<uint32_t>(out[1]) << 8) |
           (static_cast<uint32_t>(out[2]) << 16) | (static_cast<uint32_t>(out[3]) << 24);
}

// xorshift state, seeded once, so anything built from it is identical on every
// run. Defined here but the accessor below is public.
uint32_t g_randomState = 0x9E3779B9u;

void addQuad(Mesh& mesh, const Vertex& a, const Vertex& b, const Vertex& c,
             const Vertex& d) {
    const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back(a);
    mesh.vertices.push_back(b);
    mesh.vertices.push_back(c);
    mesh.vertices.push_back(d);
    // Two triangles, counter-clockwise when seen from the front.
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

Vertex makeVertex(float px, float py, float pz, float nx, float ny, float nz,
                  float u, float v) {
    Vertex vtx = {};
    vtx.pos[0] = px; vtx.pos[1] = py; vtx.pos[2] = pz;
    vtx.nrm[0] = nx; vtx.nrm[1] = ny; vtx.nrm[2] = nz;
    vtx.uv[0] = u;   vtx.uv[1] = v;
    return vtx;
}

}  // namespace

// --- Random -----------------------------------------------------------------

float nextRandom() {
    g_randomState ^= g_randomState << 13;
    g_randomState ^= g_randomState >> 17;
    g_randomState ^= g_randomState << 5;
    return static_cast<float>(g_randomState & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

// --- Matrices ---------------------------------------------------------------

Mat4 identity() {
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[i * 4 + k] * b.m[k * 4 + j];
            }
            r.m[i * 4 + j] = sum;
        }
    }
    return r;
}

Mat4 translation(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 scaling(float x, float y, float z) {
    Mat4 r = identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

Mat4 rotationX(float radians) {
    Mat4 r = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    r.m[5] = c;  r.m[6] = s;
    r.m[9] = -s; r.m[10] = c;
    return r;
}

Mat4 rotationY(float radians) {
    Mat4 r = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    r.m[0] = c;  r.m[2] = -s;
    r.m[8] = s;  r.m[10] = c;
    return r;
}

Mat4 rotationZ(float radians) {
    Mat4 r = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    r.m[0] = c;  r.m[1] = s;
    r.m[4] = -s; r.m[5] = c;
    return r;
}

Mat4 lookAtLH(float ex, float ey, float ez, float tx, float ty, float tz) {
    float zx = tx - ex, zy = ty - ey, zz = tz - ez;
    const float zlen = std::sqrt(zx * zx + zy * zy + zz * zz);
    if (zlen > 0.0f) { zx /= zlen; zy /= zlen; zz /= zlen; }

    // Right = cross(up, z) with up = (0, 1, 0).
    float xx = zz, xy = 0.0f, xz = -zx;
    const float xlen = std::sqrt(xx * xx + xy * xy + xz * xz);
    if (xlen > 0.0f) { xx /= xlen; xy /= xlen; xz /= xlen; }

    // Up = cross(z, x).
    const float yx = zy * xz - zz * xy;
    const float yy = zz * xx - zx * xz;
    const float yz = zx * xy - zy * xx;

    Mat4 r = identity();
    r.m[0] = xx; r.m[1] = yx; r.m[2] = zx;  r.m[3] = 0.0f;
    r.m[4] = xy; r.m[5] = yy; r.m[6] = zy;  r.m[7] = 0.0f;
    r.m[8] = xz; r.m[9] = yz; r.m[10] = zz; r.m[11] = 0.0f;
    r.m[12] = -(xx * ex + xy * ey + xz * ez);
    r.m[13] = -(yx * ex + yy * ey + yz * ez);
    r.m[14] = -(zx * ex + zy * ey + zz * ez);
    r.m[15] = 1.0f;
    return r;
}

Mat4 perspectiveFovLH(float fovY, float aspect, float zn, float zf) {
    const float h = 1.0f / std::tan(fovY * 0.5f);
    const float w = h / aspect;
    Mat4 r = {};
    r.m[0] = w;
    r.m[5] = h;
    r.m[10] = zf / (zf - zn);
    r.m[11] = 1.0f;
    r.m[14] = -zn * zf / (zf - zn);
    return r;
}

Mat4 orthographicLH(float width, float height, float zn, float zf) {
    Mat4 r = {};
    r.m[0] = width > 0.0f ? 2.0f / width : 1.0f;
    r.m[5] = height > 0.0f ? 2.0f / height : 1.0f;
    r.m[10] = zf > zn ? 1.0f / (zf - zn) : 1.0f;
    r.m[14] = -(zn / (zf - zn));
    r.m[15] = 1.0f;
    return r;
}

// --- Geometry ---------------------------------------------------------------

Mesh makeCube(float halfExtent) {
    const float h = halfExtent;
    Mesh mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    // +X, -X, +Y, -Y, +Z, -Z
    addQuad(mesh,
            makeVertex( h, -h, -h,  1, 0, 0, 0, 1),
            makeVertex( h,  h, -h,  1, 0, 0, 0, 0),
            makeVertex( h,  h,  h,  1, 0, 0, 1, 0),
            makeVertex( h, -h,  h,  1, 0, 0, 1, 1));
    addQuad(mesh,
            makeVertex(-h, -h,  h, -1, 0, 0, 0, 1),
            makeVertex(-h,  h,  h, -1, 0, 0, 0, 0),
            makeVertex(-h,  h, -h, -1, 0, 0, 1, 0),
            makeVertex(-h, -h, -h, -1, 0, 0, 1, 1));
    addQuad(mesh,
            makeVertex(-h,  h, -h,  0, 1, 0, 0, 1),
            makeVertex(-h,  h,  h,  0, 1, 0, 0, 0),
            makeVertex( h,  h,  h,  0, 1, 0, 1, 0),
            makeVertex( h,  h, -h,  0, 1, 0, 1, 1));
    addQuad(mesh,
            makeVertex(-h, -h,  h,  0, -1, 0, 0, 1),
            makeVertex(-h, -h, -h,  0, -1, 0, 0, 0),
            makeVertex( h, -h, -h,  0, -1, 0, 1, 0),
            makeVertex( h, -h,  h,  0, -1, 0, 1, 1));
    addQuad(mesh,
            makeVertex(-h, -h,  h,  0, 0, 1, 0, 1),
            makeVertex(-h,  h,  h,  0, 0, 1, 0, 0),
            makeVertex( h,  h,  h,  0, 0, 1, 1, 0),
            makeVertex( h, -h,  h,  0, 0, 1, 1, 1));
    addQuad(mesh,
            makeVertex( h, -h, -h,  0, 0, -1, 0, 1),
            makeVertex( h,  h, -h,  0, 0, -1, 0, 0),
            makeVertex(-h,  h, -h,  0, 0, -1, 1, 0),
            makeVertex(-h, -h, -h,  0, 0, -1, 1, 1));
    return mesh;
}

Mesh makeFloor(float halfExtent, int divisions, float uvTiles) {
    if (divisions < 1) divisions = 1;
    Mesh mesh;
    const int n = divisions + 1;
    mesh.vertices.reserve(static_cast<size_t>(n) * n);
    mesh.indices.reserve(static_cast<size_t>(divisions) * divisions * 6);

    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float u = static_cast<float>(i) / divisions;
            const float v = static_cast<float>(j) / divisions;
            mesh.vertices.push_back(makeVertex(
                (u * 2.0f - 1.0f) * halfExtent, 0.0f,
                (v * 2.0f - 1.0f) * halfExtent,
                0.0f, 1.0f, 0.0f,
                u * uvTiles, v * uvTiles));
        }
    }
    for (int j = 0; j < divisions; ++j) {
        for (int i = 0; i < divisions; ++i) {
            const uint16_t a = static_cast<uint16_t>(j * n + i);
            const uint16_t b = static_cast<uint16_t>(j * n + i + 1);
            const uint16_t c = static_cast<uint16_t>((j + 1) * n + i + 1);
            const uint16_t d = static_cast<uint16_t>((j + 1) * n + i);
            // Wound clockwise seen from above, matching the cube's +Y face, so
            // the floor survives back-face culling.
            mesh.indices.push_back(a);
            mesh.indices.push_back(d);
            mesh.indices.push_back(c);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
        }
    }
    return mesh;
}

// A triangle is front-facing (clockwise in Direct3D) when
// cross(b - a, c - a) points along the intended outward normal, so every
// generator below is wound to satisfy that.
Mesh makeCone(float radius, float height, int segments) {
    if (segments < 3) segments = 3;
    Mesh mesh;

    for (int i = 0; i < segments; ++i) {
        const float t0 = 2.0f * 3.14159265358979f * i / segments;
        const float t1 = 2.0f * 3.14159265358979f * (i + 1) / segments;
        const float tm = 0.5f * (t0 + t1);

        // Side normal for a cone: the ring contributes `height`, the axis
        // contributes `radius`.
        auto sideNormal = [&](float t) {
            float x = std::cos(t) * height;
            float z = std::sin(t) * height;
            float y = radius;
            const float len = std::sqrt(x * x + y * y + z * z);
            return makeVertex(0, 0, 0, x / len, y / len, z / len, 0, 0);
        };

        // UVs run v = 0 at the base to v = 1 at the apex, so a vertical
        // gradient texture reads as a smooth shade up the side.
        Vertex apex = sideNormal(tm);
        apex.pos[0] = 0.0f;
        apex.pos[1] = height;
        apex.pos[2] = 0.0f;
        apex.uv[0] = 0.5f;
        apex.uv[1] = 1.0f;

        const float u0 = static_cast<float>(i) / segments;
        const float u1 = static_cast<float>(i + 1) / segments;

        Vertex r0 = sideNormal(t0);
        r0.pos[0] = std::cos(t0) * radius;
        r0.pos[1] = 0.0f;
        r0.pos[2] = std::sin(t0) * radius;
        r0.uv[0] = u0;
        r0.uv[1] = 0.0f;

        Vertex r1 = sideNormal(t1);
        r1.pos[0] = std::cos(t1) * radius;
        r1.pos[1] = 0.0f;
        r1.pos[2] = std::sin(t1) * radius;
        r1.uv[0] = u1;
        r1.uv[1] = 0.0f;

        // Wound (apex, r1, r0) so the cross product points outwards.
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        mesh.vertices.push_back(apex);
        mesh.vertices.push_back(r1);
        mesh.vertices.push_back(r0);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
    }

    // Base cap, facing down.
    Vertex centre = makeVertex(0, 0, 0, 0, -1, 0, 0.5f, 0.0f);
    const uint16_t centreIndex = static_cast<uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back(centre);
    for (int i = 0; i < segments; ++i) {
        const float t0 = 2.0f * 3.14159265358979f * i / segments;
        const float t1 = 2.0f * 3.14159265358979f * (i + 1) / segments;
        Vertex r0 = makeVertex(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius,
                               0, -1, 0, 0.5f, 0.0f);
        Vertex r1 = makeVertex(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius,
                               0, -1, 0, 0.5f, 0.0f);
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        mesh.vertices.push_back(r0);
        mesh.vertices.push_back(r1);
        mesh.indices.push_back(centreIndex);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
    }
    return mesh;
}

Mesh makeSphere(float radius, int slices, int stacks) {
    if (slices < 3) slices = 3;
    if (stacks < 2) stacks = 2;
    Mesh mesh;
    const float pi = 3.14159265358979f;

    for (int j = 0; j <= stacks; ++j) {
        const float phi = pi * j / stacks;
        for (int i = 0; i <= slices; ++i) {
            const float theta = 2.0f * pi * i / slices;
            const float x = std::sin(phi) * std::cos(theta);
            const float y = std::cos(phi);
            const float z = std::sin(phi) * std::sin(theta);
            mesh.vertices.push_back(makeVertex(x * radius, y * radius, z * radius,
                                               x, y, z,
                                               static_cast<float>(i) / slices,
                                               static_cast<float>(j) / stacks));
        }
    }
    const int n = slices + 1;
    for (int j = 0; j < stacks; ++j) {
        for (int i = 0; i < slices; ++i) {
            const uint16_t a = static_cast<uint16_t>(j * n + i);
            const uint16_t b = static_cast<uint16_t>(j * n + i + 1);
            const uint16_t c = static_cast<uint16_t>((j + 1) * n + i + 1);
            const uint16_t d = static_cast<uint16_t>((j + 1) * n + i);
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }
    return mesh;
}

Mesh makeTorus(float majorRadius, float minorRadius, int majorSegments,
               int minorSegments) {
    if (majorSegments < 3) majorSegments = 3;
    if (minorSegments < 3) minorSegments = 3;
    Mesh mesh;
    const float pi = 3.14159265358979f;

    for (int i = 0; i <= majorSegments; ++i) {
        const float u = 2.0f * pi * i / majorSegments;
        const float cu = std::cos(u);
        const float su = std::sin(u);
        for (int j = 0; j <= minorSegments; ++j) {
            const float v = 2.0f * pi * j / minorSegments;
            const float cv = std::cos(v);
            const float sv = std::sin(v);
            const float nx = cv * cu;
            const float ny = sv;
            const float nz = cv * su;
            mesh.vertices.push_back(makeVertex(
                (majorRadius + minorRadius * cv) * cu,
                minorRadius * sv,
                (majorRadius + minorRadius * cv) * su,
                nx, ny, nz,
                static_cast<float>(i) / majorSegments,
                static_cast<float>(j) / minorSegments));
        }
    }

    const int n = minorSegments + 1;
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            const uint16_t a = static_cast<uint16_t>(i * n + j);
            const uint16_t b = static_cast<uint16_t>(i * n + j + 1);
            const uint16_t c = static_cast<uint16_t>((i + 1) * n + j + 1);
            const uint16_t d = static_cast<uint16_t>((i + 1) * n + j);
            // Wound so cross(b - a, c - a) points outwards, same rule as the
            // sphere and floor.
            mesh.indices.push_back(a);
            mesh.indices.push_back(d);
            mesh.indices.push_back(c);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
        }
    }
    return mesh;
}

Mesh makeAnnulus(float innerRadius, float outerRadius, int segments) {
    if (segments < 3) segments = 3;
    Mesh mesh;
    const float pi = 3.14159265358979f;

    for (int i = 0; i <= segments; ++i) {
        const float a = 2.0f * pi * i / segments;
        const float c = std::cos(a);
        const float s = std::sin(a);
        const float u = static_cast<float>(i) / segments;
        mesh.vertices.push_back(makeVertex(c * innerRadius, 0.0f, s * innerRadius,
                                           0, 1, 0, u, 0.0f));
        mesh.vertices.push_back(makeVertex(c * outerRadius, 0.0f, s * outerRadius,
                                           0, 1, 0, u, 1.0f));
    }
    for (int i = 0; i < segments; ++i) {
        const uint16_t a = static_cast<uint16_t>(i * 2);      // inner edge, i
        const uint16_t b = static_cast<uint16_t>(i * 2 + 1);  // outer edge, i
        const uint16_t c = static_cast<uint16_t>(i * 2 + 2);  // inner edge, i+1
        const uint16_t d = static_cast<uint16_t>(i * 2 + 3);  // outer edge, i+1
        // Wound so cross(b - a, c - a) points along +Y, the surface normal.
        mesh.indices.push_back(a);
        mesh.indices.push_back(c);
        mesh.indices.push_back(b);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
        mesh.indices.push_back(d);
    }
    return mesh;
}

Mesh makeStarfield(float radius, int count) {
    if (count < 1) count = 1;
    Mesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(count) * 4);
    mesh.indices.reserve(static_cast<size_t>(count) * 6);
    const float pi = 3.14159265358979f;

    for (int i = 0; i < count; ++i) {
        // Uniform direction on the sphere.
        const float z = nextRandom() * 2.0f - 1.0f;
        const float a = nextRandom() * 2.0f * pi;
        const float ring = std::sqrt(1.0f - z * z);
        const float dx = ring * std::cos(a);
        const float dy = z;
        const float dz = ring * std::sin(a);

        // Perpendicular basis for a quad facing the sphere's centre, which is
        // where the camera sits, so each star is edge-on to the viewer.
        float rx = -dz, ry = 0.0f, rz = dx;
        const float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
        if (rlen > 0.0f) { rx /= rlen; ry /= rlen; rz /= rlen; }
        const float ux = dy * rz - dz * ry;
        const float uy = dz * rx - dx * rz;
        const float uz = dx * ry - dy * rx;

        // Most stars small, a handful noticeably bigger: cubed bias.
        const float b = nextRandom();
        const float half = 0.30f + 1.60f * b * b * b;
        const float px = dx * radius, py = dy * radius, pz = dz * radius;

        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        const float ox[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const float oy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        for (int c = 0; c < 4; ++c) {
            const float s = half;
            const float cx = px + (rx * ox[c] + ux * oy[c]) * s;
            const float cy = py + (ry * ox[c] + uy * oy[c]) * s;
            const float cz = pz + (rz * ox[c] + uz * oy[c]) * s;
            mesh.vertices.push_back(makeVertex(cx, cy, cz, -dx, -dy, -dz,
                                               (ox[c] + 1.0f) * 0.5f,
                                               (1.0f - oy[c]) * 0.5f));
        }
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
    return mesh;
}

// A quad in the XY plane, normal pointing at -Z. Wound so
// cross(b - a, c - a) = -Z, matching the camera parked on that side.
Mesh makeBillboard(float size) {
    const float h = size * 0.5f;
    Mesh mesh;
    mesh.vertices.push_back(makeVertex(-h, -h, 0.0f, 0, 0, -1, 0, 1));
    mesh.vertices.push_back(makeVertex(-h,  h, 0.0f, 0, 0, -1, 0, 0));
    mesh.vertices.push_back(makeVertex( h,  h, 0.0f, 0, 0, -1, 1, 0));
    mesh.vertices.push_back(makeVertex( h, -h, 0.0f, 0, 0, -1, 1, 1));
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

// A cylinder centred on the origin, axis along Y: side wall plus both caps.
Mesh makeCylinder(float radius, float height, int segments) {
    if (segments < 3) segments = 3;
    Mesh mesh;
    const float pi = 3.14159265358979f;
    const float half = height * 0.5f;

    for (int i = 0; i < segments; ++i) {
        const float t0 = 2.0f * pi * i / segments;
        const float t1 = 2.0f * pi * (i + 1) / segments;
        auto ringVertex = [&](float t, float y) {
            const float c = std::cos(t);
            const float s = std::sin(t);
            return makeVertex(c * radius, y, s * radius, c, 0, s,
                              static_cast<float>(t) / (2.0f * pi), (y > 0.0f) ? 0.0f : 1.0f);
        };
        const Vertex a = ringVertex(t0, -half);
        const Vertex b = ringVertex(t1, -half);
        const Vertex c = ringVertex(t1, half);
        const Vertex d = ringVertex(t0, half);
        // Wound so cross(b - a, c - a) points outwards along the radius.
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        mesh.vertices.push_back(a);
        mesh.vertices.push_back(d);
        mesh.vertices.push_back(c);
        mesh.vertices.push_back(b);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    // Caps. The top faces +Y, the bottom -Y, so they are wound oppositely.
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? half : -half;
        const float ny = cap == 0 ? 1.0f : -1.0f;
        const uint16_t centre = static_cast<uint16_t>(mesh.vertices.size());
        mesh.vertices.push_back(makeVertex(0, y, 0, 0, ny, 0, 0.5f, 0.5f));
        for (int i = 0; i < segments; ++i) {
            const float t0 = 2.0f * pi * i / segments;
            const float t1 = 2.0f * pi * (i + 1) / segments;
            Vertex r0 = makeVertex(std::cos(t0) * radius, y, std::sin(t0) * radius,
                                   0, ny, 0, 0.5f, 0.5f);
            Vertex r1 = makeVertex(std::cos(t1) * radius, y, std::sin(t1) * radius,
                                   0, ny, 0, 0.5f, 0.5f);
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
            mesh.vertices.push_back(r0);
            mesh.vertices.push_back(r1);
            // Ordering flips with the normal so both caps face outwards.
            if (cap == 0) {
                mesh.indices.push_back(centre);
                mesh.indices.push_back(base + 1);
                mesh.indices.push_back(base + 0);
            } else {
                mesh.indices.push_back(centre);
                mesh.indices.push_back(base + 0);
                mesh.indices.push_back(base + 1);
            }
        }
    }
    return mesh;
}

Mesh makeBeveledCylinder(float radius, float height, float chamfer, int segments) {
    if (segments < 3) segments = 3;
    if (radius <= 0.0f) radius = 0.001f;
    if (chamfer < 0.0f) chamfer = 0.0f;
    // The chamfer has to fit: never deeper than half the height, never wider
    // than the body it is cutting into.
    if (chamfer > height * 0.5f) chamfer = height * 0.5f;
    if (chamfer > radius * 0.5f) chamfer = radius * 0.5f;
    const float pi = 3.14159265358979f;
    const float half = height * 0.5f;
    const float k = 1.0f / std::sqrt(2.0f);  // a 45 degree chamfer splits evenly

    Mesh mesh;

    // Profile from the bottom up. Each entry is a ring: radial offset, height,
    // and how the surface normal is split between the radial and axial axes.
    struct Ring { float r; float y; float nr; float ny; };
    const Ring rings[4] = {
        {radius - chamfer, -half,         1.0f,  0.0f},
        {radius,           -half + chamfer, k,  -k},
        {radius,            half - chamfer, k,   k},
        {radius - chamfer,  half,         1.0f,  0.0f},
    };
    const int ringCount = 4;

    // Side bands, including the two chamfer bands. Wound (lower0, upper0,
    // upper1, lower1) exactly like makeCylinder, so the cross product points
    // outwards along the radius.
    for (int band = 0; band < ringCount - 1; ++band) {
        const Ring& lo = rings[band];
        const Ring& hi = rings[band + 1];
        const float vLo = (lo.y + half) / height;
        const float vHi = (hi.y + half) / height;
        for (int i = 0; i < segments; ++i) {
            const float t0 = 2.0f * pi * i / segments;
            const float t1 = 2.0f * pi * (i + 1) / segments;
            auto ringVertex = [&](float t, const Ring& r, float v) {
                const float cs = std::cos(t);
                const float sn = std::sin(t);
                return makeVertex(cs * r.r, r.y, sn * r.r,
                                  cs * r.nr, r.ny, sn * r.nr,
                                  t / (2.0f * pi), v);
            };
            const Vertex a = ringVertex(t0, lo, vLo);
            const Vertex d = ringVertex(t0, hi, vHi);
            const Vertex cc = ringVertex(t1, hi, vHi);
            const Vertex b = ringVertex(t1, lo, vLo);
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
            mesh.vertices.push_back(a);
            mesh.vertices.push_back(d);
            mesh.vertices.push_back(cc);
            mesh.vertices.push_back(b);
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 3);
        }
    }

    // Caps, set in from the widest radius by the chamfer. Wound oppositely so
    // each one faces outwards.
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? half : -half;
        const float ny = cap == 0 ? 1.0f : -1.0f;
        const float cr = radius - chamfer;
        const uint16_t centre = static_cast<uint16_t>(mesh.vertices.size());
        mesh.vertices.push_back(makeVertex(0, y, 0, 0, ny, 0, 0.5f, 0.5f));
        for (int i = 0; i < segments; ++i) {
            const float t0 = 2.0f * pi * i / segments;
            const float t1 = 2.0f * pi * (i + 1) / segments;
            Vertex r0 = makeVertex(std::cos(t0) * cr, y, std::sin(t0) * cr,
                                   0, ny, 0, 0.5f, 0.5f);
            Vertex r1 = makeVertex(std::cos(t1) * cr, y, std::sin(t1) * cr,
                                   0, ny, 0, 0.5f, 0.5f);
            const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
            mesh.vertices.push_back(r0);
            mesh.vertices.push_back(r1);
            if (cap == 0) {
                mesh.indices.push_back(centre);
                mesh.indices.push_back(base + 1);
                mesh.indices.push_back(base + 0);
            } else {
                mesh.indices.push_back(centre);
                mesh.indices.push_back(base + 0);
                mesh.indices.push_back(base + 1);
            }
        }
    }
    return mesh;
}

Mesh& appendMesh(Mesh& dst, const Mesh& src, const Mat4& transform) {
    const float* m = transform.m;
    const uint16_t base = static_cast<uint16_t>(dst.vertices.size());

    for (const Vertex& v : src.vertices) {
        Vertex out = v;
        const float x = v.pos[0], y = v.pos[1], z = v.pos[2];
        out.pos[0] = x * m[0] + y * m[4] + z * m[8] + m[12];
        out.pos[1] = x * m[1] + y * m[5] + z * m[9] + m[13];
        out.pos[2] = x * m[2] + y * m[6] + z * m[10] + m[14];

        const float nx = v.nrm[0], ny = v.nrm[1], nz = v.nrm[2];
        float ox = nx * m[0] + ny * m[4] + nz * m[8];
        float oy = nx * m[1] + ny * m[5] + nz * m[9];
        float oz = nx * m[2] + ny * m[6] + nz * m[10];
        const float len = std::sqrt(ox * ox + oy * oy + oz * oz);
        if (len > 0.0f) { ox /= len; oy /= len; oz /= len; }
        out.nrm[0] = ox;
        out.nrm[1] = oy;
        out.nrm[2] = oz;

        dst.vertices.push_back(out);
    }
    for (uint16_t index : src.indices) {
        dst.indices.push_back(static_cast<uint16_t>(base + index));
    }
    return dst;
}

// --- Textures ---------------------------------------------------------------

Texture makeCheckerTexture(int size, int cells, uint32_t colorA, uint32_t colorB) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    if (cells < 1) cells = 1;
    const int cellSize = size / cells;
    if (cellSize < 1) return t;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool odd = ((x / cellSize) + (y / cellSize)) & 1;
            putPixel(t, x, y, odd ? colorB : colorA);
        }
    }
    // Mark the borders so the texture's orientation is obvious on screen.
    const uint32_t edge = packRgba(0x44, 0x8A, 0xFF, 0xFF);
    for (int i = 0; i < size; ++i) {
        putPixel(t, i, 0, edge);
        putPixel(t, i, size - 1, edge);
        putPixel(t, 0, i, edge);
        putPixel(t, size - 1, i, edge);
    }
    return t;
}

Texture makeRingTexture(int size, uint32_t inner, uint32_t outer) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    const float centre = (size - 1) * 0.5f;
    const float maxR = centre;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = x - centre;
            const float dy = y - centre;
            const float r = std::sqrt(dx * dx + dy * dy) / maxR;
            // Alternating bands: shows up mip selection and linear filtering.
            const int band = static_cast<int>(r * 8.0f);
            putPixel(t, x, y, (band & 1) ? inner : outer);
        }
    }
    return t;
}

Texture makeVerticalGradient(int size, uint32_t atV0, uint32_t atV1) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    const float span = static_cast<float>(size > 1 ? size - 1 : 1);

    for (int y = 0; y < size; ++y) {
        const uint32_t colour = lerpColor(atV0, atV1, y / span);
        for (int x = 0; x < size; ++x) {
            putPixel(t, x, y, colour);
        }
    }
    return t;
}

Texture makeRadialGlow(int size, uint32_t centre, uint32_t edge) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    const float mid = (size - 1) * 0.5f;
    const float maxR = mid > 0.0f ? mid : 1.0f;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (x - mid) / maxR;
            const float dy = (y - mid) / maxR;
            float r = std::sqrt(dx * dx + dy * dy);
            if (r > 1.0f) r = 1.0f;
            // Smoothstep instead of bands: the falloff has no edges for the eye
            // to catch on, which is the whole point of this texture.
            const float f = r * r * (3.0f - 2.0f * r);
            putPixel(t, x, y, lerpColor(centre, edge, f));
        }
    }
    return t;
}

Texture makeTubeGradient(int size, uint32_t bright, uint32_t dark) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    const float pi2 = 6.283185307179586f;
    const float span = static_cast<float>(size > 1 ? size - 1 : 1);

    for (int y = 0; y < size; ++y) {
        const float v = y / span;
        // v = 0.25 is the top of the tube, v = 0.75 the bottom. The cosine makes
        // the falloff periodic, so v = 0 and v = 1 match and there is no seam.
        const float f = 0.5f * (1.0f + std::cos(pi2 * (v - 0.25f)));
        const uint32_t colour = lerpColor(dark, bright, f);
        for (int x = 0; x < size; ++x) {
            putPixel(t, x, y, colour);
        }
    }
    return t;
}

Texture makePlanetTexture(int size) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);

    // Belt colours from the north pole down to the south, in order.
    static const uint32_t kBelts[] = {
        rgba(0xB6, 0x9C, 0x78), rgba(0xE9, 0xDA, 0xBA), rgba(0xC0, 0x8E, 0x5A),
        rgba(0xF0, 0xE2, 0xC4), rgba(0xAE, 0x72, 0x40), rgba(0xE6, 0xD5, 0xB2),
        rgba(0xC4, 0x94, 0x62), rgba(0xEF, 0xE0, 0xC0), rgba(0xB2, 0x88, 0x5C),
        rgba(0xDD, 0xC8, 0xA4),
    };
    const int beltCount = static_cast<int>(sizeof(kBelts) / sizeof(kBelts[0]));
    const uint32_t kStorm = rgba(0xC2, 0x66, 0x3E);
    const float stormU = 0.33f, stormV = 0.62f;
    const float stormRu = 0.075f, stormRv = 0.045f;

    const float span = static_cast<float>(size > 1 ? size - 1 : 1);
    for (int y = 0; y < size; ++y) {
        const float v = y / span;

        const float beltPos = v * beltCount;
        int belt = static_cast<int>(beltPos);
        if (belt >= beltCount) belt = beltCount - 1;
        // Flat inside a belt, blended across the middle of the seam: the belts
        // read clearly without the hard edges that make a texture tiring.
        float f = (beltPos - belt - 0.35f) / 0.30f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        f = f * f * (3.0f - 2.0f * f);
        const uint32_t beltColour =
            lerpColor(kBelts[belt], kBelts[(belt + 1) % beltCount], f);

        for (int x = 0; x < size; ++x) {
            const float u = x / span;
            uint32_t colour = beltColour;

            // The oval storm, wrapped in u so it never straddles the seam.
            float du = u - stormU;
            if (du > 0.5f) du -= 1.0f;
            if (du < -0.5f) du += 1.0f;
            const float dv = v - stormV;
            const float d = std::sqrt((du / stormRu) * (du / stormRu) +
                                      (dv / stormRv) * (dv / stormRv));
            if (d < 1.0f) {
                float w = 1.0f - d;
                w = w * w * (3.0f - 2.0f * w);
                colour = lerpColor(colour, kStorm, w);
            }
            putPixel(t, x, y, colour);
        }
    }
    return t;
}

Texture makePlanetRingTexture(int size) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);

    // Brightness from the inner edge of the ring to the outer edge.
    static const float kProfile[] = {0.10f, 0.78f, 0.34f, 0.96f, 0.55f,
                                     1.00f, 0.26f, 0.82f, 0.14f};
    const int steps = static_cast<int>(sizeof(kProfile) / sizeof(kProfile[0]));
    const float pi2 = 6.283185307179586f;
    const float span = static_cast<float>(size > 1 ? size - 1 : 1);

    for (int y = 0; y < size; ++y) {
        const float v = y / span;
        const float pos = v * (steps - 1);
        int i = static_cast<int>(pos);
        if (i >= steps - 1) i = steps - 2;
        const float f = pos - i;
        const float b = kProfile[i] + (kProfile[i + 1] - kProfile[i]) * f;
        const float alpha = 0.5f + 0.5f * b;  // the faint bands stay see-through

        for (int x = 0; x < size; ++x) {
            const float u = x / span;
            // Two smooth bright arcs, so the ring visibly turns.
            const float arc = 0.78f + 0.22f * std::cos(pi2 * u * 2.0f);
            const float level = b * arc;
            putPixel(t, x, y,
                     rgba(static_cast<uint8_t>(255.0f * level),
                          static_cast<uint8_t>(255.0f * level * 0.88f),
                          static_cast<uint8_t>(255.0f * level * 0.66f),
                          static_cast<uint8_t>(255.0f * alpha)));
        }
    }
    return t;
}

Texture makeSaturnPlanetTexture(int size) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);

    // Pale, low-contrast belts. Saturn reads as washed butterscotch next to
    // Jupiter's strong rust and cream, which is the whole point of the palette.
    static const uint32_t kBelts[] = {
        rgba(0xE7, 0xD9, 0xBB), rgba(0xDD, 0xCA, 0xA4), rgba(0xF1, 0xE7, 0xD0),
        rgba(0xD6, 0xC0, 0x97), rgba(0xEC, 0xE0, 0xC4), rgba(0xD0, 0xB8, 0x8E),
        rgba(0xE9, 0xDC, 0xBF), rgba(0xDB, 0xC7, 0xA2), rgba(0xF2, 0xE9, 0xD4),
        rgba(0xD4, 0xBD, 0x94),
    };
    const int beltCount = static_cast<int>(sizeof(kBelts) / sizeof(kBelts[0]));
    const uint32_t kPolar = rgba(0x9E, 0xB4, 0xC8);
    const float span = static_cast<float>(size > 1 ? size - 1 : 1);

    for (int y = 0; y < size; ++y) {
        const float v = y / span;

        const float beltPos = v * beltCount;
        int belt = static_cast<int>(beltPos);
        if (belt >= beltCount) belt = beltCount - 1;
        // Softer blend window than Jupiter's: Saturn's belts have gentler edges.
        float f = (beltPos - belt - 0.30f) / 0.40f;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        f = f * f * (3.0f - 2.0f * f);
        uint32_t colour = lerpColor(kBelts[belt], kBelts[(belt + 1) % beltCount], f);

        // A faint blue-grey hood over both poles.
        const float pole = smoothstepf(0.14f, 0.0f, v) + smoothstepf(0.86f, 1.0f, v);
        colour = lerpColor(colour, kPolar, 0.30f * (pole > 1.0f ? 1.0f : pole));

        for (int x = 0; x < size; ++x) {
            putPixel(t, x, y, colour);
        }
    }
    return t;
}

Texture makeSaturnRingTexture(int size) {
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);

    // Radial density from the inner edge (v = 0) to the outer edge (v = 1).
    // The Cassini division at v ~ 0.645 is the deep one; Encke at ~0.885 is the
    // narrow one. Sample the profile linearly between control points.
    static const float kV[] = {0.000f, 0.060f, 0.100f, 0.300f, 0.340f, 0.600f,
                               0.645f, 0.700f, 0.875f, 0.895f, 0.955f, 1.000f};
    static const float kD[] = {0.10f, 0.24f, 0.42f, 0.50f, 0.96f, 0.92f,
                               0.05f, 0.74f, 0.70f, 0.14f, 0.55f, 0.00f};
    const int steps = static_cast<int>(sizeof(kV) / sizeof(kV[0]));
    const float pi2 = 6.283185307179586f;
    const float span = static_cast<float>(size > 1 ? size - 1 : 1);

    for (int y = 0; y < size; ++y) {
        const float v = y / span;
        float density;
        if (v <= kV[0]) {
            density = kD[0];
        } else if (v >= kV[steps - 1]) {
            density = kD[steps - 1];
        } else {
            int i = 0;
            while (i < steps - 2 && v > kV[i + 1]) ++i;
            const float f = (v - kV[i]) / (kV[i + 1] - kV[i]);
            density = kD[i] + (kD[i + 1] - kD[i]) * f;
        }
        // Fine ringlets, shallow enough that they do not read as a pattern.
        const float band = 0.90f + 0.10f * std::cos(pi2 * v * 26.0f);
        const float shaped = density * band;

        for (int x = 0; x < size; ++x) {
            const float u = x / span;
            // Slow azimuthal lobes: without them the rotation of a ring is
            // invisible, since every radius looks the same.
            const float lobe = 0.92f + 0.08f * std::cos(pi2 * (u - 0.15f)) +
                               0.05f * std::cos(pi2 * 3.0f * (u - 0.40f));
            float d = shaped * lobe;
            if (d < 0.0f) d = 0.0f;
            if (d > 1.0f) d = 1.0f;
            const uint8_t r = static_cast<uint8_t>(255.0f * d * 0.92f);
            const uint8_t g = static_cast<uint8_t>(255.0f * d * 0.86f);
            const uint8_t b = static_cast<uint8_t>(255.0f * d * 0.72f);
            putPixel(t, x, y, rgba(r, g, b,
                                   static_cast<uint8_t>(255.0f * d * 0.95f)));
        }
    }
    return t;
}

Texture makeStarSprite(int size) {
    // A plain bright dot rather than a glow: solid all the way out to the rim,
    // with only about a texel of ramp so the edge is not jagged. The black
    // surround is what additive blending needs - it adds nothing.
    Texture t;
    t.width = t.height = size;
    t.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    const float mid = (size - 1) * 0.5f;
    const float maxR = mid > 0.0f ? mid : 1.0f;
    const float edge = 1.5f / maxR;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (x - mid) / maxR;
            const float dy = (y - mid) / maxR;
            const float r = std::sqrt(dx * dx + dy * dy);
            float k = (1.0f - r) / edge;
            if (k < 0.0f) k = 0.0f;
            if (k > 1.0f) k = 1.0f;
            const uint8_t level = static_cast<uint8_t>(255.0f * k);
            putPixel(t, x, y, rgba(level, level, level, 255));
        }
    }
    return t;
}

}  // namespace render