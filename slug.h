// Slug Text Rendering Algorithm Implementation
//
// Based on the paper "GPU-Centered Font Rendering Directly from Glyph Outlines"
// by Eric Lengyel published on 2017-06-14
//
// This implementation also refers to the sample HLSL shader code provided
// at: https://github.com/EricLengyel/Slug
//
// By Zimeng Li (@konakona418)

#pragma once

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

// abstraction leak!
#include <external/glad.h>

// raylib seems to embed stbttf but doesn't expose,
// so just instantiate implementation here
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <external/stb_truetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

static constexpr int kBandSplits = 16;

static const char kVertShaderCode[] =
  "#version 430\n"
  "struct VertexData {\n"
  "  vec4 pos;\n"
  "  vec4 tex;\n"
  "  vec4 jac;\n"
  "  vec4 bnd;\n"
  "  vec4 col;\n"
  "};\n"
  "layout(std430, binding = 0) readonly buffer PushData {\n"
  "  VertexData data[4];\n"
  "}\n"
  "pushData;\n"
  "uniform mat4 u_mvp;\n"
  "uniform vec2 u_viewport;\n"
  "out vec2       v_texcoord;\n"
  "flat out vec4  v_banding;\n"
  "flat out ivec4 v_glyph;\n"
  "out vec4       v_color;\n"
  "void SlugUnpack(vec4 tex, vec4 bnd, out vec4 vbnd, out ivec4 vgly) {\n"
  "  uvec2 g = floatBitsToUint(tex.zw);\n"
  "  vgly    = ivec4(int(g.x & 0xFFFFu), int(g.x >> 16u), int(g.y & 0xFFFFu), int(g.y >> 16u));\n"
  "  vbnd    = bnd;\n"
  "}\n"
  "vec2 SlugDilate(vec4 pos, vec4 tex, vec4 jac, vec4 m0, vec4 m1, vec4 m3, vec2 dim, out vec2 vpos) {\n"
  "  vec2  n = normalize(pos.zw);\n"
  "  float s = dot(m3.xy, pos.xy) + m3.w;\n"
  "  float t = dot(m3.xy, n);\n"
  "  float u = (s * dot(m0.xy, n) - t * (dot(m0.xy, pos.xy) + m0.w)) * dim.x;\n"
  "  float v = (s * dot(m1.xy, n) - t * (dot(m1.xy, pos.xy) + m1.w)) * dim.y;\n"
  "  float s2 = s * s;\n"
  "  float st = s * t;\n"
  "  float uv = u * u + v * v;\n"
  "  vec2  d  = pos.zw * (s2 * (st + sqrt(max(uv, 0.0))) / (uv - st * st));\n"
  "  vpos = pos.xy + d;\n"
  "  return vec2(tex.x + dot(d, jac.xy), tex.y + dot(d, jac.zw));\n"
  "}\n"
  "void UnpackPushData(\n"
  "  uint     idx,\n"
  "  out vec4 a_pos,\n"
  "  out vec4 a_tex,\n"
  "  out vec4 a_jac,\n"
  "  out vec4 a_bnd,\n"
  "  out vec4 a_col) {\n"
  "  a_pos = pushData.data[idx].pos;\n"
  "  a_tex = pushData.data[idx].tex;\n"
  "  a_jac = pushData.data[idx].jac;\n"
  "  a_bnd = pushData.data[idx].bnd;\n"
  "  a_col = pushData.data[idx].col;\n"
  "}\n"
  "void main() {\n"
  "  vec2 p;\n"
  "  vec4 m0 = vec4(u_mvp[0][0], u_mvp[1][0], u_mvp[2][0], u_mvp[3][0]);\n"
  "  vec4 m1 = vec4(u_mvp[0][1], u_mvp[1][1], u_mvp[2][1], u_mvp[3][1]);\n"
  "  vec4 m2 = vec4(u_mvp[0][2], u_mvp[1][2], u_mvp[2][2], u_mvp[3][2]);\n"
  "  vec4 m3 = vec4(u_mvp[0][3], u_mvp[1][3], u_mvp[2][3], u_mvp[3][3]);\n"
  "  uint idx = gl_VertexID;\n"
  "  vec4 a_pos, a_tex, a_jac, a_bnd, a_col;\n"
  "  UnpackPushData(idx, a_pos, a_tex, a_jac, a_bnd, a_col);\n"
  "  v_texcoord = SlugDilate(a_pos, a_tex, a_jac, m0, m1, m3, u_viewport, p);\n"
  "  gl_Position.x = p.x * m0.x + p.y * m0.y + m0.w;\n"
  "  gl_Position.y = p.x * m1.x + p.y * m1.y + m1.w;\n"
  "  gl_Position.z = p.x * m2.x + p.y * m2.y + m2.w;\n"
  "  gl_Position.w = p.x * m3.x + p.y * m3.y + m3.w;\n"
  "  SlugUnpack(a_tex, a_bnd, v_banding, v_glyph);\n"
  "  v_color = a_col;\n"
  "}\n";

