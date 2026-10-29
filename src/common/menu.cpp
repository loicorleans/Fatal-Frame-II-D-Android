// the options panel on F10
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>  // ClearActiveID, the drag scroll's

#include "app/app.h"
#include "audio/output.h"
#include "common/aspect.h"
#include "common/keys.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "common/pad_binds.h"
#include "common/translations.h"
#include "game/cheat_engine.h"

// native/gyro.cpp: aiming by turning the phone
namespace gyro {
uint32_t CurrentMode();
const char* ModeName();
void SetMode(uint32_t mode);
float Sensitivity();
void SetSensitivity(float s);
}  // namespace gyro

// native/touch.cpp: the touch controls' layout
namespace touch {
const char* LayoutName();
void StepLayout(int step);
uint32_t ControlsOpacity();
void StepOpacity(int step);
uint32_t ControlsSize();
void StepSize(int step);
const char* StickModeName();
void StepStickMode(int step);
void EditLayout();
}  // namespace touch

// native/update.cpp: the activity asks GitHub for a newer release and says what it found
namespace host {
void CheckForUpdates();
// native/stats.cpp: the performance overlay's numbers, percent and degrees, -1 where the device does not tell
void SamplePerf();
float CpuLoad();
float GpuLoad();
float CpuTemp();
float GpuTemp();
float BatteryTemp();
}  // namespace host

namespace app {

Settings DefaultSettings();  // native/main.cpp: a phone's first run, unlimited frame rate and vsync off

namespace {

std::atomic<bool> g_open{false};

// A controller steps through the panel in levels: the D-pad starts on the tabs (Left / Right step them), A goes into
// the page's rows, and Y comes back out. On a value row A opens it for Left / Right, and A or Y closes it again.
enum class PadFocus { Tabs, Rows };
PadFocus g_pad_focus = PadFocus::Tabs;
ImGuiID g_edit_id = 0;      // the value row A opened for the D-pad
bool g_enter_rows = false;  // A on the tabs: the first row takes the focus
bool g_back = false;        // Y pressed this frame; the first that can use it takes it

// a controller drives the panel: one is plugged in and the keyboard is not the last thing used
bool PadMode() { return (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_HasGamepad) && !input::KeyboardActive(); }
// the D-pad is on the tabs, so no row shows a focus
bool OnTabs() { return PadMode() && g_pad_focus == PadFocus::Tabs; }

// the dragon emblem's gold on blood red and black
constexpr ImVec4 kPanelBg{0.030f, 0.012f, 0.014f, 0.96f};
constexpr ImVec4 kBorder{0.58f, 0.43f, 0.17f, 0.95f};
constexpr ImVec4 kText{0.90f, 0.85f, 0.76f, 1.0f};  // aged bone
constexpr ImVec4 kHighlight{0.38f, 0.05f, 0.06f, 1.0f};  // dried blood
constexpr ImVec4 kHeading{0.88f, 0.68f, 0.27f, 1.0f};
constexpr ImVec4 kDim{0.64f, 0.56f, 0.50f, 1.0f};
constexpr ImVec4 kTab{0.14f, 0.05f, 0.04f, 1.0f};
constexpr ImVec4 kTabHovered{0.26f, 0.07f, 0.05f, 1.0f};
constexpr ImVec4 kButton{0.22f, 0.06f, 0.04f, 1.0f};
// lighter than the highlight, so it shows on a selected row
constexpr ImVec4 kButtonHovered{0.58f, 0.12f, 0.08f, 1.0f};
constexpr ImVec4 kLine{0.92f, 0.72f, 0.28f, 0.35f};
constexpr std::pair<ImGuiCol, ImVec4> kPanelColors[] = {
    {ImGuiCol_WindowBg, kPanelBg}, {ImGuiCol_Border, kBorder}, {ImGuiCol_Text, kText},
    {ImGuiCol_Header, kHighlight}, {ImGuiCol_HeaderHovered, kHighlight}, {ImGuiCol_HeaderActive, kHighlight},
    {ImGuiCol_Button, kButton}, {ImGuiCol_ButtonHovered, kButtonHovered}, {ImGuiCol_ButtonActive, kButtonHovered},
    {ImGuiCol_FrameBg, kTab}, {ImGuiCol_FrameBgHovered, kTabHovered}, {ImGuiCol_FrameBgActive, kHighlight},
    {ImGuiCol_ScrollbarGrab, kHighlight}, {ImGuiCol_ScrollbarGrabHovered, kButtonHovered},
    {ImGuiCol_ScrollbarGrabActive, kHeading}, {ImGuiCol_Separator, kLine}, {ImGuiCol_NavHighlight, kHeading},
};

constexpr uint32_t kAnisoValues[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"GAME DEFAULT", "2x", "4x", "8x", "16x"};
constexpr uint32_t kDistances[] = {1, 2, 4, 8};
const char* const kDistanceNames[] = {"GAME DEFAULT", "2x", "4x", "8x"};
constexpr uint32_t kFmvScales[] = {0, 1};
const char* const kFmvScaleNames[] = {"FILL", "4:3"};
constexpr uint32_t kScreenFilters[] = {0, 1, 2, 3, 4, 5};
const char* const kScreenFilterNames[] = {"OFF", "SHARPEN", "VIVID", "SCANLINES", "CRT", "HDR"};
constexpr uint32_t kAntiAliasings[] = {game::kAaOff, game::kAaFxaa, game::kAaSsaa};
const char* const kAntiAliasingNames[] = {"OFF", "FXAA", "SSAA 2X"};
// the counter's place on the screen, Options::fps_position; top centre is under the settings gear
enum FpsPosition : uint32_t { kFpsTop, kFpsTopLeft, kFpsTopRight, kFpsBottomLeft, kFpsBottom, kFpsBottomRight };
constexpr uint32_t kFpsPositions[] = {kFpsTop, kFpsTopLeft, kFpsTopRight, kFpsBottomLeft, kFpsBottom, kFpsBottomRight};
const char* const kFpsPositionNames[] = {"TOP CENTER", "TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM CENTER",
                                         "BOTTOM RIGHT"};
constexpr uint32_t kAudioOutputs[] = {audio::kOutputAuto, audio::kOutputMono, audio::kOutputStereo,
                                      audio::kOutputSurround};
const char* const kAudioOutputNames[] = {"AUTOMATIC", "MONO", "2.0 STEREO", "5.1 SURROUND"};
const char* const kLanguageNames[] = {"ENGLISH", "FRENCH", "GERMAN", "ITALIAN", "SPANISH",
                                      "INDONESIAN"};  // as game::kLanguages
constexpr const char* kDonateUrl = "https://saweria.co/Loic";
constexpr const char* kGithubUrl = "https://github.com/loicorleans";
constexpr const char* kYoutubeUrl = "https://www.youtube.com/@HWchannel_LO";
constexpr uint32_t kFpsCaps[] = {30, 60, 75, 90, 120, 144, 165, 240, 360, kFpsCapRefresh, 0};

int StepIndex(int index, int step, int count) {
    if (count <= 0) return 0;
    index = (index + step) % count;
    return index < 0 ? index + count : index;
}

int NearestIndex(const uint32_t* values, int count, uint32_t value) {
    int best = 0;
    uint32_t best_d = UINT32_MAX;
    for (int i = 0; i < count; i++) {
        uint32_t d = values[i] > value ? values[i] - value : value - values[i];
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

// index nearest value, stepped first
template <size_t N>
int StepTable(const uint32_t (&values)[N], uint32_t& value, int step) {
    int i = NearestIndex(values, int(N), value);
    if (step) {
        i = StepIndex(i, step, int(N));
        value = values[i];
    }
    return i;
}

// value clamped to lo to hi, stepped by inc through the values StepIndex wraps over
int StepRange(int value, int lo, int hi, int inc, int step) {
    int count = (hi - lo) / inc + 1;
    return lo + inc * StepIndex((std::clamp(value, lo, hi) - lo) / inc, step, count);
}

// ---- gothic dressing: ornaments are drawn with lines and diamonds, the built-in font has no symbols ----

ImU32 U32(const ImVec4& c, float alpha = -1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, alpha < 0 ? c.w : alpha));
}

void Diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);
}

// a line that fades out at one end
void FadeLine(ImDrawList* dl, float x0, float x1, float y, float thick, const ImVec4& color, bool fade_left) {
    if (x1 <= x0) return;
    ImU32 solid = U32(color), clear = U32(color, 0.0f);
    ImU32 l = fade_left ? clear : solid, r = fade_left ? solid : clear;
    dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(x1, y + thick), l, r, r, l);
}

// a line across the content with a diamond in its middle, fading toward both ends
void OrnateDivider() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float em = ImGui::GetFontSize();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, mid = p.x + w * 0.5f, y = p.y + em * 0.35f;
    float gap = em * 0.9f;
    FadeLine(dl, p.x, mid - gap, y, 1.0f, kHeading, true);
    FadeLine(dl, mid + gap, p.x + w, y, 1.0f, kHeading, false);
    Diamond(dl, ImVec2(mid, y + 0.5f), em * 0.3f, U32(kHeading));
    Diamond(dl, ImVec2(mid - em * 0.75f, y + 0.5f), em * 0.14f, U32(kHighlight, 1.0f));
    Diamond(dl, ImVec2(mid + em * 0.75f, y + 0.5f), em * 0.14f, U32(kHighlight, 1.0f));
    ImGui::Dummy(ImVec2(0, em * 0.7f));
}

