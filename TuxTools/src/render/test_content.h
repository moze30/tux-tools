/*
 * Tux-Tools - procedural content for the driver test.
 *
 * Everything the driver test draws is generated here, in code - there is no
 * image asset and no image decoder in the loop. That matters because an asset
 * is decoded by a system component (GDI+ for GIFs) whose behaviour varies
 * between machines, which made a previous version of this test render
 * differently on different devices. Geometry and textures built from scratch
 * depend only on the graphics API being tested.
 *
 * Lives under src/render/ and is shared by the Direct3D 9 / 10 / 11 backends.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace render {

// --- Small row-major, row-vector matrix maths ------------------------------
// Convention: clip = position * WVP, matching HLSL's mul(float4, float4x4)
// with `row_major` matrices.

struct Mat4 {
    float m[16];
};

Mat4 identity();
Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 translation(float x, float y, float z);
Mat4 scaling(float x, float y, float z);
Mat4 rotationX(float radians);
Mat4 rotationY(float radians);
Mat4 rotationZ(float radians);
Mat4 lookAtLH(float ex, float ey, float ez, float tx, float ty, float tz);
Mat4 perspectiveFovLH(float fovY, float aspect, float zn, float zf);

// Left-handed orthographic projection with z mapped into 0..1, matching the
// perspective helper. Used to build the directional light's shadow projection.
Mat4 orthographicLH(float width, float height, float zn, float zf);

// --- Geometry ---------------------------------------------------------------

struct Vertex {
    float pos[3];
    float nrm[3];
    float uv[2];
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    uint32_t indexCount() const { return static_cast<uint32_t>(indices.size()); }
    uint32_t vertexCount() const { return static_cast<uint32_t>(vertices.size()); }
};

// A cube centred on the origin. Each face gets its own vertices so the normals
// stay flat and the UVs can run 0..1 per face.
Mesh makeCube(float halfExtent);

// A flat floor on the XZ plane, subdivided into a grid. `uvTiles` sets how many
// times the texture repeats across the whole plane.
Mesh makeFloor(float halfExtent, int divisions, float uvTiles);

// A cone standing on the origin, apex pointing up along +Y.
Mesh makeCone(float radius, float height, int segments);

// A UV sphere centred on the origin.
Mesh makeSphere(float radius, int slices, int stacks);

// A torus lying in the XZ plane, centred on the origin.
Mesh makeTorus(float majorRadius, float minorRadius, int majorSegments,
               int minorSegments);

// A flat annulus (a ring with no thickness) in the XZ plane, facing +Y. UVs run
// u around the ring and v = 0 at the inner edge to v = 1 at the outer edge.
Mesh makeAnnulus(float innerRadius, float outerRadius, int segments);

// A starfield: `count` quads scattered over a sphere of the given radius, each
// oriented to face the sphere's centre. Put the camera at that centre and every
// star is edge-on to the viewer. Sizes vary; because the camera never moves the
// result is a static mesh - no per-frame rebuild.
Mesh makeStarfield(float radius, int count);

// A single quad in the XY plane facing -Z, centred on the origin, `size` wide.
// With the camera parked on the -Z side this is already edge-on to it, so a
// star drawn from it only needs translating - no billboard rebuild per frame.
Mesh makeBillboard(float size);

// A cylinder centred on the origin, its axis along Y.
Mesh makeCylinder(float radius, float height, int segments);

// A cylinder whose top and bottom rims are chamfered by `chamfer` along both
// axes. The chamfer gives the end caps a bright, narrow band that reads as a
// machined edge under a specular highlight, so the cap-to-wall junction does
// not look like two unrelated planes meeting at a hard corner.
Mesh makeBeveledCylinder(float radius, float height, float chamfer, int segments);

// Deterministic pseudo-random in 0..1, fixed seed, so anything built from it
// looks the same on every run. Exposed because the backends generate their own
// scatter patterns (particle discs) and should be reproducible too.
float nextRandom();

// Appends `src` to `dst` with `transform` applied. Lets a compound object - a
// space station, say - be assembled once at load time and drawn in a single
// call instead of one call per part.
Mesh& appendMesh(Mesh& dst, const Mesh& src, const Mat4& transform);

// --- Textures ---------------------------------------------------------------

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // tightly packed RGBA8, row major
};

// Packs 8-bit channels into the word layout the texture generators take, so
// call sites can read as colours instead of hex.
inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

// RGBA byte order is deliberate: it is what every backend can upload without
// channel swizzling, and it is produced here rather than by an image decoder.
Texture makeCheckerTexture(int size, int cells, uint32_t colorA, uint32_t colorB);
Texture makeRingTexture(int size, uint32_t inner, uint32_t outer);

// Calm alternatives to the two patterned textures above: a smooth top-to-bottom
// gradient and a smooth radial falloff. Neither repeats, so there is no pattern
// for the eye to lock onto. `atV0` / `centre` are the v = 0 / middle colours.
Texture makeVerticalGradient(int size, uint32_t atV0, uint32_t atV1);
Texture makeRadialGlow(int size, uint32_t centre, uint32_t edge);

// Shading for a torus tube: bright where the tube faces up, dark underneath.
// The falloff is a cosine, so it is periodic in v and the tube shows no seam.
// Constant along u.
Texture makeTubeGradient(int size, uint32_t bright, uint32_t dark);

// A banded gas-giant surface: latitudinal belts across v with soft edges, plus
// an oval storm. Bands are constant along u, so the sphere has no seam.
Texture makePlanetTexture(int size);

// Radial banding for a planet ring: v runs across the ring from the inner edge
// to the outer edge. Alpha varies too, so the ring reads as translucent.
Texture makePlanetRingTexture(int size);

// Banded gas giant in pale butterscotch and cream, with a faint blue-grey
// polar hood. Deliberately lower contrast than the Jupiter texture so the two
// planets stay distinguishable at a glance.
Texture makeSaturnPlanetTexture(int size);

// Saturn's rings. v runs from the inner edge to the outer edge and carries the
// radial structure - the C and B rings, the Cassini division, the A ring and
// the narrow Encke gap - while u carries a slow azimuthal variation so the
// rings are visibly turning instead of being rotationally symmetric.
Texture makeSaturnRingTexture(int size);

// A plain bright dot: solid white to the rim with a hair of anti-aliasing, and
// black outside. Meant to be drawn with additive blending, so the black
// surround contributes nothing and the star reads as a point of light rather
// than a glowing blob.
Texture makeStarSprite(int size);

}  // namespace render