static const char kFragShaderCode[] =
  "#version 430\n"
  "#define kBandSplits 16u\n"
  "in vec2       v_texcoord;\n"
  "flat in vec4  v_banding;\n"
  "flat in ivec4 v_glyph;\n"
  "in vec4       v_color;\n"
  "out vec4 fragColor;\n"
  "struct PackedCurve {\n"
  "  float x0, y0, x1, y1, x2, y2;\n"
  "  uint  _padding[2];\n"
  "};\n"
  "struct PackedBandMeta {\n"
  "  uint dataOffset;\n"
  "  uint nCurves;\n"
  "};\n"
  "layout(std430, binding = 1) readonly buffer CurveBuffer {\n"
  "  PackedCurve curves[];\n"
  "};\n"
  "layout(std430, binding = 2) readonly buffer BandBuffer {\n"
  "  uint rawData[];\n"
  "};\n"
  "uint CalcRootCode(float y1, float y2, float y3) {\n"
  "  uint i1    = floatBitsToUint(y1) >> 31u;\n"
  "  uint i2    = floatBitsToUint(y2) >> 30u;\n"
  "  uint i3    = floatBitsToUint(y3) >> 29u;\n"
  "  uint shift = (i2 & 2u) | (i1 & ~2u);\n"
  "  shift      = (i3 & 4u) | (shift & ~4u);\n"
  "  return ((0x2E74u >> shift) & 0x0101u);\n"
  "}\n"
  "vec2 SolveHorizPoly(vec4 p12, vec2 p3) {\n"
  "  vec2  a  = p12.xy - p12.zw * 2.0 + p3;\n"
  "  vec2  b  = p12.xy - p12.zw;\n"
  "  float ra = 1.0 / a.y;\n"
  "  float rb = 0.5 / b.y;\n"
  "  float d  = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));\n"
  "  float t1 = (b.y - d) * ra;\n"
  "  float t2 = (b.y + d) * ra;\n"
  "  if (abs(a.y) < 1.0 / 65536.0) t1 = t2 = p12.y * rb;\n"
  "  return vec2((a.x * t1 - b.x * 2.0) * t1 + p12.x, (a.x * t2 - b.x * 2.0) * t2 + p12.x);\n"
  "}\n"
  "vec2 SolveVertPoly(vec4 p12, vec2 p3) {\n"
  "  vec2  a  = p12.xy - p12.zw * 2.0 + p3;\n"
  "  vec2  b  = p12.xy - p12.zw;\n"
  "  float ra = 1.0 / a.x;\n"
  "  float rb = 0.5 / b.x;\n"
  "  float d  = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));\n"
  "  float t1 = (b.x - d) * ra;\n"
  "  float t2 = (b.x + d) * ra;\n"
  "  if (abs(a.x) < 1.0 / 65536.0) t1 = t2 = p12.x * rb;\n"
  "  return vec2((a.y * t1 - b.y * 2.0) * t1 + p12.y, (a.y * t2 - b.y * 2.0) * t2 + p12.y);\n"
  "}\n"
  "float CalcCoverage(float xcov, float ycov, float xwgt, float ywgt, int flags) {\n"
  "  float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));\n"
  "  return clamp(coverage, 0.0, 1.0);\n"
  "}\n"
  "void main() {\n"
  "  vec2  pixelsPerEm = 1.0 / fwidth(v_texcoord);\n"
  "  ivec2 bandMax     = ivec2(v_glyph.z & 0x00FF, (v_glyph.z >> 8) & 0x00FF);\n"
  "  ivec2 bandIndex = clamp(ivec2(v_texcoord * v_banding.xy + v_banding.zw), ivec2(0), bandMax);\n"
  "  uint  glyphBase = uint(v_glyph.x);// glyph band data offset\n"
  "  uint hMetaIdx    = glyphBase + (kBandSplits + uint(bandIndex.y)) * 2u;\n"
  "  uint hCount      = rawData[hMetaIdx + 1u];\n"
  "  uint hDataOffset = glyphBase + rawData[hMetaIdx];\n"
  "  float xcov = 0.0, xwgt = 0.0;\n"
  "  for (uint i = 0u; i < hCount; i++) {\n"
  "    uint        curveIdx = v_glyph.y + rawData[hDataOffset + i];\n"
  "    PackedCurve c        = curves[curveIdx];\n"
  "    vec4        p12      = vec4(c.x0, c.y0, c.x1, c.y1) - v_texcoord.xyxy;\n"
  "    vec2        p3       = vec2(c.x2, c.y2) - v_texcoord;\n"
  "    if (max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5) break;\n"
  "    uint code = CalcRootCode(p12.y, p12.w, p3.y);\n"
  "    if (code != 0u) {\n"
  "      vec2 r = SolveHorizPoly(p12, p3) * pixelsPerEm.x;\n"
  "      if ((code & 1u) != 0u) {\n"
  "        xcov += clamp(r.x + 0.5, 0.0, 1.0);\n"
  "        xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));\n"
  "      }\n"
  "      if (code > 1u) {\n"
  "        xcov -= clamp(r.y + 0.5, 0.0, 1.0);\n"
  "        xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));\n"
  "      }\n"
  "    }\n"
  "  }\n"
  "  uint vMetaIdx    = glyphBase + uint(bandIndex.x) * 2u;\n"
  "  uint vCount      = rawData[vMetaIdx + 1u];\n"
  "  uint vDataOffset = glyphBase + rawData[vMetaIdx];\n"
  "  float ycov = 0.0, ywgt = 0.0;\n"
  "  for (uint i = 0u; i < vCount; i++) {\n"
  "    uint        curveIdx = v_glyph.y + rawData[vDataOffset + i];\n"
  "    PackedCurve c        = curves[curveIdx];\n"
  "    vec4        p12      = vec4(c.x0, c.y0, c.x1, c.y1) - v_texcoord.xyxy;\n"
  "    vec2        p3       = vec2(c.x2, c.y2) - v_texcoord;\n"
  "    if (max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5) break;\n"
  "    uint code = CalcRootCode(p12.x, p12.z, p3.x);\n"
  "    if (code != 0u) {\n"
  "      vec2 r = SolveVertPoly(p12, p3) * pixelsPerEm.y;\n"
  "      if ((code & 1u) != 0u) {\n"
  "        ycov -= clamp(r.x + 0.5, 0.0, 1.0);\n"
  "        ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));\n"
  "      }\n"
  "      if (code > 1u) {\n"
  "        ycov += clamp(r.y + 0.5, 0.0, 1.0);\n"
  "        ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));\n"
  "      }\n"
  "    }\n"
  "  }\n"
  "  fragColor = v_color * CalcCoverage(xcov, ycov, xwgt, ywgt, v_glyph.w);\n"
  "}\n";

