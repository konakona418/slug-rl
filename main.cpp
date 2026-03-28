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

#include <cmath>
#include <cstring>
#include <set>
#include <sstream>
#include <vector>

#include "slug.h"

static constexpr float kMinViewScale = 0.1f;
static constexpr float kMaxViewScale = 16.0f;
static constexpr float kZoomStep     = 1.1f;

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

  float   viewScale    = 1.0f;
  Vector2 viewOffset   = {0.0f, 0.0f};
  bool    isPanning    = false;
  Vector2 lastMousePos = {0.0f, 0.0f};

  while (!WindowShouldClose()) {
    DrawText("Mouse Wheel: Zoom | Middle Mouse Button: Pan | 0: Reset View", 10, kWindowHeight - 60, 20, YELLOW);

    std::stringstream ss;
    ss << "View Scale: " << viewScale << " | View Offset: (" << viewOffset.x << ", " << viewOffset.y << ")";
    DrawText(ss.str().c_str(), 10, kWindowHeight - 30, 20, YELLOW);

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

    BeginDrawing();
    BeginBlendMode(BLEND_ALPHA);
    ClearBackground(BLACK);

    rlPushMatrix();
    rlTranslatef(viewOffset.x, viewOffset.y, 0.0f);
    rlScalef(viewScale, viewScale, 1.0f);

    if (fromFile) {
      DrawTextCodepointsSlug(slugFontZh, codepointSections[0], sectionCounts[0], {25.0f, 25.0f}, 64.0f, 0, WHITE);
    } else {
      DrawTextCodepointsSlug(slugFontZh, codepointSections[0], sectionCounts[0], {25.0f, 25.0f}, 28.0f, 0, Color{255, 255, 0, 255});
      DrawTextCodepointsSlug(slugFontJa, codepointSections[1], sectionCounts[1], {25.0f, 200.0f}, 28.0f, 0, Color{0, 255, 255, 255});
      DrawTextCodepointsSlug(slugFontZh, codepointSections[2], sectionCounts[2], {25.0f, 400.0f}, 28.0f, 0, Color{255, 182, 193, 255});
    }

    rlPopMatrix();

    EndBlendMode();
    EndDrawing();
  }

  UnloadFontSlug(slugFontJa);
  UnloadFontSlug(slugFontZh);

  CloseWindow();

  for (int* sectionCodepoints: codepointSections) {
    UnloadCodepoints(sectionCodepoints);
  }

  if (fromFile) UnloadFileText(sampleText);

  return 0;
}
