#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace edf::native {
// Guest immediate vertex spans, converted to host bytes.
//
// This is guest logic, not API logic: big-endian float words, a quad layout
// that has to be expanded into triangles, and bounds that must be checked
// before anything is allocated or a buffer is touched. A backend needs the
// identical answer, so it lives here once rather than being written a second
// time against a second API - the same reason the render-state decode does.
//
// Every function validates completely before converting anything, so a
// rejected span leaves no half-written buffer behind.

// POSITION0 float2 triangles, 24 bytes per triangle. Throws on a span that is
// empty, not a whole number of triangles, larger than the cap, or contains a
// non-finite float. Returns host bytes at 8 bytes per vertex.
std::vector<uint8_t> ConvertGuestPositionTriangles(std::span<const uint8_t> guest);

// POSITION0 float2 + TEXCOORD0 float2, 16 bytes per vertex. A strip is passed
// through in order; otherwise the span is four-vertex quads, expanded to
// triangle lists as 0,1,2, 0,2,3. Throws on a span that is empty, larger than
// the cap, or the wrong multiple for the topology.
//
// Deliberately does not check for non-finite floats, because the code this was
// taken from does not: adding the check here would change which draws the
// renderer accepts, which is not a thing a refactor should do quietly.
std::vector<uint8_t> ConvertGuestQuads(std::span<const uint8_t> guest, bool strip);
}  // namespace edf::native