struct SlugFont {
  struct BCurve {
    Vector2 from;
    Vector2 control;
    Vector2 to;
  };

  struct Band {
    std::vector<int> curveIndices;

    void SortIndicesBasedOnMaxPos(const std::vector<BCurve>& curves, const bool vertical) {
      std::sort(curveIndices.begin(), curveIndices.end(), [&](const int a, const int b) {
        const auto& cA = curves[a];
        const auto& cB = curves[b];

        float maxA =
          vertical
            ? std::max({cA.from.y, cA.control.y, cA.to.y})
            : std::max({cA.from.x, cA.control.x, cA.to.x});
        float maxB =
          vertical
            ? std::max({cB.from.y, cB.control.y, cB.to.y})
            : std::max({cB.from.x, cB.control.x, cB.to.x});

        return maxA > maxB;
      });
    }

    Band() = default;

    Band(std::vector<BCurve>& curves, bool vertical, float minB, float maxB) {
      for (int i = 0; i < (int) curves.size(); ++i) {
        const auto& c = curves[i];

        float cMin =
          vertical
            ? std::min({c.from.x, c.control.x, c.to.x})
            : std::min({c.from.y, c.control.y, c.to.y});
        float cMax =
          vertical
            ? std::max({c.from.x, c.control.x, c.to.x})
            : std::max({c.from.y, c.control.y, c.to.y});

        if (cMax >= minB && cMin <= maxB) {
          curveIndices.push_back(i);
        }
      }
      SortIndicesBasedOnMaxPos(curves, vertical);
    }
  };