// the panel's own look: a mist of blood at the foot, a thin inner frame and a bracket with a diamond in each corner
void DrawFrame() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    float em = ImGui::GetFontSize();
    ImVec2 end(p.x + sz.x, p.y + sz.y);
    dl->AddRectFilledMultiColor(p, end, U32(kHighlight, 0.0f), U32(kHighlight, 0.0f), U32(kHighlight, 0.30f),
                                U32(kHighlight, 0.30f));
    float in = em * 0.42f, len = em * 2.4f, thick = std::max(2.0f, em * 0.14f);
    ImVec2 a(p.x + in, p.y + in), b(end.x - in, end.y - in);
    dl->AddRect(a, b, U32(kBorder, 0.45f), 0.0f, 0, 1.0f);
    const ImVec2 corners[4] = {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)};
    const float sx[4] = {1, -1, -1, 1}, sy[4] = {1, 1, -1, -1};
    for (int i = 0; i < 4; i++) {
        ImVec2 c = corners[i];
        dl->AddLine(c, ImVec2(c.x + sx[i] * len, c.y), U32(kHeading), thick);
        dl->AddLine(c, ImVec2(c.x, c.y + sy[i] * len), U32(kHeading), thick);
        Diamond(dl, c, em * 0.34f, U32(kHeading));
        Diamond(dl, c, em * 0.17f, U32(kPanelBg, 1.0f));
        // a bead on each arm
        Diamond(dl, ImVec2(c.x + sx[i] * (len + em * 0.5f), c.y), em * 0.12f, U32(kHeading));
        Diamond(dl, ImVec2(c.x, c.y + sy[i] * (len + em * 0.5f)), em * 0.12f, U32(kHeading));
    }
}

// the wording the panel gets in English: a translated string is kept as it is, so no language loses its meaning
struct Alias {
    const char* english;
    const char* themed;
};
constexpr Alias kGothic[] = {
    {"SETTINGS", "SETTINGS"},
    {"GRAPHICS", "VISIONS"}, {"TOUCH CONTROLS", "TOUCH"}, {"GENERAL", "RITES"},
    {"KEYBOARD", "KEYS"}, {"CONTROLLER", "PAD"}, {"CREDITS", "SCRIBES"},
    {"DISPLAY", "THE LENS"}, {"PERFORMANCE", "THE PULSE"}, {"QUALITY", "THE VEIL"},
    {"TOUCH", "SPECTRAL TOUCH"}, {"SUPPORT", "OFFERINGS"},
    {"Back", "RETURN"}, {"Defaults", "RESTORE"},
};

std::string Themed(const char* label, bool caps = true) {
    std::string t = Ui(label, caps);
    if (t != label) return t;
    for (const Alias& a : kGothic)
        if (!std::strcmp(a.english, label)) return a.themed;
    return t;
}

// "S E T T I N G S": a space after each letter, three between words (UTF-8 aware)
std::string Spaced(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (c == ' ') out += "  ";
        else {
            out.append(s, i, n);
            out += ' ';
        }
        i += n;
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// a heading: a diamond, the name in gold, and a line that fades to the right
void Section(const char* title) {
    float em = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0, em * 0.5f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + em * 1.3f);
    ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    ImGui::TextUnformatted(Themed(title).c_str());
    ImGui::PopStyleColor();
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    float y = (mn.y + mx.y) * 0.5f;
    Diamond(dl, ImVec2(p.x + em * 0.5f, y), em * 0.28f, U32(kHeading));
    Diamond(dl, ImVec2(p.x + em * 0.5f, y), em * 0.12f, U32(kHighlight, 1.0f));
    FadeLine(dl, mx.x + em * 0.6f, p.x + w, y, 1.0f, kHeading, false);
    ImGui::Dummy(ImVec2(0, em * 0.25f));
}

constexpr float kValueEms = 12.0f;  // the value column's width

// a row's place and size: the label left, the value block (an arrow each side of the value) right
struct RowBox {
    float em;
    float row_w;
    float row_h;
    float value_w;
    float block_w;
    float row_x;
    float row_y;
};

RowBox MeasureRow() {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox box;
    box.em = ImGui::GetFontSize();
    box.row_w = ImGui::GetContentRegionAvail().x;
    box.row_h = ImGui::GetFrameHeight();
    box.value_w = box.em * kValueEms;
    box.block_w = box.row_h * 2 + box.value_w + style.ItemSpacing.x * 2;
    box.row_x = ImGui::GetCursorPosX();
    box.row_y = ImGui::GetCursorPosY();
    return box;
}

void RowLabel(const RowBox& box, const char* label) {
    ImGui::SetCursorPos(ImVec2(box.row_x + ImGui::GetStyle().ItemInnerSpacing.x, box.row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Ui(label).c_str());
}

// the cursor back to the row's start, then past the whole row
void EndRow(const RowBox& box) {
    ImGui::SetCursorPos(ImVec2(box.row_x, box.row_y));
    ImGui::Dummy(ImVec2(box.row_w, box.row_h));
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y), U32(kHeading, 0.13f), 1.0f);
}

void FillItem() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    constexpr ImVec4 kBlood{0.50f, 0.07f, 0.07f, 1.0f};
    ImU32 l = U32(kBlood, 0.95f), r = U32(kBlood, 0.18f);
    dl->AddRectFilledMultiColor(mn, mx, l, r, r, l);
    dl->AddRectFilled(mn, ImVec2(mn.x + std::max(2.0f, ImGui::GetFontSize() * 0.16f), mx.y), U32(kHeading));
}

// label left, arrows and value right, the row is the only nav stop
int OptionRow(const char* label, const char* value) {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox box = MeasureRow();
    ImGui::PushID(label);
    int step = 0;
    bool opened = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(box.row_w, box.row_h));
    ImGuiID id = ImGui::GetItemID();
    bool focused = ImGui::IsItemFocused();
    ImVec2 item_min = ImGui::GetItemRectMin(), item_max = ImGui::GetItemRectMax();
    // A on the controller opens the row for the D-pad, and closes it again; moving off the row ends it too
    if (opened && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) g_edit_id = g_edit_id == id ? 0 : id;
    if (g_edit_id == id && !focused) g_edit_id = 0;
    bool editing = g_edit_id == id;
    bool lit = (focused && !OnTabs()) || ImGui::IsItemHovered();
    if (lit) FillItem();
    if (editing) ImGui::GetWindowDrawList()->AddRect(item_min, item_max, U32(kHeading), 0.0f, 0, 2.0f);
    if (focused) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || (editing && ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft))) step = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || (editing && ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight))) step = 1;
    }
    RowLabel(box, label);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav | ImGuiItemFlags_ButtonRepeat, true);  // held arrows keep stepping
    // the arrows sit in small gilded boxes
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, U32(kHeading, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    float block_x = box.row_x + box.row_w - box.block_w;
    ImGui::SetCursorPos(ImVec2(block_x, box.row_y));
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) step = -1;
    float value_x = block_x + box.row_h + style.ItemSpacing.x;
    float pad = (box.value_w - ImGui::CalcTextSize(value).x) * 0.5f;
    ImGui::SetCursorPos(ImVec2(value_x + std::max(pad, 0.0f), box.row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, lit ? kHeading : kText);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(value_x + box.value_w + style.ItemSpacing.x, box.row_y));
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) step = 1;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();
    ImGui::PopID();
    EndRow(box);
    return step;
}

const char* OnOff(bool on) { return on ? "ON" : "OFF"; }

void Flip(bool& on, int step) {
    if (step) on = !on;
}

// applies and saves
void Changed() {
    audio::SetOutputMode(game::Opt().audio_output);
    Save();
}

enum RowId { kRenderResolution, kMaxFps, kVsync, kFpsCounter, kAniso, kReloadBlur, kSkipIntros, kSensitivity,
             kInvertMouse, kWalkSpeed, kFov, kViewmodelFov, kHoldAim, kDrawDistance, kAudioOutput, kLanguage,
             kFmvScale, kGyro, kGyroSensitivity, kTouchLayout, kTouchOpacity, kTouchSize, kTouchEdit,
             kInfiniteAmmo, kRapidFire, kNoRecoil, kGameSpeed, kGodMode, kOneHitKill, kInfiniteGrenades,
             kCheckUpdates, kTouchSensitivity, kUnlockCharacters, kPs2Buttons,
             kUnlockExtras, kInfiniteFatality, kMaxExperience, kMaxHealth, kMaxMana, kAspectRatio, kSkipCutscenes, kScreenFilter, kAntiAliasing,
             kFrameSkip, kFpsPosition, kFpsTemps, kUltrawide, kStickMode, kBloom, kFog, kInfiniteEnergy, kDonate,
             kGithub, kYoutube, kFpsUsage, kFrameSkipCount, kFogLayers, kParticles };

using rows::Kind;
using rows::Row;

