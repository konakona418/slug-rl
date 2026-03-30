// Slug Text Rendering Sample
//
// By Zimeng Li (@konakona418)

static const char kFontFilenameJa[] = "NotoSansJP-Regular.ttf";
static const char kFontFilenameZh[] = "NotoSansSC-Regular.ttf";

static const char kSampleTextZh[] =
  u8"燕子去了，有再来的时候；\n"
  u8"杨柳枯了，有再青的时候；\n"
  u8"桃花谢了，有再开的时候。\n"
  u8"但是，聪明的，你告诉我，\n"
  u8"我们的日子为什么一去不复返呢？\n";

static const char kSampleTextJa[] =
  u8"吾輩は猫である。\n"
  u8"名前はまだ無い。\n"
  u8"どこで生れたかとんと見当がつかぬ。\n"
  u8"何でも薄暗いじめじめした所で\n"
  u8"ニャーニャー泣いていた事だけは記憶している。\n"
  u8"吾輩はここで始めて人間というものを見た。\n";

static const char kSampleTextEn[] =
  "Four score and seven years ago\n"
  "our fathers brought forth on this continent,\n"
  "a new nation, conceived in Liberty,\n"
  "and dedicated to the proposition that all men are created equal.\n";

static const char kWindowTitle[] = "Slug Text Rendering";
static const int  kWindowWidth   = 1280;
static const int  kWindowHeight  = 720;

static constexpr float kMinViewScale  = 0.1f;
static constexpr float kMaxViewScale  = 16.0f;
static constexpr float kZoomStep      = 1.1f;
static constexpr int   kBloomLevels   = 4;
static constexpr float kBloomStrength = 0.8f;

static const char kBloomDownsampleShader[] =
  "#version 430 core\n"
  "in vec2 fragTexCoord;\n"
  "in vec4 fragColor;\n"
  "uniform sampler2D texture0;\n"
  "uniform vec4 colDiffuse;\n"
  "uniform vec2 texelSize;\n"
  "out vec4 finalColor;\n"
  "void main()\n"
  "{\n"
  "  vec3 sum = vec3(0.0);\n"
  "  for (int x = -1; x <= 1; x++)\n"
  "  {\n"
  "    for (int y = -1; y <= 1; y++)\n"
  "    {\n"
  "      sum += texture(texture0, fragTexCoord + vec2(x, y) * texelSize).rgb;\n"
  "    }\n"
  "  }\n"
  "  vec3 blurred = sum / 9.0;\n"
  "  finalColor = vec4(blurred, 1.0) * colDiffuse;\n"
  "}\n";

static const char kBloomUpsampleShader[] =
  "#version 430 core\n"
  "in vec2 fragTexCoord;\n"
  "in vec4 fragColor;\n"
  "uniform sampler2D texture0;\n"
  "uniform vec4 colDiffuse;\n"
  "out vec4 finalColor;\n"
  "void main()\n"
  "{\n"
  "  vec4 color = texture(texture0, fragTexCoord);\n"
  "  finalColor = color * colDiffuse;\n"
  "}\n";

#include <cmath>
#include <cstring>
#include <set>
#include <sstream>
#include <vector>

#include "slug.h"