  struct Glyph {
    int                 codepoint;
    Rectangle           bounds;
    std::vector<BCurve> curves;
    int                 advanceWidth;
    int                 leftSideBearing;

    std::array<Band, kBandSplits> bandV;
    std::array<Band, kBandSplits> bandH;

    float bandOffsetV, bandScaleV;
    float bandOffsetH, bandScaleH;

    Glyph(stbtt_fontinfo& fontInfo, const int codepoint) {
      this->codepoint = codepoint;

      stbtt_GetCodepointHMetrics(&fontInfo, codepoint, &advanceWidth, &leftSideBearing);

      int x0, y0, x1, y1;
      stbtt_GetCodepointBox(&fontInfo, codepoint, &x0, &y0, &x1, &y1);
      bounds = {
        static_cast<float>(x0),
        static_cast<float>(y0),
        static_cast<float>(x1 - x0),
        static_cast<float>(y1 - y0),
      };

      stbtt_vertex* vertices;

      int nCurves = 0;
      nCurves     = stbtt_GetCodepointShape(&fontInfo, codepoint, &vertices);

      int lastX = 0;
      int lastY = 0;
      for (int i = 0; i < nCurves; ++i) {
        const auto& v = vertices[i];

        if (v.type == STBTT_vmove) {
          lastX = v.x;
          lastY = v.y;
        } else if (v.type == STBTT_vline) {
          // treat lines as a quadratic bezier with control point = from point
          // don't use center point as control point. causes random artifacts
          BCurve curve;
          curve.from    = {static_cast<float>(lastX), static_cast<float>(lastY)};
          curve.control = {static_cast<float>(lastX), static_cast<float>(lastY)};
          curve.to      = {static_cast<float>(v.x), static_cast<float>(v.y)};

          lastX = v.x;
          lastY = v.y;

          curves.push_back(curve);
          //TraceLog(LOG_INFO, "Line Curve: From (%.2f, %.2f) To (%.2f, %.2f)", curve.from.x, curve.from.y, curve.to.x, curve.to.y);
        } else if (v.type == STBTT_vcurve) {
          BCurve curve;
          curve.from    = {static_cast<float>(lastX), static_cast<float>(lastY)};
          curve.control = {static_cast<float>(v.cx), static_cast<float>(v.cy)};
          curve.to      = {static_cast<float>(v.x), static_cast<float>(v.y)};

          lastX = v.x;
          lastY = v.y;

          curves.push_back(curve);
          //TraceLog(LOG_INFO, "Bezier Curve: From (%.2f, %.2f) Control (%.2f, %.2f) To (%.2f, %.2f)", curve.from.x, curve.from.y, curve.control.x, curve.control.y, curve.to.x, curve.to.y);
        }
      }

      bandScaleV  = (float) kBandSplits / bounds.width;
      bandOffsetV = -bounds.x * bandScaleV;

      bandScaleH  = (float) kBandSplits / bounds.height;
      bandOffsetH = -bounds.y * bandScaleH;

      for (int i = 0; i < kBandSplits; ++i) {
        float xMin = bounds.x + (bounds.width * i) / (float) kBandSplits;
        float xMax = bounds.x + (bounds.width * (i + 1)) / (float) kBandSplits;
        bandV[i]   = Band(curves, true, xMin, xMax);

        float yMin = bounds.y + (bounds.height * i) / (float) kBandSplits;
        float yMax = bounds.y + (bounds.height * (i + 1)) / (float) kBandSplits;
        bandH[i]   = Band(curves, false, yMin, yMax);
      }

      stbtt_FreeShape(&fontInfo, vertices);
    }
  };