// the recomp's settings in pages of their own, grouped within; the game's options menu shows the headings too
constexpr Row kGraphicsRows[] = {
    {Kind::Heading, -1, "DISPLAY"},
    {Kind::Value, kRenderResolution, "RENDER RESOLUTION"},
    {Kind::Value, kAspectRatio, "ASPECT RATIO"},
    {Kind::Value, kUltrawide, "ULTRAWIDE"},
    {Kind::Heading, -1, "PERFORMANCE"},
    {Kind::Value, kVsync, "VERTICAL SYNC"},
    {Kind::Value, kMaxFps, "MAX FRAME RATE"},
    {Kind::Value, kFrameSkip, "FRAME SKIP"},
    {Kind::Value, kFrameSkipCount, "FRAME SKIP COUNT"},
    {Kind::Value, kFpsCounter, "FPS COUNTER"},
    {Kind::Value, kFpsPosition, "FPS COUNTER POSITION"},
    {Kind::Value, kFpsUsage, "SHOW CPU / GPU USAGE"},
    {Kind::Value, kFpsTemps, "SHOW TEMPERATURES"},
    {Kind::Heading, -1, "QUALITY"},
    {Kind::Value, kAniso, "ANISOTROPIC FILTERING"},
    {Kind::Value, kAntiAliasing, "ANTI-ALIASING"},
    {Kind::Value, kBloom, "BLOOM"},
    {Kind::Value, kFog, "FOG"},
    {Kind::Value, kFogLayers, "FOG LAYERS"},
    {Kind::Value, kParticles, "PARTICLES"},
    {Kind::Value, kScreenFilter, "SCREEN FILTER"},
};

constexpr Row kTouchRows[] = {
    {Kind::Heading, -1, "TOUCH"},
    {Kind::Value, kTouchLayout, "CONTROLS LAYOUT"},
    {Kind::Value, kStickMode, "STICK MODE"},
    {Kind::Value, kTouchOpacity, "CONTROLS OPACITY"},
    {Kind::Value, kTouchSize, "CONTROLS SIZE"},
    {Kind::Link, kTouchEdit, "EDIT CONTROLS POSITION"},
};

// LANGUAGE is the settings panel's: the game takes its own from the console's language
constexpr Row kGeneralRows[] = {
    {Kind::Value, kLanguage, "LANGUAGE"},
    {Kind::Value, kAudioOutput, "AUDIO OUTPUT"},
};

// the credits' names, an info row's id; as they are, untranslated
const char* const kCreditNames[] = {"Loic \"Han\" Orleans", "Zukiru", "TGP482", "SDL, glslang, toml++, Dear ImGui",
                                    "xemu, Cxbx-Reloaded", "Rangga Casper"};

// a page of its own: who made the port, and the links to support it
constexpr Row kCreditRows[] = {
    {Kind::Heading, -1, "CREDITS"},
    {Kind::Info, 0, "ANDROID PORT"},
    {Kind::Info, 1, "HDR"},
    {Kind::Info, 2, "RECOMPILER AND RUNTIME"},
    {Kind::Info, 3, "LIBRARIES"},
    {Kind::Info, 4, "XBOX REFERENCES"},
    {Kind::Info, 5, "INSPIRED BY"},
    {Kind::Heading, -1, "SUPPORT"},
    {Kind::Link, kDonate, "DONATE"},
    {Kind::Link, kGithub, "GITHUB"},
    {Kind::Link, kYoutube, "YOUTUBE"},
};

// the guide and the unlockables: pages of plain paragraphs, in Indonesian
constexpr Row kGuideRows[] = {
    {Kind::Heading, -1, "GAMBARAN"},
    {Kind::Text, -1,
     "Kamu bermain sebagai Mio Amakura yang mencari saudari kembarnya, Mayu, di Desa Minakami yang dihantui. "
     "Game dibagi menjadi 9 bab. Bab ke-10 yang khusus hanya terbuka lewat tingkat kesulitan Hard dan Nightmare."},
    {Kind::Text, -1,
     "Kamu akan menjelajahi rumah-rumah dan area desa, seperti Rumah Osaka, Rumah Kurosawa, dan Rumah Tachibana, "
     "sambil mengikuti jejak Mayu dan memecahkan teka-teki di setiap tempat."},
    {Kind::Heading, -1, "KAMERA OBSCURA"},
    {Kind::Text, -1,
     "Kamera adalah senjata sekaligus alat bantu teka-teki. Foto hantu untuk melukai mereka, dan foto benda "
     "tertentu untuk mendapat petunjuk."},
    {Kind::Text, -1,
     "Saat hantu bersiap menyerang, ada kilatan 'Shutter Chance'. Jepret tepat di saat itu untuk peluang "
     "'Fatal Frame', yaitu beberapa jepretan beruntun dengan kerusakan lebih besar."},
    {Kind::Text, -1,
     "Film punya beberapa tipe (Type-07, 14, 61, dan 90). Angka yang lebih besar umumnya lebih kuat tetapi "
     "lebih langka, jadi hemat film kuat untuk lawan yang berat."},
    {Kind::Heading, -1, "MAYU"},
    {Kind::Text, -1,
     "Saat Mayu ikut bersamamu, dia bisa membantu menemukan petunjuk, tetapi hantu juga bisa menyerangnya. "
     "Sesekali lihat sekeliling. Ada juga bagian singkat di mana kamu mengendalikan Mayu."},
    {Kind::Heading, -1, "TEKA-TEKI"},
    {Kind::Text, -1,
     "Teka-teki memakai petunjuk dari catatan dan benda yang kamu temukan atau foto. Beberapa dibatasi waktu, jadi "
     "baca semua dokumen dengan teliti dan jangan lewatkan sudut ruangan."},
    {Kind::Heading, -1, "DAFTAR ROH"},
    {Kind::Text, -1,
     "Memotret roh yang berkeliaran mengisi Spirit List. Roh-roh itu tidak selalu menyerang, jadi pakai "
     "kesempatan itu untuk memotret mereka dan melengkapi daftar."},
    {Kind::Heading, -1, "CATATAN"},
    {Kind::Text, -1,
     "Halaman ini hanya panduan umum. Letak item dan rute tiap bab tidak dimuat di sini, karena belum bisa "
     "dipastikan akurat."},
};

constexpr Row kUnlockRows[] = {
    {Kind::Heading, -1, "BONUS SETELAH TAMAT"},
    {Kind::Text, -1,
     "Tamat di Normal: membuka Hard, 2 kostum tambahan, dan kacamata untuk Mio."},
    {Kind::Text, -1,
     "Tamat di Hard: membuka Nightmare, opsi Album, ending tambahan (True Ending), 2 kostum, dan Stop Lens."},
    {Kind::Text, -1,
     "Tamat di Nightmare: ending alternatif, 2 kostum, Crash Lens, dokumen pengembangan, dan urutan 'The Promise'."},
    {Kind::Text, -1,
     "Bab ke-10 yang khusus terbuka dari tingkat Hard dan Nightmare."},
    {Kind::Heading, -1, "KHUSUS DIRECTOR'S CUT (XBOX)"},
    {Kind::Text, -1,
     "Versi Xbox menambah mode orang pertama, Survival Mode, ending baru yang eksklusif, dan lebih banyak kostum."},
    {Kind::Text, -1,
     "Kostum tambahan dibuka lewat berbagai mode: bikini biru dan pink dari Normal, kostum Deception dari Hard, "
     "kostum DOA (Kasumi dan Ayane) dari Mission Mode, bikini bunga dari Nightmare atau Fatal Mode."},
    {Kind::Heading, -1, "CATATAN"},
    {Kind::Text, -1,
     "Daftar ini dari sumber komunitas, dan detail kostum bisa berbeda antar sumber. Setahuku game ini tidak punya "
     "kode cheat resmi: semua bonus dibuka dengan menamatkan game atau mode."},
};

// a page of its own, apart from the recomp's settings
constexpr Row kCheatRows[] = {
    {Kind::Heading, -1, "WEAPONS"},
    {Kind::Value, kInfiniteAmmo, "INFINITE AMMO"},
    {Kind::Value, kInfiniteGrenades, "INFINITE GRENADES"},
    {Kind::Value, kRapidFire, "RAPID FIRE"},
    {Kind::Value, kNoRecoil, "NO RECOIL"},
    {Kind::Heading, -1, "PLAYER"},
    {Kind::Value, kGodMode, "GOD MODE"},
    {Kind::Value, kOneHitKill, "ONE-HIT KILL"},
    {Kind::Heading, -1, "GAME"},
    {Kind::Value, kGameSpeed, "GAME SPEED"},
};

const std::vector<Row>& InputRows() {
    static const std::vector<Row> list = [] {
        std::vector<Row> out = {{Kind::Heading, -1, "KEYBOARD"}};
        size_t count = 0;
        const input::Action* actions = input::Actions(&count);
        for (size_t i = 0; i < count; i++) out.push_back({Kind::Bind, int(i), actions[i].label});
        return out;
    }();
    return list;
}

const std::vector<Row>& PadRows() {
    static const std::vector<Row> list = [] {
        std::vector<Row> out = {{Kind::Heading, -1, "CONTROLLER"}};
        size_t count = 0;
        const input::Action* actions = input::PadActions(&count);
        for (size_t i = 0; i < count; i++) out.push_back({Kind::PadBind, int(i), actions[i].label});
        return out;
    }();
    return list;
}