static void RenderBloomPass(
  RenderTexture2D& renderTexture,
  RenderTexture2D* bloomTextures,
  const int*       bloomWidths,
  const int*       bloomHeights,
  int              bloomLevels,
  Shader           downsampleShader,
  Shader           upsampleShader,
  int              downsampleTexelLoc) {
  Texture2D sourceTexture = renderTexture.texture;
  int       sourceWidth   = renderTexture.texture.width;
  int       sourceHeight  = renderTexture.texture.height;

  for (int i = 0; i < bloomLevels; ++i) {
    BeginTextureMode(bloomTextures[i]);
    {
      BeginShaderMode(downsampleShader);
      Vector2 texelSize = {1.0f / (float) sourceWidth, 1.0f / (float) sourceHeight};
      SetShaderValue(downsampleShader, downsampleTexelLoc, &texelSize, SHADER_UNIFORM_VEC2);
      DrawTexturePro(
        sourceTexture,
        Rectangle{0.0f, 0.0f, (float) sourceWidth, (float) -sourceHeight},
        Rectangle{0.0f, 0.0f, (float) bloomWidths[i], (float) bloomHeights[i]},
        Vector2{0.0f, 0.0f},
        0.0f,
        WHITE);
      EndShaderMode();
    }
    EndTextureMode();

    sourceTexture = bloomTextures[i].texture;
    sourceWidth   = bloomWidths[i];
    sourceHeight  = bloomHeights[i];
  }

  for (int i = bloomLevels - 2; i >= 0; --i) {
    BeginTextureMode(bloomTextures[i]);
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(upsampleShader);
    DrawTexturePro(
      bloomTextures[i + 1].texture,
      Rectangle{0.0f, 0.0f, (float) bloomWidths[i + 1], (float) -bloomHeights[i + 1]},
      Rectangle{0.0f, 0.0f, (float) bloomWidths[i], (float) bloomHeights[i]},
      Vector2{0.0f, 0.0f},
      0.0f,
      WHITE);
    EndShaderMode();
    EndBlendMode();
    EndTextureMode();
  }
}