  struct PackedGlyphData {
    // [GlyphMeta, Curve0, Curve1, ...] [GlyphMeta, Curve0, Curve1, ...] ...
    std::vector<uint8_t> curveData;
    // [BandMetaV0, BandMetaV1, ... BandMetaH0, BandMetaH1, ...] [CurveIdx0, CurveIdx1, ...] [CurveIdx0, CurveIdx1, ...] ...
    std::vector<uint32_t> bandSplitData;

    std::unordered_map<int, size_t> codepointToCurveOffset;
    std::unordered_map<int, size_t> codepointToBandSplitOffset;

    struct PackedCurve {
      float    x0, y0, x1, y1, x2, y2;
      uint32_t _padding[2];
    } __attribute__((packed));

    struct PackedBandMeta {
      uint32_t dataOffset;
      uint32_t nCurves;
    } __attribute__((packed));

    struct PackedBandMetaArray {
      PackedBandMeta vertical[kBandSplits];
      PackedBandMeta horizontal[kBandSplits];
    } __attribute__((packed));

    void PackGlyph(const Glyph& glyph) {
      size_t curveDataOffset                  = curveData.size() / sizeof(PackedCurve);
      codepointToCurveOffset[glyph.codepoint] = curveDataOffset;

      for (const auto& curve: glyph.curves) {
        PackedCurve packedCurve;
        packedCurve.x0          = curve.from.x;
        packedCurve.y0          = curve.from.y;
        packedCurve.x1          = curve.control.x;
        packedCurve.y1          = curve.control.y;
        packedCurve.x2          = curve.to.x;
        packedCurve.y2          = curve.to.y;
        packedCurve._padding[0] = 0xDEADBEEF;
        packedCurve._padding[1] = 0xDEADBEEF;

        curveData.insert(
          curveData.end(),
          reinterpret_cast<uint8_t*>(&packedCurve),
          reinterpret_cast<uint8_t*>(&packedCurve) + sizeof(PackedCurve));
      }

      size_t bandSplitDataOffset                  = bandSplitData.size();
      codepointToBandSplitOffset[glyph.codepoint] = bandSplitDataOffset;

      PackedBandMetaArray   bandMetaArray;
      std::vector<uint32_t> curveIndices;
      uint32_t              metaSizeUnits = sizeof(PackedBandMetaArray) / sizeof(uint32_t);

      for (int i = 0; i < kBandSplits; ++i) {
        bandMetaArray.vertical[i].nCurves    = glyph.bandV[i].curveIndices.size();
        bandMetaArray.vertical[i].dataOffset = metaSizeUnits + (uint32_t) curveIndices.size();
        for (int idx: glyph.bandV[i].curveIndices) curveIndices.push_back(idx);
      }

      for (int i = 0; i < kBandSplits; ++i) {
        bandMetaArray.horizontal[i].nCurves    = glyph.bandH[i].curveIndices.size();
        bandMetaArray.horizontal[i].dataOffset = metaSizeUnits + (uint32_t) curveIndices.size();
        for (int idx: glyph.bandH[i].curveIndices) curveIndices.push_back(idx);
      }

      uint32_t* metaPtr = reinterpret_cast<uint32_t*>(&bandMetaArray);
      bandSplitData.insert(bandSplitData.end(), metaPtr, metaPtr + metaSizeUnits);
      bandSplitData.insert(bandSplitData.end(), curveIndices.begin(), curveIndices.end());
    }

    void PackGlyphs(const std::vector<Glyph>& glyphs) {
      for (const auto& glyph: glyphs) {
        PackGlyph(glyph);
      }
    }
  };

  std::vector<Glyph>           glyphs;
  std::unordered_map<int, int> codepointToGlyphIndex;

  PackedGlyphData            packedData;
  std::unique_ptr<uint8_t[]> fontData;
  stbtt_fontinfo             fontInfo;
  int                        ascent;
  int                        descent;
  int                        lineGap;