// the setting an ON / OFF row flips, nullptr for any other row
bool* ToggleFlag(Settings& s, int id) {
    game::Options& o = game::Opt();
    switch (id) {
        case kVsync: return &s.vsync;
        case kFpsCounter: return &o.fps_counter;
        case kFrameSkip: return &o.frame_skip;
        case kUltrawide: return &o.ultrawide;
        case kFpsTemps: return &o.fps_temps;
        case kFpsUsage: return &o.fps_usage;
        case kReloadBlur: return &o.reload_blur;
        case kBloom: return &o.bloom;
        case kFog: return &o.fog;
        case kFogLayers: return &o.fog_layers;
        case kSkipIntros: return &o.skip_intros;
        case kInvertMouse: return &o.invert_mouse;
        case kHoldAim: return &o.hold_aim;
        case kInfiniteAmmo: return &o.infinite_ammo;
        case kNoRecoil: return &o.no_recoil;
        case kGodMode: return &o.god_mode;
        case kOneHitKill: return &o.one_hit_kill;
        case kInfiniteGrenades: return &o.infinite_grenades;
        case kUnlockCharacters: return &o.unlock_characters;
        case kPs2Buttons: return &o.ps2_buttons;
        case kUnlockExtras: return &o.unlock_extras;
        case kInfiniteFatality: return &o.infinite_fatality;
        case kInfiniteEnergy: return &o.infinite_energy;
        case kMaxExperience: return &o.max_experience;
        case kMaxHealth: return &o.max_health;
        case kMaxMana: return &o.max_mana;
        case kSkipCutscenes: return &o.skip_cutscenes;
        default: return nullptr;
    }
}

// both field of view rows: 45 to 140 in steps of 1, 70 the game's own
constexpr int kFovMin = 45;
constexpr int kFovMax = 140;
constexpr uint32_t kGameFov = 70;

void FovText(uint32_t fov, char (&text)[64]) {
    std::snprintf(text, sizeof(text), fov == kGameFov ? "%u (DEFAULT)" : "%u", fov);
}

// the display rows that are no ON / OFF, stepped first; false for another row
bool StepDisplayRow(int id, int step, Settings& s, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kRenderResolution: {
            // 20% to 200% in steps of 5
            if (step) s.render_percent = uint32_t(StepRange(int(s.render_percent), 20, 200, 5, step));
            Mode out = Output();
            std::snprintf(text, sizeof(text), "%u%% (%ux%u)", s.render_percent, out.width * s.render_percent / 100,
                          out.height * s.render_percent / 100);
            return true;
        }
        case kAspectRatio: {
            // 1:1 to the whole screen, an unknown saved value the whole screen's
            constexpr int count = int(std::size(game::kAspects));
            int i = count - 1;
            for (int j = 0; j < count; j++)
                if (game::kAspects[j].code == o.aspect_ratio) i = j;
            if (step) {
                i = StepIndex(i, step, count);
                o.aspect_ratio = game::kAspects[i].code;
            }
            std::snprintf(text, sizeof(text), "%s", game::kAspects[i].name);
            return true;
        }
        case kFmvScale:
            std::snprintf(text, sizeof(text), "%s", kFmvScaleNames[StepTable(kFmvScales, o.fmv_scale, step)]);
            return true;
        case kFov:
            if (step) o.fov = uint32_t(StepRange(int(o.fov), kFovMin, kFovMax, 1, step));
            FovText(o.fov, text);
            return true;
        case kViewmodelFov:
            if (step) o.viewmodel_fov = uint32_t(StepRange(int(o.viewmodel_fov), kFovMin, kFovMax, 1, step));
            FovText(o.viewmodel_fov, text);
            return true;
        case kParticles: {
            // the effect sprites the game draws one by one: all of them, half, a quarter or none
            static const char* const kNames[] = {"ALL", "HALF", "A QUARTER", "OFF"};
            if (step) o.particles = uint32_t(StepRange(int(o.particles), 0, 3, 1, step));
            std::snprintf(text, sizeof(text), "%s", kNames[std::min<uint32_t>(o.particles, 3)]);
            return true;
        }
        case kFrameSkipCount:
            // 0 (OFF) to 9 frames left out after each one shown, as a console emulator's frame skip
            if (step) o.frame_skip_n = uint32_t(StepRange(int(o.frame_skip_n), 0, 9, 1, step));
            if (o.frame_skip_n) std::snprintf(text, sizeof(text), "%u", o.frame_skip_n);
            else std::snprintf(text, sizeof(text), "OFF");
            return true;
        case kMaxFps: {
            uint32_t cap = kFpsCaps[StepTable(kFpsCaps, s.fps_cap, step)];
            if (cap == kFpsCapRefresh) std::snprintf(text, sizeof(text), "REFRESH RATE");
            else if (cap) std::snprintf(text, sizeof(text), cap == 60 ? "%u (DEFAULT)" : "%u", cap);
            else std::snprintf(text, sizeof(text), "UNLIMITED");
            return true;
        }
        default:
            return false;
    }
}

// the graphics rows that are no ON / OFF, stepped first; false for another row
bool StepGraphicsRow(int id, int step, Settings& s, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kAniso:
            std::snprintf(text, sizeof(text), "%s", kAnisoNames[StepTable(kAnisoValues, s.aniso, step)]);
            return true;
        case kAntiAliasing:
            std::snprintf(text, sizeof(text), "%s", kAntiAliasingNames[StepTable(kAntiAliasings, o.anti_aliasing, step)]);
            return true;
        case kScreenFilter:
            std::snprintf(text, sizeof(text), "%s", kScreenFilterNames[StepTable(kScreenFilters, o.screen_filter, step)]);
            return true;
        case kFpsPosition:
            std::snprintf(text, sizeof(text), "%s", kFpsPositionNames[StepTable(kFpsPositions, o.fps_position, step)]);
            return true;
        case kDrawDistance:
            std::snprintf(text, sizeof(text), "%s", kDistanceNames[StepTable(kDistances, o.draw_distance, step)]);
            return true;
        default:
            return false;
    }
}

// the cheat rows that are no ON / OFF, stepped first; false for another row
bool StepCheatRow(int id, int step, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kRapidFire:
            // off, then 2 to 4 times the rate
            if (step) o.rapid_fire = uint32_t(StepRange(int(o.rapid_fire), 1, 4, 1, step));
            if (o.rapid_fire <= 1) std::snprintf(text, sizeof(text), "OFF");
            else std::snprintf(text, sizeof(text), "%uX", o.rapid_fire);
            return true;
        case kGameSpeed:
            // 25% to 200% in steps of 25
            if (step) o.game_speed = uint32_t(StepRange(int(o.game_speed), 25, 200, 25, step));
            std::snprintf(text, sizeof(text), o.game_speed == 100 ? "%u%% (DEFAULT)" : "%u%%", o.game_speed);
            return true;
        default:
            return false;
    }
}

// the general, mouse and keyboard rows that are no ON / OFF, stepped first
void StepInputRow(int id, int step, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kAudioOutput:
            std::snprintf(text, sizeof(text), "%s", kAudioOutputNames[StepTable(kAudioOutputs, o.audio_output, step)]);
            break;
        case kSensitivity:
            // 0.1 to 5.0 in steps of 0.1
            if (step) {
                int tenths = StepRange(int(std::lround(o.mouse_sensitivity * 10.0f)), 1, 50, 1, step);
                o.mouse_sensitivity = float(tenths) / 10.0f;
            }
            std::snprintf(text, sizeof(text), "%.1f", o.mouse_sensitivity);
            break;
        case kTouchSensitivity:
            // as the mouse's: 0.1 to 5.0 in steps of 0.1
            if (step) {
                int tenths = StepRange(int(std::lround(o.touch_sensitivity * 10.0f)), 1, 50, 1, step);
                o.touch_sensitivity = float(tenths) / 10.0f;
            }
            std::snprintf(text, sizeof(text), "%.1f", o.touch_sensitivity);
            break;
        case kWalkSpeed:
            // 10% to 100% in steps of 5
            if (step) o.walk_speed = uint32_t(StepRange(int(o.walk_speed), 10, 100, 5, step));
            std::snprintf(text, sizeof(text), "%u%%", o.walk_speed);
            break;
        case kGyro:
            // off, always on, while firing
            if (step) gyro::SetMode(uint32_t(StepIndex(int(gyro::CurrentMode()), step, 3)));
            std::snprintf(text, sizeof(text), "%s", gyro::CurrentMode() == 0 ? "OFF"
                                                    : gyro::CurrentMode() == 1 ? "ALWAYS"
                                                                               : "WHILE FIRING");
            break;
        case kTouchLayout:
            // the presets, CUSTOM once a control was moved
            if (step) touch::StepLayout(step);
            std::snprintf(text, sizeof(text), "%s", touch::LayoutName());
            break;
        case kStickMode:
            // floating sticks come where the thumb lands, fixed ones stay where they are drawn
            if (step) touch::StepStickMode(step);
            std::snprintf(text, sizeof(text), "%s", touch::StickModeName());
            break;
        case kTouchOpacity:
            if (step) touch::StepOpacity(step);
            std::snprintf(text, sizeof(text), "%u%%", touch::ControlsOpacity());
            break;
        case kTouchSize:
            if (step) touch::StepSize(step);
            std::snprintf(text, sizeof(text), "%u%%", touch::ControlsSize());
            break;
        case kCheckUpdates:
            // any step asks; the answer comes in a dialog over the game
            if (step) host::CheckForUpdates();
            std::snprintf(text, sizeof(text), "CHECK");
            break;
        case kGyroSensitivity:
            // 0.1 to 5.0 in steps of 0.1
            if (step) gyro::SetSensitivity(float(StepRange(int(std::lround(gyro::Sensitivity() * 10.0f)), 1, 50, 1, step)) / 10.0f);
            std::snprintf(text, sizeof(text), "%.1f", gyro::Sensitivity());
            break;
        case kLanguage: {
            int count = int(std::size(game::kLanguages));
            int i = 0;
            while (i < count - 1 && o.language != game::kLanguages[i]) i++;
            // past the languages this disc has no strings for; English and Indonesian are always there
            if (step) {
                do i = StepIndex(i, step, count);
                while (!game::LanguageAvailable(game::kLanguages[i]));
                o.language = game::kLanguages[i];
            }
            const char* format = o.language == game::BootLanguage() ? "%s" : "%s (RESTART)";
            std::snprintf(text, sizeof(text), format, kLanguageNames[i]);
            break;
        }
    }
}

