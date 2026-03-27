#version 430

in vec2       v_texcoord;
flat in vec4  v_banding;
flat in ivec4 v_glyph;
in vec4       v_color;

out vec4 fragColor;

struct PackedCurve {
  float x0, y0, x1, y1, x2, y2;
  uint  _padding[2];
};

struct PackedBandMeta {
  uint dataOffset;
  uint nCurves;
};

layout(std430, binding = 1) readonly buffer CurveBuffer {
  PackedCurve curves[];
};

layout(std430, binding = 2) readonly buffer BandBuffer {
  uint rawData[];
};

uint CalcRootCode(float y1, float y2, float y3) {
  uint i1    = floatBitsToUint(y1) >> 31u;
  uint i2    = floatBitsToUint(y2) >> 30u;
  uint i3    = floatBitsToUint(y3) >> 29u;
  uint shift = (i2 & 2u) | (i1 & ~2u);
  shift      = (i3 & 4u) | (shift & ~4u);
  return ((0x2E74u >> shift) & 0x0101u);
}

vec2 SolveHorizPoly(vec4 p12, vec2 p3) {
  vec2  a  = p12.xy - p12.zw * 2.0 + p3;
  vec2  b  = p12.xy - p12.zw;
  float ra = 1.0 / a.y;
  float rb = 0.5 / b.y;
  float d  = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
  float t1 = (b.y - d) * ra;
  float t2 = (b.y + d) * ra;
  if (abs(a.y) < 1.0 / 65536.0) t1 = t2 = p12.y * rb;
  return vec2((a.x * t1 - b.x * 2.0) * t1 + p12.x, (a.x * t2 - b.x * 2.0) * t2 + p12.x);
}

vec2 SolveVertPoly(vec4 p12, vec2 p3) {
  vec2  a  = p12.xy - p12.zw * 2.0 + p3;
  vec2  b  = p12.xy - p12.zw;
  float ra = 1.0 / a.x;
  float rb = 0.5 / b.x;
  float d  = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
  float t1 = (b.x - d) * ra;
  float t2 = (b.x + d) * ra;
  if (abs(a.x) < 1.0 / 65536.0) t1 = t2 = p12.x * rb;
  return vec2((a.y * t1 - b.y * 2.0) * t1 + p12.y, (a.y * t2 - b.y * 2.0) * t2 + p12.y);
}

float CalcCoverage(float xcov, float ycov, float xwgt, float ywgt, int flags) {
  float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
  return clamp(coverage, 0.0, 1.0);
}

void main() {
  vec2  pixelsPerEm = 1.0 / fwidth(v_texcoord);
  ivec2 bandMax     = v_glyph.zw;
  bandMax.y &= 0x00FF;

  ivec2 bandIndex = clamp(ivec2(v_texcoord * v_banding.xy + v_banding.zw), ivec2(0), bandMax);
  uint  glyphBase = uint(v_glyph.x);// glyph band data offset

  /*uint hMetaIdx    = glyphBase + (16u + uint(bandIndex.y)) * 2u;
  uint hCount      = rawData[hMetaIdx + 1u];
  uint hDataOffset = glyphBase + rawData[hMetaIdx];

  float xcov = 0.0, xwgt = 0.0;
  for (uint i = 0u; i < hCount; i++) {
    uint        curveIdx = v_glyph.y + rawData[hDataOffset + i];
    PackedCurve c        = curves[curveIdx];
    vec4        p12      = vec4(c.x0, c.y0, c.x1, c.y1) - v_texcoord.xyxy;
    vec2        p3       = vec2(c.x2, c.y2) - v_texcoord;

    //if (max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5) break;

    uint code = CalcRootCode(p12.y, p12.w, p3.y);
    if (code != 0u) {
      vec2 r = SolveHorizPoly(p12, p3) * pixelsPerEm.x;
      if ((code & 1u) != 0u) {
        xcov += clamp(r.x + 0.5, 0.0, 1.0);
        xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
      }
      if (code > 1u) {
        xcov -= clamp(r.y + 0.5, 0.0, 1.0);
        xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
      }
    }
  }*/

  uint vMetaIdx    = glyphBase + uint(bandIndex.x) * 2u;
  uint vCount      = rawData[vMetaIdx + 1u];
  uint vDataOffset = glyphBase + rawData[vMetaIdx];

  float ycov = 0.0, ywgt = 0.0;
  for (uint i = 0u; i < vCount; i++) {
    uint        curveIdx = v_glyph.y + rawData[vDataOffset + i];
    PackedCurve c        = curves[curveIdx];
    vec4        p12      = vec4(c.x0, c.y0, c.x1, c.y1) - v_texcoord.xyxy;
    vec2        p3       = vec2(c.x2, c.y2) - v_texcoord;

    // todo: something wrong with early exit condition, need to debug
    //if (max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5) break;

    uint code = CalcRootCode(p12.x, p12.z, p3.x);
    if (code != 0u) {
      vec2 r = SolveVertPoly(p12, p3) * pixelsPerEm.y;
      if ((code & 1u) != 0u) {
        ycov -= clamp(r.x + 0.5, 0.0, 1.0);
        ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
      }
      if (code > 1u) {
        ycov += clamp(r.y + 0.5, 0.0, 1.0);
        ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
      }
    }
  }

  // fragColor = v_color * CalcCoverage(xcov, ycov, xwgt, ywgt, v_glyph.w);
  // todo: something wrong with horizontal coverage, need to debug
  fragColor = v_color * ycov;
}