  struct SlugVertex {
    float pos[4];// xy: pos, zw: normal
    float tex[4];// xy: em-coords, z: glyph-offset, w: band-info
    float jac[4];// Inverse Jacobian
    float bnd[4];// Band scale/offset
    float col[4];// RGBA color

    static SlugVertex Create(
      float x, float y,
      float nx, float ny,
      float u, float v,

      uint32_t             curveOffset,
      uint32_t             bandOffset,
      uint16_t             maxBandX,
      uint16_t             maxBandY,
      std::array<float, 4> inverseJacobian,

      std::array<float, 4> bandParams,
      Color                tint) {
      SlugVertex vert;

      vert.pos[0] = x;
      vert.pos[1] = y;
      vert.pos[2] = nx;
      vert.pos[3] = ny;

      vert.tex[0] = u;
      vert.tex[1] = v;

      uint32_t packedZ = (curveOffset & 0xFFFF) << 16 | (bandOffset & 0xFFFF);
      memcpy(&vert.tex[2], &packedZ, 4);

      uint32_t flags   = 0;
      uint32_t packedW = (flags << 24) | ((maxBandY & 0xFF) << 8) | (maxBandX & 0xFF);
      memcpy(&vert.tex[3], &packedW, 4);

      vert.jac[0] = inverseJacobian[0];
      vert.jac[1] = inverseJacobian[1];
      vert.jac[2] = inverseJacobian[2];
      vert.jac[3] = inverseJacobian[3];

      vert.bnd[0] = bandParams[0];
      vert.bnd[1] = bandParams[1];
      vert.bnd[2] = bandParams[2];
      vert.bnd[3] = bandParams[3];

      vert.col[0] = tint.r / 255.0f;
      vert.col[1] = tint.g / 255.0f;
      vert.col[2] = tint.b / 255.0f;
      vert.col[3] = tint.a / 255.0f;

      return vert;
    }
  };

  static std::array<float, 4> CalculateInverseJacobian(Vector2 p0, Vector2 p1, Vector2 p3, Vector2 t0, Vector2 t1, Vector2 t3) {
    Vector2 pDx = Vector2Subtract(p1, p0);
    Vector2 pDy = Vector2Subtract(p3, p0);
    Vector2 tDx = Vector2Subtract(t1, t0);
    Vector2 tDy = Vector2Subtract(t3, t0);

    float det = pDx.x * pDy.y - pDy.x * pDx.y;
    if (fabsf(det) < 1.0e-8f) {
      return {1.0f, 0.0f, 0.0f, 1.0f};
    }

    float invDet = 1.0f / det;

    float invP00 = pDy.y * invDet;
    float invP01 = -pDy.x * invDet;
    float invP10 = -pDx.y * invDet;
    float invP11 = pDx.x * invDet;

    float jac00 = tDx.x * invP00 + tDy.x * invP10;
    float jac01 = tDx.x * invP01 + tDy.x * invP11;
    float jac10 = tDx.y * invP00 + tDy.y * invP10;
    float jac11 = tDx.y * invP01 + tDy.y * invP11;

    return {jac00, jac01, jac10, jac11};
  }

  struct RenderResources {
    uint32_t shaderProgram = 0;
    uint32_t curveSSBO     = 0;
    uint32_t bandSplitSSBO = 0;
    uint32_t vertDataSSBO  = 0;
    uint32_t dummyVAO      = 0;

    void Create(const PackedGlyphData& packedData, int vertDataSize) {
      shaderProgram = rlLoadShaderProgram(kVertShaderCode, kFragShaderCode);
      curveSSBO     = rlLoadShaderBuffer(packedData.curveData.size(), (void*) packedData.curveData.data(), RL_DYNAMIC_DRAW);
      bandSplitSSBO = rlLoadShaderBuffer(packedData.bandSplitData.size() * sizeof(uint32_t), (void*) packedData.bandSplitData.data(), RL_DYNAMIC_DRAW);

      SlugVertex dummyVertex = {};
      vertDataSSBO           = rlLoadShaderBuffer(vertDataSize, &dummyVertex, RL_DYNAMIC_DRAW);
      dummyVAO               = rlLoadVertexArray();

      TraceLog(LOG_INFO, "Curve Data SSBO (%d): (Actual Data Size: %d bytes)", curveSSBO, packedData.curveData.size());
      TraceLog(LOG_INFO, "Band Split Data SSBO (%d): (Actual Data Size: %d bytes)", bandSplitSSBO, packedData.bandSplitData.size());
      TraceLog(LOG_INFO, "Vertex Data SSBO: %d (Size: %d bytes)", vertDataSSBO, vertDataSize);
    }