// steps the value first when step is not zero
std::string RowText(int id, int step, Settings& s = Get()) {
    char text[64] = "";
    if (bool* on = ToggleFlag(s, id)) {
        Flip(*on, step);
        std::snprintf(text, sizeof(text), "%s", OnOff(*on));
    } else if (!StepDisplayRow(id, step, s, text) && !StepGraphicsRow(id, step, s, text) &&
               !StepCheatRow(id, step, text)) {
        StepInputRow(id, step, text);
    }
    if (step && &s == &Get()) Changed();
    return text;
}

constexpr const char* kUnbound = "UNBOUND";

std::string KeyText(std::string keys) {
    if (keys.empty()) return kUnbound;
    for (char& c : keys) c = c == ',' ? '/' : char(std::toupper(uint8_t(c)));
    return keys;
}

// as options_menu.cpp's captions
const char* const kPages[] = {"GRAPHICS", "TOUCH CONTROLS", "GENERAL", "KEYBOARD", "CONTROLLER", "GUIDE", "UNLOCKS", "CHEATS", "CREDITS"};
constexpr int kSettingsPage = 0;
constexpr int kTouchPage = 1;
constexpr int kGeneralPage = 2;
constexpr int kKeyboardPage = 3;
constexpr int kControllerPage = 4;
constexpr int kGuidePage = 5;
constexpr int kUnlocksPage = 6;
constexpr int kCheatsTab = 7;
constexpr int kCreditsPage = 8;
constexpr int kCheatsPage = -1;  // Black's weapon cheats, none here

void RestoreDefaults(int page) {
    if (page == kCreditsPage || page == kGuidePage || page == kUnlocksPage || page == kCheatsTab) return;  // nothing to reset
    game::Options& o = game::Opt();
    if (page == kCheatsPage) {
        game::Options none;
        o.infinite_ammo = none.infinite_ammo;
        o.rapid_fire = none.rapid_fire;
        o.no_recoil = none.no_recoil;
        o.game_speed = none.game_speed;
        o.god_mode = none.god_mode;
        o.one_hit_kill = none.one_hit_kill;
        o.infinite_grenades = none.infinite_grenades;
        Changed();
        return;
    }
    if (page == kKeyboardPage) {
        o.mouse_sensitivity = game::Options{}.mouse_sensitivity;
        o.invert_mouse = false;
        o.hold_aim = true;
        o.walk_speed = game::Options{}.walk_speed;
        input::ResetBindings();
        return;
    }
    if (page == kControllerPage) {
        input::ResetPadBindings();
        return;
    }
    game::Options keep = o;
    Get() = DefaultSettings();
    o = game::Options{};
    o.mouse_sensitivity = keep.mouse_sensitivity;
    o.invert_mouse = keep.invert_mouse;
    o.hold_aim = keep.hold_aim;
    o.walk_speed = keep.walk_speed;
    o.keys = keep.keys;
    o.pad = keep.pad;
    // the cheats page has its own defaults
    o.infinite_ammo = keep.infinite_ammo;
    o.rapid_fire = keep.rapid_fire;
    o.no_recoil = keep.no_recoil;
    o.game_speed = keep.game_speed;
    o.god_mode = keep.god_mode;
    o.one_hit_kill = keep.one_hit_kill;
    o.infinite_grenades = keep.infinite_grenades;
    Changed();
}

// the row being rebound, -1 when idle; its keys must be released before the next press binds
std::atomic<int> g_capture{-1};
int g_capture_slot = 0;  // its box: 0 the key, 1 the alternate
int g_hovered_box = -1;  // the box under the mouse or focused with a key in it, this frame
bool g_capture_armed = false;
int g_capture_done = 0;  // frame of the last bind, the key that bound must not start another
// the controller row being rebound, -1 when idle: as the keys', its inputs let go before the next press binds; Escape
// or kPadCaptureMs with no press cancels, for a player with the controller alone
std::atomic<int> g_pad_capture{-1};
bool g_pad_capture_armed = false;
std::chrono::steady_clock::time_point g_pad_capture_since;
constexpr std::chrono::seconds kPadCaptureTimeout{5};
int g_page = kSettingsPage;
// the action's key and its alternate as their boxes show them, the alternate blank when there is none
std::vector<std::string> KeyBoxes(size_t action) {
    std::string keys = input::Binding(action);
    size_t comma = keys.find(',');
    std::string second;
    if (comma != std::string::npos) second = keys.substr(comma + 1);
    second = second.substr(0, second.find(','));
    if (!second.empty()) second = KeyText(second);
    return {KeyText(keys.substr(0, comma)), second};
}

constexpr float kKeyBoxEms = 10.0f;  // a key box's width when the row has two

// label left, a box per key right (the key and its alternate side by side), ending where the other rows' values do;
// Enter or click starts a box's capture, right click or Delete sets `cleared`; returns the box, -1 for none
int BindRow(const char* label, const std::vector<std::string>& keys, int capturing, int& cleared,
            const char* prompt = "PRESS A KEY") {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox row = MeasureRow();
    float value_end = row.row_x + row.row_w - row.block_w + row.row_h + style.ItemSpacing.x + row.value_w;
    int n = int(keys.size());
    int pressed = -1;
    float box_w = n > 1 ? row.em * kKeyBoxEms : row.value_w;
    ImGui::PushID(label);
    RowLabel(row, label);
    for (int i = 0; i < n; i++) {
        float x = value_end - float(n - i) * box_w - float(n - 1 - i) * style.ItemSpacing.x;
        ImGui::SetCursorPos(ImVec2(x, row.row_y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##key", ImVec2(box_w, row.row_h))) pressed = i;
        bool deleted = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || deleted) cleared = i;
        bool pointed = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        if (pointed && !keys[i].empty() && keys[i] != kUnbound) g_hovered_box = i;
        bool lit = capturing == i || ImGui::IsItemFocused() || ImGui::IsItemHovered();
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                  ImGui::GetColorU32(lit ? ImGuiCol_Header : ImGuiCol_FrameBg));
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            U32(kHeading, lit ? 0.9f : 0.35f), 0.0f, 0, 1.0f);
        // key names as they are
        std::string value = keys[i];
        if (capturing == i) value = Ui(prompt);
        else if (value == kUnbound) value = Ui(kUnbound);
        float pad = (box_w - ImGui::CalcTextSize(value.c_str()).x) * 0.5f;
        ImGui::SetCursorPos(ImVec2(x + std::max(pad, 0.0f), row.row_y));
        ImGui::AlignTextToFramePadding();
        if (capturing == i) ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
        ImGui::TextUnformatted(value.c_str());
        if (capturing == i) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::PopID();
    EndRow(row);
    return pressed;
}

// Escape cancels, any other bindable key or mouse button binds
void PollCapture() {
    if (g_capture < 0) return;
    int vk = input::PressedKey();
    if (!g_capture_armed) {
        g_capture_armed = !vk;
        return;
    }
    if (!vk) return;
    if (vk != VK_ESCAPE) input::Bind(size_t(g_capture.load()), vk, g_capture_slot);
    g_capture = -1;
    g_capture_done = ImGui::GetFrameCount();
}

// any input of a controller binds once those held when it began are let go
void PollPadCapture() {
    if (g_pad_capture < 0) return;
    std::string input = input::PressedPadInput();
    bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                  std::chrono::steady_clock::now() - g_pad_capture_since > kPadCaptureTimeout;
    if (!cancel && !g_pad_capture_armed) {
        g_pad_capture_armed = input.empty();
        return;
    }
    if (!cancel && input.empty()) return;
    if (!cancel) input::PadBind(size_t(g_pad_capture.load()), input);
    g_pad_capture = -1;
    g_capture_done = ImGui::GetFrameCount();
}

// not under a bind or on the button that just bound
bool CaptureFree() { return g_capture < 0 && g_pad_capture < 0 && ImGui::GetFrameCount() > g_capture_done + 1; }

void ValueRow(const Row& r) {
    int step = OptionRow(r.label, Ui(RowText(r.id, 0)).c_str());
    if (step) RowText(r.id, step);
}

const char* LinkUrl(int id) {
    switch (id) {
        case kDonate: return kDonateUrl;
        case kGithub: return kGithubUrl;
        case kYoutube: return kYoutubeUrl;
        default: return nullptr;
    }
}