int main(int argc, char** argv) {
  char*             sampleText;
  bool              fromFile = false;
  std::vector<int*> codepointSections;
  std::vector<int>  sectionCounts;

  if (argc > 1) {
    sampleText = LoadFileText(argv[1]);
    fromFile   = true;
  } else {
    sampleText = nullptr;
  }

  if (fromFile) {
    int nCodepoints = 0;
    codepointSections.push_back(LoadCodepoints(sampleText, &nCodepoints));
    sectionCounts.push_back(nCodepoints);
  } else {
    const char* sampleSections[] = {kSampleTextZh, kSampleTextJa, kSampleTextEn};
    for (const char* sectionText: sampleSections) {
      int nCodepoints = 0;
      codepointSections.push_back(LoadCodepoints(const_cast<char*>(sectionText), &nCodepoints));
      sectionCounts.push_back(nCodepoints);
    }
  }

  std::set<int> uniqueCodepoints;
  for (size_t i = 0; i < codepointSections.size(); ++i) {
    int* sectionCodepoints = codepointSections[i];
    int  sectionCount      = sectionCounts[i];
    uniqueCodepoints.insert(sectionCodepoints, sectionCodepoints + sectionCount);
  }

  for (int c = 32; c < 127; ++c) {
    uniqueCodepoints.insert(c);
  }

  std::vector<int> uniqueCodepointVec(uniqueCodepoints.begin(), uniqueCodepoints.end());

  InitWindow(kWindowWidth, kWindowHeight, kWindowTitle);
  SetTargetFPS(60);

  PSlugFont slugFontJa = LoadFontSlug(
    kFontFilenameJa,
    uniqueCodepointVec.data(),
    (int) uniqueCodepointVec.size());

  PSlugFont slugFontZh = LoadFontSlug(
    kFontFilenameZh,
    uniqueCodepointVec.data(),
    (int) uniqueCodepointVec.size());

  Font raylibFontJa = LoadFontEx(
    kFontFilenameJa,
    96,
    uniqueCodepointVec.data(),
    (int) uniqueCodepointVec.size());
  Font raylibFontZh = LoadFontEx(
    kFontFilenameZh,
    96,
    uniqueCodepointVec.data(),
    (int) uniqueCodepointVec.size());

  auto downsampleShader   = LoadShaderFromMemory(nullptr, kBloomDownsampleShader);
  auto upsampleShader     = LoadShaderFromMemory(nullptr, kBloomUpsampleShader);
  int  downsampleTexelLoc = GetShaderLocation(downsampleShader, "texelSize");

  auto renderTexture = LoadRenderTexture(kWindowWidth, kWindowHeight);
  SetTextureFilter(renderTexture.texture, TEXTURE_FILTER_BILINEAR);

  RenderTexture2D bloomTextures[kBloomLevels];
  int             bloomWidths[kBloomLevels];
  int             bloomHeights[kBloomLevels];
  int             currentWidth  = kWindowWidth;
  int             currentHeight = kWindowHeight;
  for (int i = 0; i < kBloomLevels; ++i) {
    currentWidth     = std::max(1, currentWidth / 2);
    currentHeight    = std::max(1, currentHeight / 2);
    bloomWidths[i]   = currentWidth;
    bloomHeights[i]  = currentHeight;
    bloomTextures[i] = LoadRenderTexture(currentWidth, currentHeight);
    SetTextureFilter(bloomTextures[i].texture, TEXTURE_FILTER_BILINEAR);
  }

  float   viewScale    = 1.0f;
  Vector2 viewOffset   = {0.0f, 0.0f};
  bool    isPanning    = false;
  Vector2 lastMousePos = {0.0f, 0.0f};

  bool fancyVFX = true;
  bool useSlug  = true;

  const int     demoCodepoint = U'的';
  const float   demoFontSize  = 96.0f;
  const Vector2 demoPosition  = {1020.0f, 210.0f};
  const Vector2 demoOrigin    = {28.0f, 56.0f};
  float         demoRotation  = 0.0f;

  int  rotationPivotCodepointLength = 0;
  int* rotationPivotTextCodepoints  = LoadCodepoints("Rotation Pivot", &rotationPivotCodepointLength);

  SetTextLineSpacing(0);
  while (!WindowShouldClose()) {
    demoRotation += 90.0f * GetFrameTime();
    if (demoRotation >= 360.0f) demoRotation -= 360.0f;

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
      Vector2 mousePos  = GetMousePosition();
      float   oldScale  = viewScale;
      float   zoomScale = powf(kZoomStep, wheel);
      float   newScale  = Clamp(oldScale * zoomScale, kMinViewScale, kMaxViewScale);

      if (newScale != oldScale) {
        Vector2 worldPosUnderMouse = {
          (mousePos.x - viewOffset.x) / oldScale,
          (mousePos.y - viewOffset.y) / oldScale,
        };

        viewScale  = newScale;
        viewOffset = {
          mousePos.x - worldPosUnderMouse.x * viewScale,
          mousePos.y - worldPosUnderMouse.y * viewScale,
        };
      }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
      isPanning    = true;
      lastMousePos = GetMousePosition();
    }

    if (isPanning && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
      Vector2 currentMousePos = GetMousePosition();
      Vector2 delta           = Vector2Subtract(currentMousePos, lastMousePos);
      viewOffset              = Vector2Add(viewOffset, delta);
      lastMousePos            = currentMousePos;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
      isPanning = false;
    }

    if (IsKeyPressed(KEY_ZERO)) {
      viewScale  = 1.0f;
      viewOffset = {0.0f, 0.0f};
    }

    if (IsKeyPressed(KEY_SPACE)) {
      fancyVFX = !fancyVFX;
    }

    if (IsKeyPressed(KEY_ENTER)) {
      useSlug = !useSlug;
    }

    BeginTextureMode(renderTexture);
    {
      BeginBlendMode(BLEND_ALPHA);
      ClearBackground(BLACK);

      rlPushMatrix();
      rlTranslatef(viewOffset.x, viewOffset.y, 0.0f);
      rlScalef(viewScale, viewScale, 1.0f);

      if (useSlug) {
        if (fromFile) {
          DrawTextCodepointsSlug(slugFontZh, codepointSections[0], sectionCounts[0], {25.0f, 25.0f}, 64.0f, 0, WHITE);
        } else {
          DrawTextCodepointsSlug(slugFontZh, codepointSections[0], sectionCounts[0], {25.0f, 25.0f}, 28.0f, 0, Color{255, 255, 0, 255});
          DrawTextCodepointsSlug(slugFontJa, codepointSections[1], sectionCounts[1], {25.0f, 200.0f}, 28.0f, 0, Color{0, 255, 255, 255});
          DrawTextCodepointsSlug(slugFontZh, codepointSections[2], sectionCounts[2], {25.0f, 400.0f}, 28.0f, 0, Color{255, 182, 193, 255});
        }

        DrawTextCodepointSlugPro(
          slugFontZh,
          demoCodepoint,
          demoPosition,
          demoOrigin,
          demoRotation,
          demoFontSize,
          ORANGE);
      } else {
        if (fromFile) {
          DrawTextEx(raylibFontZh, sampleText, {25.0f, 25.0f}, 64.0f, 0.0f, WHITE);
        } else {
          DrawTextEx(raylibFontZh, kSampleTextZh, {25.0f, 25.0f}, 28.0f, 0.0f, Color{255, 255, 0, 255});
          DrawTextEx(raylibFontJa, kSampleTextJa, {25.0f, 200.0f}, 28.0f, 0.0f, Color{0, 255, 255, 255});
          DrawTextEx(raylibFontZh, kSampleTextEn, {25.0f, 400.0f}, 28.0f, 0.0f, Color{255, 182, 193, 255});
        }

        int         utf8Size      = 0;
        const char* demoUtf8Glyph = CodepointToUTF8(demoCodepoint, &utf8Size);
        DrawTextPro(
          raylibFontZh,
          demoUtf8Glyph,
          demoPosition,
          demoOrigin,
          demoRotation,
          demoFontSize,
          0.0f,
          ORANGE);
      }

      DrawCircleLinesV(demoPosition, 6.0f, RED);
      if (useSlug) {
        DrawTextCodepointsSlug(slugFontZh, rotationPivotTextCodepoints, rotationPivotCodepointLength, {demoPosition.x + 10.0f, demoPosition.y - 10.0f}, 18.0f, 0, RED);
      } else {
        DrawTextEx(raylibFontZh, "Rotation Pivot", {demoPosition.x + 10.0f, demoPosition.y - 10.0f}, 18.0f, 0.0f, RED);
      }
      rlPopMatrix();

      EndBlendMode();
    }
    EndTextureMode();

    if (fancyVFX) {
      RenderBloomPass(
        renderTexture,
        bloomTextures,
        bloomWidths,
        bloomHeights,
        kBloomLevels,
        downsampleShader,
        upsampleShader,
        downsampleTexelLoc);
    }

    BeginDrawing();
    {
      ClearBackground(BLACK);

      DrawTextureRec(
        renderTexture.texture,
        Rectangle{0.0f, 0.0f, (float) renderTexture.texture.width, (float) -renderTexture.texture.height},
        Vector2{0.0f, 0.0f},
        WHITE);

      if (fancyVFX) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(
          bloomTextures[0].texture,
          Rectangle{0.0f, 0.0f, (float) bloomWidths[0], (float) -bloomHeights[0]},
          Rectangle{0.0f, 0.0f, (float) kWindowWidth, (float) kWindowHeight},
          Vector2{0.0f, 0.0f},
          0.f,
          Color{255, 255, 255, (unsigned char) (255.0f * kBloomStrength)});
        EndBlendMode();
      }

      DrawText("Mouse Wheel: Zoom | Middle Mouse Button: Pan | 0: Reset View | Space: Toggle VFX | Enter: Toggle Slug/Raylib", 10, kWindowHeight - 55, 20, YELLOW);
      std::stringstream ss;
      ss << "Render: " << (useSlug ? "Slug" : "Raylib")
         << " | Rotation: " << demoRotation
         << " | View Scale: " << viewScale
         << " | View Offset: (" << viewOffset.x << ", " << viewOffset.y << ")";
      DrawText(ss.str().c_str(), 10, kWindowHeight - 30, 20, YELLOW);

      DrawFPS(10, 10);
    }
    EndDrawing();
  }

  UnloadCodepoints(rotationPivotTextCodepoints);

  UnloadRenderTexture(renderTexture);
  for (int i = 0; i < kBloomLevels; ++i) {
    UnloadRenderTexture(bloomTextures[i]);
  }
  UnloadShader(downsampleShader);
  UnloadShader(upsampleShader);

  UnloadFont(raylibFontJa);
  UnloadFont(raylibFontZh);

  UnloadFontSlug(slugFontJa);
  UnloadFontSlug(slugFontZh);

  CloseWindow();

  for (int* sectionCodepoints: codepointSections) {
    UnloadCodepoints(sectionCodepoints);
  }

  if (fromFile) UnloadFileText(sampleText);

  return 0;
}
