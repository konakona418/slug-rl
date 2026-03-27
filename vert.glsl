#version 430

struct VertexData {
  vec4 pos;
  vec4 tex;
  vec4 jac;
  vec4 bnd;
  vec4 col;
};

layout(std430, binding = 0) readonly buffer PushData {
  VertexData data[4];
}
pushData;

uniform mat4 u_mvp;
uniform vec2 u_viewport;

out vec2       v_texcoord;
flat out vec4  v_banding;
flat out ivec4 v_glyph;
out vec4       v_color;

void SlugUnpack(vec4 tex, vec4 bnd, out vec4 vbnd, out ivec4 vgly) {
  uvec2 g = floatBitsToUint(tex.zw);
  vgly    = ivec4(int(g.x & 0xFFFFu), int(g.x >> 16u), int(g.y & 0xFFFFu), int(g.y >> 16u));
  vbnd    = bnd;
}

vec2 SlugDilate(vec4 pos, vec4 tex, vec4 jac, vec4 m0, vec4 m1, vec4 m3, vec2 dim, out vec2 vpos) {
  vec2  n = normalize(pos.zw);
  float s = dot(m3.xy, pos.xy) + m3.w;
  float t = dot(m3.xy, n);

  float u = (s * dot(m0.xy, n) - t * (dot(m0.xy, pos.xy) + m0.w)) * dim.x;
  float v = (s * dot(m1.xy, n) - t * (dot(m1.xy, pos.xy) + m1.w)) * dim.y;

  float s2 = s * s;
  float st = s * t;
  float uv = u * u + v * v;
  vec2  d  = pos.zw * (s2 * (st + sqrt(max(uv, 0.0))) / (uv - st * st));

  vpos = pos.xy + d;
  return vec2(tex.x + dot(d, jac.xy), tex.y + dot(d, jac.zw));
}

void UnpackPushData(
  uint     idx,
  out vec4 a_pos,
  out vec4 a_tex,
  out vec4 a_jac,
  out vec4 a_bnd,
  out vec4 a_col) {
  a_pos = pushData.data[idx].pos;
  a_tex = pushData.data[idx].tex;
  a_jac = pushData.data[idx].jac;
  a_bnd = pushData.data[idx].bnd;
  a_col = pushData.data[idx].col;
}

void main() {
  vec2 p;
  vec4 m0 = vec4(u_mvp[0][0], u_mvp[1][0], u_mvp[2][0], u_mvp[3][0]);
  vec4 m1 = vec4(u_mvp[0][1], u_mvp[1][1], u_mvp[2][1], u_mvp[3][1]);
  vec4 m2 = vec4(u_mvp[0][2], u_mvp[1][2], u_mvp[2][2], u_mvp[3][2]);
  vec4 m3 = vec4(u_mvp[0][3], u_mvp[1][3], u_mvp[2][3], u_mvp[3][3]);

  uint idx = gl_VertexID;
  vec4 a_pos, a_tex, a_jac, a_bnd, a_col;
  UnpackPushData(idx, a_pos, a_tex, a_jac, a_bnd, a_col);

  v_texcoord = SlugDilate(a_pos, a_tex, a_jac, m0, m1, m3, u_viewport, p);

  gl_Position.x = p.x * m0.x + p.y * m0.y + m0.w;
  gl_Position.y = p.x * m1.x + p.y * m1.y + m1.w;
  gl_Position.z = p.x * m2.x + p.y * m2.y + m2.w;
  gl_Position.w = p.x * m3.x + p.y * m3.y + m3.w;

  SlugUnpack(a_tex, a_bnd, v_banding, v_glyph);
  v_color = a_col;
}