// label left, OPEN right; a tap, click, Enter or A anywhere on the row opens the page in the browser, or the touch
// layout's editor
void LinkRow(const Row& r) {
    RowBox box = MeasureRow();
    ImGui::PushID(r.label);
    bool open = ImGui::Selectable("##link", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(box.row_w, box.row_h));
    if ((ImGui::IsItemFocused() && !OnTabs()) || ImGui::IsItemHovered()) FillItem();
    RowLabel(box, r.label);
    std::string value = Ui("OPEN");
    float value_x = box.row_x + box.row_w - box.block_w + box.row_h + ImGui::GetStyle().ItemSpacing.x;
    float pad = (box.value_w - ImGui::CalcTextSize(value.c_str()).x) * 0.5f;
    ImGui::SetCursorPos(ImVec2(value_x + std::max(pad, 0.0f), box.row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopStyleColor();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        float y = (mn.y + mx.y) * 0.5f, r = box.em * 0.2f;
        Diamond(dl, ImVec2(mn.x - box.em * 0.7f, y), r, U32(kHeading));
        Diamond(dl, ImVec2(mx.x + box.em * 0.7f, y), r, U32(kHeading));
    }
    ImGui::PopID();
    EndRow(box);
    if (!open) return;
    if (r.id == kTouchEdit) touch::EditLayout();
    else if (const char* url = LinkUrl(r.id)) SDL_OpenURL(url);
}

// the role left, the name right-aligned at the row's end
void InfoRow(const Row& r) {
    RowBox box = MeasureRow();
    RowLabel(box, r.label);
    if (r.id >= 0 && r.id < int(std::size(kCreditNames))) {
        const char* name = kCreditNames[r.id];
        float x = box.row_x + box.row_w - ImGui::CalcTextSize(name).x - ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetCursorPos(ImVec2(x, box.row_y));
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
        ImGui::TextUnformatted(name);
        ImGui::PopStyleColor();
    }
    EndRow(box);
}

// a paragraph, wrapped to the panel's width
void TextRow(const Row& r) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(r.label);
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
}

void KeyRow(const Row& r) {
    int cleared = -1;
    int box = BindRow(r.label, KeyBoxes(size_t(r.id)), g_capture == r.id ? g_capture_slot : -1, cleared);
    // a Delete that answered a capture is no clear
    if (cleared >= 0 && CaptureFree()) input::Bind(size_t(r.id), 0, cleared);
    if (box < 0 || !CaptureFree()) return;
    g_capture_slot = box;
    g_capture = r.id;
    g_capture_armed = false;
}

// the bound input, else the mapping's; a clear goes back to the mapping
void PadRow(const Row& r) {
    size_t count = 0;
    const input::Action& action = input::PadActions(&count)[r.id];
    std::string bound = input::PadBinding(size_t(r.id));
    int cleared = -1;
    int box = BindRow(r.label, {bound.empty() ? action.defaults : bound}, g_pad_capture == r.id ? 0 : -1, cleared,
                      "PRESS A BUTTON");
    if (cleared >= 0 && CaptureFree()) input::PadBind(size_t(r.id), "");
    if (box < 0 || !CaptureFree()) return;
    g_pad_capture = r.id;
    g_pad_capture_armed = false;
    g_pad_capture_since = std::chrono::steady_clock::now();
}

void DrawRowList(const Row* list, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const Row& r = list[i];
        if (r.kind == Kind::Heading) Section(r.label);
        else if (r.kind == Kind::Value) ValueRow(r);
        else if (r.kind == Kind::Link) LinkRow(r);
        else if (r.kind == Kind::Info) InfoRow(r);
        else if (r.kind == Kind::PadBind) PadRow(r);
        else if (r.kind == Kind::Text) TextRow(r);
        else KeyRow(r);
    }
}

// The guide page shows the player's own walkthrough.txt when there is one: in the FatalFrame2 folder beside the game,
// else in the app's own folder. A line starting "# " opens a chapter (a header that folds), "## " is a subheading in
// it, any other line is a paragraph and a blank line is a gap. The file is read again when it changes. Without one the
// page shows the built-in guide and how to add the file.
struct GuideLine {
    bool sub;
    std::string text;  // empty: a gap
};
struct GuideChapter {
    std::string title;
    std::vector<GuideLine> lines;
};
std::vector<GuideChapter> g_guide;
std::string g_guide_file;  // where g_guide came from, empty when no file
std::filesystem::file_time_type g_guide_time;
std::chrono::steady_clock::time_point g_guide_checked;
constexpr const char* kGuidePaths[] = {"/storage/emulated/0/FatalFrame2/walkthrough.txt",
                                       "/storage/emulated/0/Android/data/com.fatalframe2.recomp/files/walkthrough.txt"};
constexpr size_t kGuideMaxBytes = 4u << 20;  // reads no more than this

bool ReadGuide(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<GuideChapter> out;
    std::string line;
    size_t total = 0;
    bool first = true;
    while (std::getline(f, line) && total < kGuideMaxBytes) {
        total += line.size() + 1;
        if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // a BOM
        first = false;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.compare(0, 2, "# ") == 0) {
            out.push_back({line.substr(2), {}});
            continue;
        }
        if (line.empty()) {  // a gap, never first in a chapter or twice in a row
            if (!out.empty() && !out.back().lines.empty() && !out.back().lines.back().text.empty())
                out.back().lines.push_back({false, ""});
            continue;
        }
        if (out.empty()) out.push_back({"WALKTHROUGH", {}});
        if (line.compare(0, 3, "## ") == 0) out.back().lines.push_back({true, line.substr(3)});
        else out.back().lines.push_back({false, line});
    }
    if (out.empty()) return false;
    g_guide = std::move(out);
    return true;
}

// at most every 1.5 s: the file there and changed, or gone
void RefreshGuide() {
    auto now = std::chrono::steady_clock::now();
    if (now - g_guide_checked < std::chrono::milliseconds(1500)) return;
    g_guide_checked = now;
    for (const char* path : kGuidePaths) {
        std::error_code ec;
        auto time = std::filesystem::last_write_time(path, ec);
        if (ec) continue;
        if (g_guide_file == path && time == g_guide_time) return;
        if (ReadGuide(path)) {
            g_guide_file = path;
            g_guide_time = time;
            return;
        }
    }
    g_guide.clear();
    g_guide_file.clear();
}

// the guide's text is drawn this much larger than the rest of the menu, for readability (1 = as the other pages)
constexpr float kGuideTextScale = 1.5f;

// scales the current window's text for as long as it lives, and puts it back on every way out of DrawGuide
struct GuideTextScale {
    GuideTextScale() { ImGui::SetWindowFontScale(kGuideTextScale); }
    ~GuideTextScale() { ImGui::SetWindowFontScale(1.0f); }
};