    void Cleanup() {
      rlUnloadVertexArray(dummyVAO);
      rlUnloadShaderBuffer(vertDataSSBO);
      rlUnloadShaderBuffer(curveSSBO);
      rlUnloadShaderBuffer(bandSplitSSBO);
      rlUnloadShaderProgram(shaderProgram);

      TraceLog(LOG_INFO, "Unloaded SSBOs: Curve SSBO (%d), Band Split SSBO (%d)", curveSSBO, bandSplitSSBO);
    }
  };

  RenderResources resources;

  // takes fontData ownership
  SlugFont(std::unique_ptr<uint8_t[]>&& fontData, const int* codepoints, const int nCodepoints) {
    this->fontData = std::move(fontData);
    stbtt_InitFont(&this->fontInfo, this->fontData.get(), 0);
    stbtt_GetFontVMetrics(&this->fontInfo, &ascent, &descent, &lineGap);

    for (int i = 0; i < nCodepoints; ++i) {
      const int codepoint = codepoints[i];
      Glyph     glyph(fontInfo, codepoint);
      glyphs.push_back(glyph);
      codepointToGlyphIndex[glyph.codepoint] = glyphs.size() - 1;
    }

    packedData.PackGlyphs(glyphs);
    resources.Create(packedData, sizeof(SlugVertex) * 4);
  }

  ~SlugFont() {
    resources.Cleanup();
  }

  void UploadVertexData(const SlugVertex* vert) {
    rlUpdateShaderBuffer(resources.vertDataSSBO, vert, sizeof(SlugVertex) * 4, 0);
  }

  void RenderChar(int codepoint, Vector2 position, Color tint) {
    uint32_t curveOffset = packedData.codepointToCurveOffset[codepoint];
    uint32_t bandOffset  = packedData.codepointToBandSplitOffset[codepoint];

    const auto& glyph = glyphs[codepointToGlyphIndex[codepoint]];

    // quads
    float width  = glyph.bounds.width;
    float height = glyph.bounds.height;
    float x      = position.x + glyph.bounds.x;
    float y      = position.y - glyph.bounds.y - glyph.bounds.height;

    Vector2 p0 = {x, y};
    Vector2 p1 = {x + width, y};
    Vector2 p3 = {x, y + height};

    Vector2 t0 = {glyph.bounds.x, glyph.bounds.y + height};
    Vector2 t1 = {glyph.bounds.x + width, glyph.bounds.y + height};
    Vector2 t3 = {glyph.bounds.x, glyph.bounds.y};

    std::array<float, 4> inverseJacobian = CalculateInverseJacobian(p0, p1, p3, t0, t1, t3);

    SlugVertex vertices[4] = {
      SlugVertex::Create(
        x, y, -1, -1,
        glyph.bounds.x, glyph.bounds.y + height,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, inverseJacobian,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH},
        tint),
      SlugVertex::Create(
        x + width, y,
        1, -1,
        glyph.bounds.x + width, glyph.bounds.y + height,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, inverseJacobian,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH},
        tint),
      SlugVertex::Create(
        x + width, y + height,
        1, 1,
        glyph.bounds.x + width, glyph.bounds.y,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, inverseJacobian,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH},
        tint),
      SlugVertex::Create(
        x, y + height,
        -1, 1,
        glyph.bounds.x, glyph.bounds.y,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, inverseJacobian,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH},
        tint),
    };

    UploadVertexData(vertices);

    rlEnableShader(resources.shaderProgram);

    rlBindShaderBuffer(resources.vertDataSSBO, 0);
    rlBindShaderBuffer(resources.curveSSBO, 1);
    rlBindShaderBuffer(resources.bandSplitSSBO, 2);

    Matrix model      = rlGetMatrixTransform();
    Matrix view       = rlGetMatrixModelview();
    Matrix projection = rlGetMatrixProjection();

    Matrix mv     = MatrixMultiply(model, view);
    Matrix mvp    = MatrixMultiply(mv, projection);
    int    mvpLoc = rlGetLocationUniform(resources.shaderProgram, "u_mvp");
    rlSetUniformMatrix(mvpLoc, mvp);

    float viewport[2] = {(float) GetScreenWidth(), (float) GetScreenHeight()};
    int   viewLoc     = rlGetLocationUniform(resources.shaderProgram, "u_viewport");
    rlSetUniform(viewLoc, viewport, RL_SHADER_UNIFORM_VEC2, 1);

    rlDisableBackfaceCulling();
    rlDisableDepthTest();
    rlEnableVertexArray(resources.dummyVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    rlDisableVertexArray();

    rlDisableShader();
  }

  float GetAscent() const {
    return (float) ascent;
  }

  float GetLineAdvance() const {
    return (float) (ascent - descent + lineGap);
  }

  float GetScaleForPixelHeight(float pixelHeight) const {
    return stbtt_ScaleForPixelHeight(&fontInfo, pixelHeight);
  }

  float GetAdvance(int codepoint, int nextCodepoint) const {
    auto it = codepointToGlyphIndex.find(codepoint);
    if (it == codepointToGlyphIndex.end()) return 0.0f;

    const Glyph& glyph = glyphs[it->second];
    int          kern  = nextCodepoint != 0 ? stbtt_GetCodepointKernAdvance(&fontInfo, codepoint, nextCodepoint) : 0;
    return (float) (glyph.advanceWidth + kern);
  }
};

