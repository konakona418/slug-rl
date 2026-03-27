#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

#define FONT_FILENAME "NotoSansSC-Regular.ttf"
#define TEST_STR                                                                        \
  "燕子去了，有再來的時候；楊柳枯了，有再靑的時候；\n"          \
  "桃花謝了，有再開的時候。\n"                                              \
  "但是，聰明的，你吿訴我，我們的日子爲什麼一去不復返呢？\n" \
  "——是有人偷了他們罷：那是誰？又藏在何處呢？\n"                \
  "是他們自己逃走了罷：現在又到了那裏呢？"

// seems that characters like dash '-' and '一' are not rendered correctly,
// perhaps because they are very small?

static const char kWindowTitle[] = "Slug Text Rendering";
static const int  kWindowWidth   = 1280;
static const int  kWindowHeight  = 720;

static constexpr int kBandSplits = 16;

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
        BCurve curve;
        curve.from    = {static_cast<float>(lastX), static_cast<float>(lastY)};
        curve.control = {static_cast<float>(v.x + lastX) / 2.0f, static_cast<float>(v.y + lastY) / 2.0f};
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

static Vector2 CvtCoordToScreen(const Vector2 coord) {
  return {coord.x, -coord.y + kWindowHeight};
}


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

#include <external/glad.h>

struct SlugFont {
  std::vector<Glyph>           glyphs;
  std::unordered_map<int, int> codepointToGlyphIndex;

  PackedGlyphData packedData;
  stbtt_fontinfo  fontInfo;
  int             ascent;
  int             descent;
  int             lineGap;

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

      uint32_t curveOffset,
      uint32_t bandOffset,
      uint16_t maxBandX,
      uint16_t maxBandY,
      float    invJac,

      std::array<float, 4> bandParams,
      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
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

      vert.jac[0] = invJac;
      vert.jac[1] = 0.0f;
      vert.jac[2] = 0.0f;
      vert.jac[3] = invJac;

      vert.bnd[0] = bandParams[0];
      vert.bnd[1] = bandParams[1];
      vert.bnd[2] = bandParams[2];
      vert.bnd[3] = bandParams[3];

      vert.col[0] = r;
      vert.col[1] = g;
      vert.col[2] = b;
      vert.col[3] = a;

      return vert;
    }
  };

  struct RenderResources {
    uint32_t shaderProgram = 0;
    uint32_t curveSSBO     = 0;
    uint32_t bandSplitSSBO = 0;
    uint32_t vertDataSSBO  = 0;
    uint32_t dummyVAO      = 0;

    void Create(const PackedGlyphData& packedData, int vertDataSize) {
      char* vertexShaderCode   = LoadFileText("vert.glsl");
      char* fragmentShaderCode = LoadFileText("frag.glsl");
      shaderProgram            = rlLoadShaderProgram(vertexShaderCode, fragmentShaderCode);
      UnloadFileText(vertexShaderCode);
      UnloadFileText(fragmentShaderCode);

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

  SlugFont(stbtt_fontinfo& fontInfo, const int* codepoints, const int nCodepoints) {
    this->fontInfo = fontInfo;
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

  void RenderChar(int codepoint, Vector2 position) {
    uint32_t curveOffset = packedData.codepointToCurveOffset[codepoint];
    uint32_t bandOffset  = packedData.codepointToBandSplitOffset[codepoint];

    const auto& glyph = glyphs[codepointToGlyphIndex[codepoint]];

    // quads
    float width  = glyph.bounds.width;
    float height = glyph.bounds.height;
    float x      = position.x + glyph.bounds.x;
    float y      = position.y - glyph.bounds.y - glyph.bounds.height;

    // todo: calculate actual Jacobian based on curve complexity for better AA quality
    float invJac = 1.0f;

    SlugVertex vertices[4] = {
      SlugVertex::Create(
        x, y, -1, -1,
        glyph.bounds.x, glyph.bounds.y + height,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, invJac,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH}),
      SlugVertex::Create(
        x + width, y,
        1, -1,
        glyph.bounds.x + width, glyph.bounds.y + height,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, invJac,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH}),
      SlugVertex::Create(
        x + width, y + height,
        1, 1,
        glyph.bounds.x + width, glyph.bounds.y,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, invJac,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH}),
      SlugVertex::Create(
        x, y + height,
        -1, 1,
        glyph.bounds.x, glyph.bounds.y,
        curveOffset, bandOffset,
        kBandSplits - 1, kBandSplits - 1, invJac,
        {glyph.bandScaleV, glyph.bandScaleH, glyph.bandOffsetV, glyph.bandOffsetH}),
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

  float GetAdvance(int codepoint, int nextCodepoint) const {
    auto it = codepointToGlyphIndex.find(codepoint);
    if (it == codepointToGlyphIndex.end()) return 0.0f;

    const Glyph& glyph = glyphs[it->second];
    int          kern  = nextCodepoint != 0 ? stbtt_GetCodepointKernAdvance(&fontInfo, codepoint, nextCodepoint) : 0;
    return (float) (glyph.advanceWidth + kern);
  }
};

void RenderSlugChar(SlugFont& slugFont, int codepoint, Vector2 position, Vector2 scale) {
  rlPushMatrix();
  rlTranslatef(position.x, position.y, 0.0f);
  rlScalef(scale.x, scale.y, 1.0f);
  slugFont.RenderChar(codepoint, {0.0f, 0.0f});
  rlPopMatrix();
}

int main(int, char**) {
  int fontFileSize = 0;
  int nCodepoints  = 0;

  auto* fontData   = LoadFileData(FONT_FILENAME, &fontFileSize);
  auto* codepoints = LoadCodepoints(TEST_STR, &nCodepoints);

  std::set<int>    uniqueCodepoints(codepoints, codepoints + nCodepoints);
  std::vector<int> uniqueCodepointVec(uniqueCodepoints.begin(), uniqueCodepoints.end());

  stbtt_fontinfo fontInfo;
  stbtt_InitFont(
    &fontInfo, fontData,
    stbtt_GetFontOffsetForIndex(fontData, 0));

  InitWindow(kWindowWidth, kWindowHeight, kWindowTitle);
  SetTargetFPS(60);

  SlugFont slugFont(fontInfo, uniqueCodepointVec.data(), (int) uniqueCodepointVec.size());

  while (!WindowShouldClose()) {
    BeginDrawing();
    BeginBlendMode(BLEND_ALPHA);
    ClearBackground(BLACK);

    Vector2 scale    = {0.05f, 0.05f};
    float   startX   = 100.0f;
    float   startY   = 100.0f;
    float   penX     = startX;
    float   baseline = startY + slugFont.GetAscent() * scale.y;

    for (int i = 0; i < nCodepoints; ++i) {
      if (codepoints[i] == '\n') {
        penX = startX;
        baseline += slugFont.GetLineAdvance() * scale.y;
        continue;
      }

      int codepoint     = codepoints[i];
      int nextCodepoint = (i + 1 < nCodepoints && codepoints[i + 1] != '\n') ? codepoints[i + 1] : 0;

      RenderSlugChar(slugFont, codepoint, {penX, baseline}, scale);
      penX += slugFont.GetAdvance(codepoint, nextCodepoint) * scale.x;
    }

    EndBlendMode();
    EndDrawing();
  }

  CloseWindow();

  UnloadCodepoints(codepoints);
  UnloadFileData(fontData);

  return 0;
}