void DrawGuide() {
    GuideTextScale text_scale;
    RefreshGuide();
    if (g_guide.empty()) {
        Section("TAMBAHKAN PANDUANMU");
        TextRow({Kind::Text, -1,
                 "Taruh file walkthrough.txt di folder FatalFrame2 (tempat game berada), lalu kembali ke tab ini. "
                 "Awali tiap bab dengan '# ', subjudul dengan '## '. Selain itu, satu baris = satu paragraf, dan "
                 "baris kosong = jarak. Simpan sebagai UTF-8, tanpa karakter khusus."});
        DrawRowList(kGuideRows, std::size(kGuideRows));
        return;
    }
    static int close_chapter = -1;  // Y closed this chapter's text: its header takes the focus back
    int inside = -1;                // the chapter whose text holds the focus
    for (size_t i = 0; i < g_guide.size(); i++) {  // the chapters fold: only an open one is drawn
        const GuideChapter& c = g_guide[i];
        std::string head = c.title + "###guide" + std::to_string(i);
        if (close_chapter == int(i)) {
            ImGui::SetNextItemOpen(false);
            ImGui::SetKeyboardFocusHere();
            close_chapter = -1;
        }
        if (!ImGui::CollapsingHeader(head.c_str())) continue;
        for (size_t j = 0; j < c.lines.size(); j++) {
            const GuideLine& l = c.lines[j];
            if (l.text.empty()) {
                ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
                continue;
            }
            // each paragraph is a stop of its own, so the D-pad reads down a long chapter
            ImVec2 at = ImGui::GetCursorPos();
            ImVec2 ts = ImGui::CalcTextSize(l.text.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x);
            ImGui::PushID(int(i * 10000 + j));
            ImGui::Selectable("##para", false, 0, ImVec2(0, ts.y));
            if (ImGui::IsItemFocused() && !OnTabs()) inside = int(i);
            ImGui::PopID();
            ImGui::SetCursorPos(at);
            ImGui::PushTextWrapPos(0.0f);
            if (l.sub) ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
            ImGui::TextUnformatted(l.text.c_str());
            if (l.sub) ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
    }
    // Y inside a chapter closes it and puts the focus on its header
    if (g_back && inside >= 0) {
        close_chapter = inside;
        g_back = false;
    }
}

// The cheats page: the cheats of cheats.txt with an ON / OFF each, and the scanner that finds a value's address in
// the game's memory. Find a number the game shows (the film count, say), scan for it, change it in the game, scan for
// the new number, and the address is left; ADD puts it in cheats.txt at the value it holds now.
struct ScanUi {
    int type = 2;  // 0 1 byte, 1 2 bytes, 2 4 bytes, 3 float
    bool all_ram = false;
    int ival = 0;
    float fval = 0;
};
ScanUi g_scan_ui;
constexpr char kScanTypes[] = {'b', 'w', 'd', 'f'};

bool HalfButton(const char* label) {
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    return ImGui::Button(label, ImVec2(w, 0));
}

void DrawCheats() {
    game::CheatsRefresh();
    Section("CHEATS");
    std::vector<game::CheatEntry> list = game::CheatsList();
    if (list.empty())
        TextRow({Kind::Text, -1,
                 "Belum ada cheat. Temukan alamatnya dengan pemindai di bawah lalu tekan ADD, atau tulis sendiri di "
                 "cheats.txt (folder FatalFrame2), satu baris tiap cheat: nama | 0xALAMAT | b/w/d/f | nilai"});
    for (size_t i = 0; i < list.size(); i++) {
        ImGui::PushID(int(i));
        int step = OptionRow(list[i].name.c_str(), Ui(list[i].on ? "ON" : "OFF").c_str());
        ImGui::PopID();
        if (step) game::CheatSet(i, !list[i].on);
    }
    Section("PEMINDAI MEMORI");
    TextRow({Kind::Text, -1,
             "Cari angka yang tampil di game (misalnya jumlah film). Pindai nilainya, ubah angka itu di game, pindai "
             "nilai barunya, sampai tinggal sedikit alamat. 'ALL RAM' lebih luas tetapi bisa menutup app kalau "
             "memorinya belum terpetakan; mulailah dari GLOBALS."});
    int step = OptionRow("RANGE", g_scan_ui.all_ram ? "ALL RAM" : "GLOBALS");
    if (step) g_scan_ui.all_ram = !g_scan_ui.all_ram;
    step = OptionRow("TYPE", game::ScanTypeName(kScanTypes[g_scan_ui.type]));
    if (step) g_scan_ui.type = StepIndex(g_scan_ui.type, step, 4);
    char type = kScanTypes[g_scan_ui.type];
    if (type == 'f') ImGui::InputFloat("VALUE", &g_scan_ui.fval, 1.0f, 10.0f, "%.3f");
    else ImGui::InputInt("VALUE", &g_scan_ui.ival, 1, 10);
    double value = type == 'f' ? double(g_scan_ui.fval) : double(g_scan_ui.ival);
    if (HalfButton("FIRST SCAN")) game::ScanFirst(type, g_scan_ui.all_ram, value);
    ImGui::SameLine();
    if (HalfButton("FIRST SCAN: UNKNOWN")) game::ScanFirstUnknown(type);
    if (HalfButton("NEXT: EQUAL")) game::ScanNext(game::ScanMode::Equal, value);
    ImGui::SameLine();
    if (HalfButton("NEXT: CHANGED")) game::ScanNext(game::ScanMode::Changed, 0);
    if (HalfButton("NEXT: UNCHANGED")) game::ScanNext(game::ScanMode::Unchanged, 0);
    ImGui::SameLine();
    if (HalfButton("NEXT: INCREASED")) game::ScanNext(game::ScanMode::Increased, 0);
    if (HalfButton("NEXT: DECREASED")) game::ScanNext(game::ScanMode::Decreased, 0);
    ImGui::SameLine();
    if (HalfButton("RESET")) game::ScanReset();
    size_t count = game::ScanCount();
    char head[64];
    std::snprintf(head, sizeof(head), "HASIL: %zu alamat", count);
    ImGui::TextUnformatted(head);
    if (count == 0 || count > 40) {
        if (count > 40) ImGui::TextUnformatted("Persempit dulu sampai 40 alamat atau kurang.");
        return;
    }
    for (const game::ScanHit& h : game::ScanResults(40)) {
        char line[64];
        std::snprintf(line, sizeof(line), "%08X  =  %.6g", h.addr, h.value);
        ImGui::PushID(int(h.addr));
        ImGui::TextUnformatted(line);
        ImGui::SameLine();
        if (ImGui::Button("ADD")) {
            char name[32];
            std::snprintf(name, sizeof(name), "CHEAT %08X", h.addr);
            game::CheatAdd(name, h.addr, type, h.value);
        }
        ImGui::PopID();
    }
}

void DrawRows(int page) {
    if (page == kCheatsTab) {
        DrawCheats();
        return;
    }
    if (page == kGuidePage) {
        DrawGuide();
        return;
    }
    size_t count = 0;
    const Row* list = rows::Page(page, &count);
    DrawRowList(list, count);
}

void Defaults() {
    g_capture = -1;
    g_pad_capture = -1;
    RestoreDefaults(g_page);
}

// one tab a page, as wide as they share the line; LB / RB step them
void DrawTabs() {
    int step = ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) - ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false);
    // the D-pad's Left / Right step the tabs while it is on them
    if (OnTabs()) step += ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) - ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false);
    // a selection made here shows from the next frame, the tab drawn this one still reads as selected
    bool set = step && CaptureFree();
    int pages = int(std::size(kPages));
    if (set) {
        g_page = StepIndex(g_page, step, pages);
        g_edit_id = 0;
    }
    ImGui::PushStyleColor(ImGuiCol_Tab, kTab);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, kTabHovered);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, kHighlight);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, kHeading);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
    if (ImGui::BeginTabBar("##pages")) {
        float gaps = ImGui::GetStyle().ItemInnerSpacing.x * float(pages - 1);
        float w = (ImGui::GetContentRegionAvail().x - gaps) / float(pages);
        for (int i = 0; i < pages; i++) {
            ImGui::SetNextItemWidth(w);
            std::string tab = Themed(kPages[i]) + "###" + kPages[i];
            if (ImGui::BeginTabItem(tab.c_str(), nullptr, set && i == g_page ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (!set && i != g_page) {
                    g_page = i;
                    g_capture = -1;
                    g_pad_capture = -1;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

// the title in spaced capitals, a blood shadow under the gold, a flourish fading out to each side
void DrawTitle() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float em = ImGui::GetFontSize(), size = em * 1.35f;
    std::string title = Spaced(Themed("SETTINGS"));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, title.c_str());
    float x = p.x + std::max((avail - ts.x) * 0.5f, 0.0f);
    float shadow = std::max(1.5f, size / 14.0f);
    dl->AddText(ImGui::GetFont(), size, ImVec2(x + shadow, p.y + shadow), U32(kHighlight, 1.0f), title.c_str());
    dl->AddText(ImGui::GetFont(), size, ImVec2(x, p.y), U32(kHeading), title.c_str());
    float y = p.y + ts.y * 0.5f, gap = em * 1.1f;
    if (x - gap > p.x) {
        FadeLine(dl, p.x, x - gap, y, 1.0f, kHeading, true);
        Diamond(dl, ImVec2(x - gap + em * 0.35f, y + 0.5f), em * 0.2f, U32(kHeading));
    }
    float x_end = x + ts.x;
    if (x_end + gap < p.x + avail) {
        FadeLine(dl, x_end + gap, p.x + avail, y, 1.0f, kHeading, false);
        Diamond(dl, ImVec2(x_end + gap - em * 0.35f, y + 0.5f), em * 0.2f, U32(kHeading));
    }
    ImGui::Dummy(ImVec2(avail, ts.y + em * 0.1f));
    OrnateDivider();
}

constexpr const char* kCredit = "Port by Loic \"Han\" Orleans";

// the hint line, then the credit on the left and Back and Defaults on the right
void DrawFooter(float em) {
    OrnateDivider();
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    const char* hint = "";
    if (g_capture >= 0) hint = "Press a key or mouse button to bind, Escape cancels";
    else if (g_pad_capture >= 0) hint = "Press a button on the controller to bind, wait 5 seconds to cancel";
    else if (PadMode() && g_pad_focus == PadFocus::Tabs) hint = "D-pad: tabs    A: enter    R2: close";
    else if (PadMode() && g_edit_id) hint = "D-pad left / right: change    A or Y: done";
    else if (PadMode()) hint = "A: select    Y: back    R2: close";
    else if (g_page == kKeyboardPage && g_hovered_box >= 0) hint = "Right click or Delete clears the key";
    else if (g_page == kControllerPage && g_hovered_box >= 0) hint = "Right click or Delete restores the default";
    ImGui::TextUnformatted(Ui(hint, false).c_str());
    ImGui::PopStyleColor();
    float bw = em * 8;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kDim, "%s", kCredit);
    ImGui::SameLine(0, 0);
    float buttons_pad = ImGui::GetContentRegionAvail().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(buttons_pad, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    if (ImGui::Button(Themed("Back", false).c_str(), ImVec2(bw, 0))) ToggleMenu();
    ImGui::SameLine();
    if (ImGui::Button(Themed("Defaults", false).c_str(), ImVec2(bw, 0))) Defaults();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// The rows a finger drags up or down scroll with it, as a phone's lists do: the row it came down on is let go, so the
// drag presses nothing. A drag of the scrollbar itself stays the scrollbar's.
void DragScroll() {
    static bool dragging = false;
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragging = false;
        return;
    }
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!dragging) {
        bool touch = io.MouseSource == ImGuiMouseSource_TouchScreen;
        bool inside = window->Rect().Contains(io.MouseClickedPos[ImGuiMouseButton_Left]);
        bool scrollbar = ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y);
        float moved = std::fabs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y);
        dragging = touch && inside && !scrollbar && moved > ImGui::GetFontSize() * 0.6f;
        if (!dragging) return;
    }
    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
    ImGui::ClearActiveID();
}