typedef SlugFont* PSlugFont;

inline void DrawTextCodepointSlug_Impl(PSlugFont slugFont, int codepoint, Vector2 position, Vector2 scale, Color tint) {
  rlPushMatrix();
  rlTranslatef(position.x, position.y, 0.0f);
  rlScalef(scale.x, scale.y, 1.0f);
  slugFont->RenderChar(codepoint, {0.0f, 0.0f}, tint);
  rlPopMatrix();
}

inline void DrawTextCodepointsSlug(PSlugFont slugFont, const int* codepoints, int nCodepoints, Vector2 position, float fontSize, float spacing, Color tint) {
  float   uniformScale = slugFont->GetScaleForPixelHeight(fontSize);
  Vector2 scale        = {uniformScale, uniformScale};

  float startX   = position.x;
  float startY   = position.y;
  float penX     = startX;
  float baseline = startY + slugFont->GetAscent() * scale.y;

  for (int i = 0; i < nCodepoints; ++i) {
    if (codepoints[i] == '\n') {
      penX = startX;
      baseline += slugFont->GetLineAdvance() * scale.y;
      continue;
    }

    int codepoint     = codepoints[i];
    int nextCodepoint = (i + 1 < nCodepoints && codepoints[i + 1] != '\n') ? codepoints[i + 1] : 0;

    DrawTextCodepointSlug_Impl(slugFont, codepoint, {penX, baseline}, scale, tint);
    penX += slugFont->GetAdvance(codepoint, nextCodepoint) * scale.x + spacing;
  }
}

inline void DrawTextCodepointSlug(PSlugFont slugFont, int codepoint, Vector2 position, float fontSize, Color tint) {
  DrawTextCodepointsSlug(slugFont, &codepoint, 1, position, fontSize, 0.0f, tint);
}

inline PSlugFont LoadFontSlug(const char* fontFileName, const int* codepoints, int nCodepoints) {
  int   fontFileSize      = 0;
  auto* fontDataUnmanaged = LoadFileData(fontFileName, &fontFileSize);

  std::unique_ptr<uint8_t[]> fontData = std::make_unique<uint8_t[]>(fontFileSize);
  memcpy(fontData.get(), fontDataUnmanaged, fontFileSize);
  UnloadFileData(fontDataUnmanaged);

  PSlugFont mem = static_cast<PSlugFont>(MemAlloc(sizeof(SlugFont)));
  return new (mem) SlugFont(std::move(fontData), codepoints, nCodepoints);
}

inline void UnloadFontSlug(PSlugFont slugFont) {
  if (slugFont) {
    slugFont->~SlugFont();
    MemFree(slugFont);
  }
}