// the controller's A and Y: from the tabs into the rows and back out
void PadNavigate() {
    if (!PadMode() || !CaptureFree()) return;
    if (g_pad_focus == PadFocus::Tabs) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
            g_pad_focus = PadFocus::Rows;
            g_enter_rows = true;
        }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)) {
        if (g_edit_id) g_edit_id = 0;
        else g_back = true;
    }
}

void DrawPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em * 50, em * 32), ImGuiCond_Always);
    for (const auto& [col, color] : kPanelColors) ImGui::PushStyleColor(col, color);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);  // the outer gold edge, DrawFrame adds the inner one
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 1.6f, em * 1.2f));
    // a scrollbar a thumb can take
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, em * 1.4f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, em * 2.0f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("##settings", nullptr, kFlags)) {
        DrawFrame();
        DrawTitle();
        PadNavigate();
        if (OnTabs()) ImGui::GetCurrentContext()->NavDisableHighlight = true;
        DrawTabs();
        float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + em;
        ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
        g_hovered_box = -1;
        if (g_enter_rows) {
            g_enter_rows = false;
            ImGui::GetCurrentContext()->NavDisableHighlight = false;
            ImGui::SetKeyboardFocusHere();
        }
        DrawRows(g_page);
        DragScroll();
        ImGui::EndChild();
        // a Y nothing in the rows used goes back up to the tabs
        if (g_back) {
            g_back = false;
            g_pad_focus = PadFocus::Tabs;
            g_edit_id = 0;
        }
        DrawFooter(em);
    }
    ImGui::End();
    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(int(std::size(kPanelColors)));
}

// the window's display's refresh rate, 0 when unreadable
uint32_t RefreshRate() {
    SDL_Window* w = static_cast<SDL_Window*>(Window());
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(w ? SDL_GetDisplayForWindow(w) : SDL_GetPrimaryDisplay());
    return m && m->refresh_rate > 1 ? uint32_t(std::lround(m->refresh_rate)) : 0;
}

// as app::Pace, at this cap
void PaceAt(uint32_t cap) {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace

void PaceFrame() {
    if (Get().fps_cap != kFpsCapRefresh) {
        Pace();
        return;
    }
    // a few under, so a variable refresh display stays in its range and vsync never waits a whole refresh
    constexpr uint32_t kRefreshMargin = 3;
    uint32_t rate = RefreshRate();
    PaceAt(rate > kRefreshMargin ? rate - kRefreshMargin : 0);
}

const rows::Row* rows::Page(int page, size_t* count) {
    if (page == kKeyboardPage) {
        *count = InputRows().size();
        return InputRows().data();
    }
    if (page == kControllerPage) {
        *count = PadRows().size();
        return PadRows().data();
    }
    if (page == kCheatsPage) {
        *count = std::size(kCheatRows);
        return kCheatRows;
    }
    if (page == kTouchPage) {
        *count = std::size(kTouchRows);
        return kTouchRows;
    }
    if (page == kGeneralPage) {
        *count = std::size(kGeneralRows);
        return kGeneralRows;
    }
    if (page == kGuidePage) {
        *count = std::size(kGuideRows);
        return kGuideRows;
    }
    if (page == kUnlocksPage) {
        *count = std::size(kUnlockRows);
        return kUnlockRows;
    }
    if (page == kCreditsPage) {
        *count = std::size(kCreditRows);
        return kCreditRows;
    }
    *count = std::size(kGraphicsRows);
    return kGraphicsRows;
}

bool MenuOpen() { return g_open.load(std::memory_order_relaxed); }

bool Capturing() { return g_capture >= 0 || g_pad_capture >= 0; }

void ToggleMenu() {
    bool open = !MenuOpen();
    g_open.store(open, std::memory_order_relaxed);
    g_capture = -1;
    g_pad_capture = -1;
    g_pad_focus = PadFocus::Tabs;
    g_edit_id = 0;
    g_enter_rows = false;
    g_back = false;
    if (open) input::ReleaseCursor();
}

// "CPU 40% 65°C", the parts the device tells
void AppendPerf(std::string& line, const char* name, float load, float temp) {
    if (load < 0 && temp < 0) return;
    char part[48];
    int n = std::snprintf(part, sizeof(part), "%s%s", line.empty() ? "" : "   ", name);
    if (load >= 0) n += std::snprintf(part + n, sizeof(part) - n, " %.0f%%", load);
    if (temp >= 0) std::snprintf(part + n, sizeof(part) - n, " %.0f\xC2\xB0" "C", temp);
    line += part;
}

// a line of text with a drop shadow, its left edge at x
void DrawShadowed(ImDrawList* dl, float size, ImVec2 at, ImU32 color, const char* text) {
    float shadow = std::max(2.0f, size / 12.0f);
    dl->AddText(ImGui::GetFont(), size, ImVec2(at.x + shadow, at.y + shadow), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(ImGui::GetFont(), size, at, color, text);
}

// the frame rate, and under it the CPU's and GPU's load and temperatures and the battery's, over the game at the
// place Options::fps_position picks
void DrawFpsCounter(double fps) {
    const game::Options& o = game::Opt();
    if (!o.fps_counter || MenuOpen()) return;
    host::SamplePerf();
    char text[96];
    // frame skip leaves frames out of the screen but not out of the game: both rates show while they differ
    // the ms are the game's own frame time (1000 / game frames a second), what the game speed follows: 16.7 is full speed
    const float game_fps = game::g_game_fps.load();
    if (game_fps > 0 && game_fps > float(fps) * 1.15f && game_fps < 240.0f)
        std::snprintf(text, sizeof(text), "%.0f FPS (GAME %.0f)  %.1f ms", fps, double(game_fps), 1000.0 / double(game_fps));
    else if (fps > 0.5)
        std::snprintf(text, sizeof(text), "%.0f FPS  %.1f ms", fps, 1000.0 / fps);
    else
        std::snprintf(text, sizeof(text), "%.0f FPS", fps);
    std::string perf;
    AppendPerf(perf, "CPU", o.fps_usage ? host::CpuLoad() : -1, o.fps_temps ? host::CpuTemp() : -1);
    AppendPerf(perf, "GPU", o.fps_usage ? host::GpuLoad() : -1, o.fps_temps ? host::GpuTemp() : -1);
    // devices that tell no GPU load (Mali drivers without timestamps): how long the game waits for the GPU instead,
    // near 0% the CPU is the limit, high the GPU is
    if (o.fps_usage && host::GpuLoad() < 0 && game::g_gpu_wait.load() >= 0) {
        char wait[32];
        std::snprintf(wait, sizeof(wait), "%sGPU WAIT %.0f%%", perf.empty() ? "" : "   ", double(game::g_gpu_wait.load()));
        perf += wait;
    }
    if (o.fps_temps) AppendPerf(perf, "BAT", -1, host::BatteryTemp());

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float big = ImGui::GetFontSize() * 1.5f, small = ImGui::GetFontSize();
    ImVec2 fs = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0, text);
    ImVec2 ps = perf.empty() ? ImVec2(0, 0) : ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0, perf.c_str());
    // the first line: the frame rate
    float line_w = fs.x;
    float block = fs.y + ps.y;
    float margin = screen.y * 0.03f;
    uint32_t pos = o.fps_position;
    bool bottom = pos == kFpsBottomLeft || pos == kFpsBottom || pos == kFpsBottomRight;
    // top centre sits under the settings gear, bottom centre over Back and Start, which the touch controls keep there
    float y = pos == kFpsTop ? screen.y * 0.14f
              : pos == kFpsBottom ? screen.y * 0.86f - block
              : bottom ? screen.y - margin - block
                       : margin;
    // left, centred or right aligned, each line on its own; the top corners clear the touch controls' White and
    // Black, the bottom ones have the edge free
    float side = bottom ? margin : screen.x * 0.13f;
    auto x = [&](float w) {
        if (pos == kFpsTopLeft || pos == kFpsBottomLeft) return side;
        if (pos == kFpsTopRight || pos == kFpsBottomRight) return screen.x - side - w;
        return screen.x / 2 - w / 2;
    };
    float line_x = x(line_w);
    DrawShadowed(dl, big, ImVec2(line_x, y), IM_COL32(255, 214, 90, 255), text);
    if (!perf.empty()) DrawShadowed(dl, small, ImVec2(x(ps.x), y + fs.y), IM_COL32(235, 235, 235, 255), perf.c_str());
}

void DrawUi(double fps) {
    game::g_fps = float(fps);
    DrawFpsCounter(fps);
    ImGuiIO& io = ImGui::GetIO();
    if (MenuOpen()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // the controller's navigation waits while the D-pad is on the tabs: Left / Right step them, A goes in
    if (MenuOpen() && OnTabs()) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.KeyRepeatRate = 0.1f;  // held arrows step ten times a second, ImGui's twenty is too fast for settings
    // scale the font with the output height: a phone's dense screen reads 13 px text at 540 rows to the height
    io.FontGlobalScale = std::max(1.0f, io.DisplaySize.y / 540.0f);
    if (MenuOpen()) {
        PollCapture();
        PollPadCapture();
        DrawPanel();
    }
}

}  // namespace app
