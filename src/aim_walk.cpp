// RE4 OG Aim Walk + TPS movement/camera for RE4 UHD (Steam, 32-bit).
// Internal addresses and input action bits are specific to bio4.exe timestamp 0x5A6192E5.
#include <windows.h>
#include <commctrl.h>
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace {
constexpr uintptr_t kImageBase = 0x00400000;
constexpr uint32_t kExpectedTimestamp = 0x5A6192E5;
constexpr uintptr_t kDispatchVa = 0x007632E0;   // cdecl: player routine dispatcher
constexpr uintptr_t kLookAtVa = 0x0059E660;     // cdecl: build view matrix from eye and target
constexpr uintptr_t kCameraVa = 0x00C702D0;     // camera object, mode +0x34, view block +0x60
constexpr uintptr_t kPlayerPtrVa = 0x00C57054;  // cPlayer*
constexpr uintptr_t kKeyOnVa = 0x00C62F08;      // uint64 Key_btn_on (logical held actions)
constexpr uintptr_t kKeyTrgVa = 0x00C62F10;     // uint64 Key_btn_trg
constexpr uintptr_t kGlobalPtrVa = 0x00C06F3C;  // GLOBAL_WK*
constexpr uint64_t kUp = 0x1, kDown = 0x2, kRight = 0x4, kLeft = 0x8;
constexpr uint64_t kAim = 0x10;
constexpr uint64_t kKnife = 0x1000000000000ull;
constexpr uint64_t kDirections = kUp | kDown | kRight | kLeft;
constexpr int kSettingsToggleMessage = WM_APP + 71;
constexpr int kAimSpeedId = 101, kSensitivityId = 102, kPitchLimitId = 103;
constexpr int kCameraCheckId = 201, kTpsMoveCheckId = 202, kAimWalkCheckId = 203;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDefaultAimSpeed = 1800.0f;
constexpr float kAcceleration = 5000.0f;
constexpr float kBraking = 6000.0f;
constexpr float kTpsTurnRate = 540.0f;  // degrees per second

using DispatchFn = void(__cdecl*)(uint8_t* player);
using LookAtFn = void(__cdecl*)(float* viewBlock);
DispatchFn g_dispatchOriginal = nullptr;
LookAtFn g_lookAtOriginal = nullptr;
uintptr_t g_moduleBase = 0;
HMODULE g_self = nullptr;
std::atomic<HWND> g_settingsWindow{nullptr};
std::atomic<HWND> g_gameWindow{nullptr};
std::atomic<bool> g_menuOpen{false};
std::atomic<float> g_aimSpeed{kDefaultAimSpeed};
std::atomic<int> g_mouseSensitivity{5};
std::atomic<int> g_pitchLimitDegrees{45};
std::atomic<bool> g_cameraEnabled{true};
std::atomic<bool> g_tpsMovementEnabled{true};
std::atomic<bool> g_aimWalkEnabled{true};
std::atomic<bool> g_traceAimMouse{true};
std::atomic<float> g_viewYaw{0.0f};
std::atomic<bool> g_haveViewYaw{false};
std::atomic<float> g_lastOrbitYaw{0.0f};
std::atomic<bool> g_haveLastOrbitYaw{false};
volatile LONG g_mouseDx = 0, g_mouseDy = 0;
float g_cameraYaw = 0.0f, g_pitchOffset = 0.0f;
bool g_haveCameraYaw = false;
bool g_previousAimAction = false;
float g_velocityForward = 0.0f, g_velocitySide = 0.0f;
HHOOK g_mouseHook = nullptr;
LONG g_lastMouseX = 0, g_lastMouseY = 0;
bool g_haveMousePoint = false;
int g_cursorShowAdjustments = 0;

uintptr_t AtVa(uintptr_t va) { return g_moduleBase + (va - kImageBase); }
float WrapAngle(float a) {
  while (a > kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

void PluginPath(wchar_t* out, size_t count, const wchar_t* name) {
  if (!out || count == 0) return;
  out[0] = 0;
  if (!GetModuleFileNameW(g_self, out, static_cast<DWORD>(count))) return;
  wchar_t* slash = wcsrchr(out, L'\\');
  if (!slash) { out[0] = 0; return; }
  wcscpy_s(slash + 1, count - static_cast<size_t>(slash + 1 - out), name);
}

void LogLine(const char* message) {
  wchar_t path[MAX_PATH]{};
  PluginPath(path, MAX_PATH, L"RE4OGAimWalk.log");
  if (!path[0]) return;
  HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, message, static_cast<DWORD>(strlen(message)), &written, nullptr);
  WriteFile(file, "\r\n", 2, &written, nullptr);
  CloseHandle(file);
}

bool SupportedExe() {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == kExpectedTimestamp;
}

float FrameSeconds() {
  auto** global = reinterpret_cast<uint8_t**>(AtVa(kGlobalPtrVa));
  if (!global || !*global) return 1.0f / 30.0f;
  const float frames = *reinterpret_cast<float*>(*global + 0x70);
  if (!std::isfinite(frames) || frames <= 0.0f || frames > 4.0f) return 1.0f / 30.0f;
  return frames / 30.0f;
}

uint64_t HeldActions() {
  return *reinterpret_cast<volatile uint64_t*>(AtVa(kKeyOnVa));
}
uint64_t TriggerActions() {
  return *reinterpret_cast<volatile uint64_t*>(AtVa(kKeyTrgVa));
}

void LoadSettings() {
  wchar_t path[MAX_PATH]{};
  PluginPath(path, MAX_PATH, L"RE4OGAimWalk.ini");
  if (!path[0]) return;
  int speed = GetPrivateProfileIntW(L"Movement", L"AimSpeed", static_cast<int>(kDefaultAimSpeed), path);
  // 600 was the old default and was reported as far too slow; migrate that default.
  if (speed == 600) speed = static_cast<int>(kDefaultAimSpeed);
  const int sensitivity = GetPrivateProfileIntW(L"Camera", L"Sensitivity", 2, path);
  const int pitchLimit = GetPrivateProfileIntW(L"Camera", L"PitchLimit", 45, path);
  g_aimSpeed.store(static_cast<float>(std::clamp(speed, 300, 3000)));
  g_mouseSensitivity.store(std::clamp(sensitivity, 1, 20));
  g_pitchLimitDegrees.store(std::clamp(pitchLimit, 5, 45));
  g_cameraEnabled.store(GetPrivateProfileIntW(L"Camera", L"Enabled", 1, path) != 0);
  g_tpsMovementEnabled.store(GetPrivateProfileIntW(L"Movement", L"TpsMovement", 1, path) != 0);
  g_aimWalkEnabled.store(GetPrivateProfileIntW(L"Movement", L"AimWalk", 1, path) != 0);
}

void SaveSettings() {
  wchar_t path[MAX_PATH]{}, value[32]{};
  PluginPath(path, MAX_PATH, L"RE4OGAimWalk.ini");
  if (!path[0]) return;
  swprintf_s(value, L"%d", static_cast<int>(g_aimSpeed.load()));
  WritePrivateProfileStringW(L"Movement", L"AimSpeed", value, path);
  swprintf_s(value, L"%d", g_mouseSensitivity.load());
  WritePrivateProfileStringW(L"Camera", L"Sensitivity", value, path);
  swprintf_s(value, L"%d", g_pitchLimitDegrees.load());
  WritePrivateProfileStringW(L"Camera", L"PitchLimit", value, path);
  WritePrivateProfileStringW(L"Camera", L"Enabled", g_cameraEnabled.load() ? L"1" : L"0", path);
  WritePrivateProfileStringW(L"Movement", L"TpsMovement", g_tpsMovementEnabled.load() ? L"1" : L"0", path);
  WritePrivateProfileStringW(L"Movement", L"AimWalk", g_aimWalkEnabled.load() ? L"1" : L"0", path);
}

BOOL CALLBACK FindGameWindowProc(HWND window, LPARAM) {
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == GetCurrentProcessId() && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
    g_gameWindow.store(window);
    return FALSE;
  }
  return TRUE;
}
HWND FindGameWindow() {
  HWND existing = g_gameWindow.load();
  if (existing && IsWindow(existing)) return existing;
  g_gameWindow.store(nullptr);
  EnumWindows(FindGameWindowProc, 0);
  return g_gameWindow.load();
}
bool GameIsForeground() {
  HWND foreground = GetForegroundWindow();
  DWORD pid = 0;
  if (foreground) GetWindowThreadProcessId(foreground, &pid);
  return pid == GetCurrentProcessId();
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM message, LPARAM data) {
  if (code == HC_ACTION && message == WM_MOUSEMOVE && data) {
    const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
    if (g_menuOpen.load()) {
      g_haveMousePoint = false;
      InterlockedExchange(&g_mouseDx, 0);
      InterlockedExchange(&g_mouseDy, 0);
      return CallNextHookEx(g_mouseHook, code, message, data);
    }
    const bool orbit = g_cameraEnabled.load() && GameIsForeground() && ((HeldActions() & (kAim | kKnife)) == 0);
    if (!orbit) {
      g_haveMousePoint = false;
      return CallNextHookEx(g_mouseHook, code, message, data);
    }
    if ((mouse->flags & LLMHF_INJECTED) != 0) {
      g_haveMousePoint = false;
      return 1;  // the synthetic recenter event must not reach the game
    }
    HWND window = FindGameWindow();
    if (!window) {
      g_haveMousePoint = false;
      return CallNextHookEx(g_mouseHook, code, message, data);
    }
    POINT center{0, 0};
    RECT client{};
    if (!GetClientRect(window, &client)) return CallNextHookEx(g_mouseHook, code, message, data);
    center.x = (client.right - client.left) / 2;
    center.y = (client.bottom - client.top) / 2;
    ClientToScreen(window, &center);
    if (!g_haveMousePoint) {
      g_lastMouseX = center.x;
      g_lastMouseY = center.y;
      g_haveMousePoint = true;
    }
    const LONG dx = mouse->pt.x - g_lastMouseX;
    const LONG dy = mouse->pt.y - g_lastMouseY;
    if (std::abs(dx) < 300 && std::abs(dy) < 300) {
      auto playerSlot = reinterpret_cast<uint8_t* volatile*>(AtVa(kPlayerPtrVa));
      uint8_t* player = playerSlot ? *playerSlot : nullptr;
      const bool aiming = player && player[0xFC] == 0 && player[0xFD] == 6 &&
                          (HeldActions() & kAim) != 0;
      if (aiming && g_traceAimMouse.load()) {
        static LONG traceX = 0, traceY = 0;
        InterlockedExchangeAdd(&traceX, dx);
        InterlockedExchangeAdd(&traceY, dy);
        const LONG totalX = InterlockedExchange(&traceX, 0) + dx;
        const LONG totalY = InterlockedExchange(&traceY, 0) + dy;
        char line[112]{};
        std::snprintf(line, sizeof(line), "AIM mouse delta: dx=%ld dy=%ld", totalX, totalY);
        LogLine(line);
      } else {
        InterlockedExchangeAdd(&g_mouseDx, dx);
        InterlockedExchangeAdd(&g_mouseDy, dy);
      }
    }
    g_lastMouseX = center.x;
    g_lastMouseY = center.y;
    if (mouse->pt.x != center.x || mouse->pt.y != center.y) SetCursorPos(center.x, center.y);
    return 1;  // consume this relative move; the plugin owns FCM input
  }
  return CallNextHookEx(g_mouseHook, code, message, data);
}

void EnsureSettingsCursorVisible() {
  CURSORINFO info{};
  info.cbSize = sizeof(info);
  if (GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING)) return;
  for (int i = 0; i < 32; ++i) {
    ++g_cursorShowAdjustments;
    if (ShowCursor(TRUE) >= 0) break;
  }
  SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
}

void OpenSettingsWindow(HWND window) {
  g_menuOpen.store(true);
  g_haveMousePoint = false;
  InterlockedExchange(&g_mouseDx, 0);
  InterlockedExchange(&g_mouseDy, 0);
  ClipCursor(nullptr);
  EnsureSettingsCursorVisible();
  ShowWindow(window, SW_SHOWNORMAL);
  SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  SetForegroundWindow(window);
  SetFocus(window);
  SetTimer(window, 1, 100, nullptr);
}

void CloseSettingsWindow(HWND window) {
  g_menuOpen.store(false);
  KillTimer(window, 1);
  ClipCursor(nullptr);
  ShowWindow(window, SW_HIDE);
  for (int i = 0; i < g_cursorShowAdjustments; ++i) ShowCursor(FALSE);
  g_cursorShowAdjustments = 0;
  HWND game = FindGameWindow();
  if (game) SetForegroundWindow(game);
}

void SetTrackbarLabel(HWND window, int id, const wchar_t* format, int value) {
  wchar_t text[96]{};
  swprintf_s(text, format, value);
  SetWindowTextW(GetDlgItem(window, id + 1000), text);
}
void CreateSettingsControls(HWND window) {
  CreateWindowExW(0, L"STATIC", L"Aim / knife walk speed", WS_CHILD | WS_VISIBLE, 18, 14, 280, 20, window, nullptr, g_self, nullptr);
  HWND speed = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
                               18, 36, 330, 36, window, reinterpret_cast<HMENU>(kAimSpeedId), g_self, nullptr);
  SendMessageW(speed, TBM_SETRANGE, TRUE, MAKELONG(300, 3000));
  SendMessageW(speed, TBM_SETTICFREQ, 300, 0);
  SendMessageW(speed, TBM_SETPOS, TRUE, static_cast<LPARAM>(g_aimSpeed.load()));
  CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 350, 43, 70, 20, window, reinterpret_cast<HMENU>(kAimSpeedId + 1000), g_self, nullptr);
  SetTrackbarLabel(window, kAimSpeedId, L"%d", static_cast<int>(g_aimSpeed.load()));

  CreateWindowExW(0, L"STATIC", L"Mouse camera sensitivity", WS_CHILD | WS_VISIBLE, 18, 78, 280, 20, window, nullptr, g_self, nullptr);
  HWND sensitivity = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
                                     18, 100, 330, 36, window, reinterpret_cast<HMENU>(kSensitivityId), g_self, nullptr);
  SendMessageW(sensitivity, TBM_SETRANGE, TRUE, MAKELONG(1, 20));
  SendMessageW(sensitivity, TBM_SETTICFREQ, 1, 0);
  SendMessageW(sensitivity, TBM_SETPOS, TRUE, g_mouseSensitivity.load());
  CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 350, 107, 70, 20, window, reinterpret_cast<HMENU>(kSensitivityId + 1000), g_self, nullptr);
  SetTrackbarLabel(window, kSensitivityId, L"%d", g_mouseSensitivity.load());

  CreateWindowExW(0, L"STATIC", L"Vertical camera limit (up / down)", WS_CHILD | WS_VISIBLE, 18, 142, 300, 20, window, nullptr, g_self, nullptr);
  HWND pitchLimit = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
                                    18, 164, 330, 36, window, reinterpret_cast<HMENU>(kPitchLimitId), g_self, nullptr);
  SendMessageW(pitchLimit, TBM_SETRANGE, TRUE, MAKELONG(5, 45));
  SendMessageW(pitchLimit, TBM_SETTICFREQ, 5, 0);
  SendMessageW(pitchLimit, TBM_SETPOS, TRUE, g_pitchLimitDegrees.load());
  CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 350, 171, 70, 20, window, reinterpret_cast<HMENU>(kPitchLimitId + 1000), g_self, nullptr);
  SetTrackbarLabel(window, kPitchLimitId, L"%d deg", g_pitchLimitDegrees.load());

  HWND camera = CreateWindowExW(0, L"BUTTON", L"TPS mouse camera", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                18, 207, 300, 24, window, reinterpret_cast<HMENU>(kCameraCheckId), g_self, nullptr);
  HWND tps = CreateWindowExW(0, L"BUTTON", L"Camera-relative TPS movement", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             18, 235, 340, 24, window, reinterpret_cast<HMENU>(kTpsMoveCheckId), g_self, nullptr);
  HWND aim = CreateWindowExW(0, L"BUTTON", L"Walk while aiming / holding knife", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             18, 263, 340, 24, window, reinterpret_cast<HMENU>(kAimWalkCheckId), g_self, nullptr);
  SendMessageW(camera, BM_SETCHECK, g_cameraEnabled.load() ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(tps, BM_SETCHECK, g_tpsMovementEnabled.load() ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(aim, BM_SETCHECK, g_aimWalkEnabled.load() ? BST_CHECKED : BST_UNCHECKED, 0);
  CreateWindowExW(0, L"STATIC", L"F1 or Esc: close  |  Cursor unlocked; gameplay input paused", WS_CHILD | WS_VISIBLE, 18, 297, 410, 20, window, nullptr, g_self, nullptr);
}

LRESULT CALLBACK SettingsWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      CreateSettingsControls(window);
      return 0;
    case kSettingsToggleMessage:
      if (IsWindowVisible(window)) CloseSettingsWindow(window);
      else OpenSettingsWindow(window);
      return 0;
    case WM_TIMER:
      if (g_menuOpen.load()) { ClipCursor(nullptr); EnsureSettingsCursorVisible(); }
      return 0;
    case WM_SETCURSOR:
      if (g_menuOpen.load()) { SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))); return TRUE; }
      break;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE && g_menuOpen.load()) { CloseSettingsWindow(window); return 0; }
      break;
    case WM_HSCROLL: {
      HWND slider = reinterpret_cast<HWND>(lParam);
      const int id = GetDlgCtrlID(slider);
      if (id == kAimSpeedId) g_aimSpeed.store(static_cast<float>(SendMessageW(slider, TBM_GETPOS, 0, 0)));
      if (id == kSensitivityId) g_mouseSensitivity.store(static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0)));
      if (id == kPitchLimitId) g_pitchLimitDegrees.store(static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0)));
      if (id == kAimSpeedId) SetTrackbarLabel(window, id, L"%d", static_cast<int>(g_aimSpeed.load()));
      if (id == kSensitivityId) SetTrackbarLabel(window, id, L"%d", g_mouseSensitivity.load());
      if (id == kPitchLimitId) SetTrackbarLabel(window, id, L"%d deg", g_pitchLimitDegrees.load());
      SaveSettings();
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      if (id == kCameraCheckId) g_cameraEnabled.store(IsDlgButtonChecked(window, id) == BST_CHECKED);
      else if (id == kTpsMoveCheckId) g_tpsMovementEnabled.store(IsDlgButtonChecked(window, id) == BST_CHECKED);
      else if (id == kAimWalkCheckId) g_aimWalkEnabled.store(IsDlgButtonChecked(window, id) == BST_CHECKED);
      else break;
      SaveSettings();
      return 0;
    }
    case WM_CLOSE:
      CloseSettingsWindow(window);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI SettingsThread(void*) {
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
  InitCommonControlsEx(&controls);
  WNDCLASSEXW cls{};
  cls.cbSize = sizeof(cls);
  cls.lpfnWndProc = SettingsWndProc;
  cls.hInstance = g_self;
  cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  cls.lpszClassName = L"RE4OGAimWalkSettings";
  RegisterClassExW(&cls);
  HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, cls.lpszClassName, L"RE4 OG Movement & Camera",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 480, 360,
                                nullptr, nullptr, g_self, nullptr);
  if (!window) { LogLine("Could not create settings window."); return 0; }
  g_settingsWindow.store(window);
  ShowWindow(window, SW_HIDE);
  g_gameWindow.store(FindGameWindow());
  g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_self, 0);
  if (!g_mouseHook) LogLine("Could not install mouse hook for TPS camera.");
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
  return 0;
}

void ToggleSettingsOnF1(bool isLeon) {
  static bool previous = false;
  if (!isLeon) return;
  const bool down = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
  if (down && !previous) {
    HWND window = g_settingsWindow.load();
    if (window) PostMessageW(window, kSettingsToggleMessage, 0, 0);
  }
  previous = down;
}

float Approach(float value, float target, float amount) {
  if (value < target) return (value + amount > target) ? target : value + amount;
  if (value > target) return (value - amount < target) ? target : value - amount;
  return target;
}

void ApplyAimWalk(uint8_t* player, float dt) {
  // The knife binding is a game action, not a physical key: use its logical
  // input bit so remapped controls work. Keep movement active in the weapon
  // routine after AIM. The knife has a distinct aimed state (r1 == 11),
  // confirmed from the in-game trace; it does not use the AIM held bit.
  const bool aimRoutine = player && player[0xFD] == 6;
  const bool knifeAimed = player && player[0xFD] == 11;
  if (!player || player[0xFC] != 0 || !g_aimWalkEnabled.load() || (!aimRoutine && !knifeAimed)) {
    g_velocityForward = g_velocitySide = 0.0f;
    return;
  }
  // Read movement independently of the game's direction bits: mouse aiming can
  // repurpose those bits for its own aim input and make walking stutter.
  const bool w = (GetAsyncKeyState('W') & 0x8000) != 0;
  const bool a = (GetAsyncKeyState('A') & 0x8000) != 0;
  const bool sKey = (GetAsyncKeyState('S') & 0x8000) != 0;
  const bool d = (GetAsyncKeyState('D') & 0x8000) != 0;
  float forward = (w ? 1.0f : 0.0f) - (sKey ? 1.0f : 0.0f);
  float side = (d ? 1.0f : 0.0f) - (a ? 1.0f : 0.0f);
  const float inputLength = std::sqrt(forward * forward + side * side);
  if (inputLength > 1.0f) { forward /= inputLength; side /= inputLength; }
  const float maxSpeed = g_aimSpeed.load();
  const float delta = (inputLength > 0.0f ? kAcceleration : kBraking) * dt;
  g_velocityForward = Approach(g_velocityForward, forward * maxSpeed, delta);
  g_velocitySide = Approach(g_velocitySide, side * maxSpeed, delta);
  const float velocityLength = std::sqrt(g_velocityForward * g_velocityForward + g_velocitySide * g_velocitySide);
  if (velocityLength > maxSpeed) { g_velocityForward *= maxSpeed / velocityLength; g_velocitySide *= maxSpeed / velocityLength; }

  const float facing = *reinterpret_cast<float*>(player + 0xA4);
  float* position = reinterpret_cast<float*>(player + 0x94);
  if (!std::isfinite(facing) || !std::isfinite(position[0]) || !std::isfinite(position[2])) { g_velocityForward = g_velocitySide = 0.0f; return; }
  const float s = std::sin(facing), c = std::cos(facing);
  position[0] += s * g_velocityForward * dt - c * g_velocitySide * dt;
  position[2] += c * g_velocityForward * dt + s * g_velocitySide * dt;
}

void ApplyTpsInput(uint8_t* player, uint64_t held, bool isLeon, float dt) {
  if (!isLeon || !g_tpsMovementEnabled.load() || player[0xFC] != 0 || player[0xFD] > 5 || (held & (kAim | kKnife)) != 0) {
    g_dispatchOriginal(player);
    return;
  }
  float forward = ((held & kUp) ? 1.0f : 0.0f) - ((held & kDown) ? 1.0f : 0.0f);
  float side = ((held & kRight) ? 1.0f : 0.0f) - ((held & kLeft) ? 1.0f : 0.0f);
  const float mag = std::sqrt(forward * forward + side * side);
  if (mag < 0.5f) { g_dispatchOriginal(player); return; }
  if (mag > 1.0f) { forward /= mag; side /= mag; }
  float cameraYaw = g_viewYaw.load();
  if (!g_haveViewYaw.load() || !std::isfinite(cameraYaw)) cameraYaw = *reinterpret_cast<float*>(player + 0xA4);
  const float desiredYaw = WrapAngle(cameraYaw - std::atan2(side, forward));
  float& facing = *reinterpret_cast<float*>(player + 0xA4);
  const float difference = WrapAngle(desiredYaw - facing);
  const float maxTurn = kTpsTurnRate * (kPi / 180.0f) * dt;
  facing = std::fabs(difference) <= maxTurn ? desiredYaw : WrapAngle(facing + std::copysign(maxTurn, difference));

  auto& keyOn = *reinterpret_cast<volatile uint64_t*>(AtVa(kKeyOnVa));
  auto& keyTrg = *reinterpret_cast<volatile uint64_t*>(AtVa(kKeyTrgVa));
  const uint64_t realOn = keyOn, realTrg = keyTrg;
  keyOn = (realOn & ~kDirections) | kUp;
  keyTrg = realTrg & ~kDirections;
  g_dispatchOriginal(player);  // native walk/run animation and root motion carry Leon forward
  keyOn = realOn;
  keyTrg = realTrg;
}

void __cdecl DispatchHook(uint8_t* player) {
  auto mainPlayerPtr = reinterpret_cast<uint8_t* volatile*>(AtVa(kPlayerPtrVa));
  const bool isLeon = player && mainPlayerPtr && player == *mainPlayerPtr;
  const uint64_t held = isLeon ? HeldActions() : 0;
  const uint64_t triggered = isLeon ? TriggerActions() : 0;
  if (isLeon) {
    static uint8_t previousR1 = 0xFF, previousR2 = 0xFF;
    const uint8_t r1 = player[0xFD], r2 = player[0xFC];
    if (r1 != previousR1 || r2 != previousR2) {
      char line[160]{};
      std::snprintf(line, sizeof(line), "Player state: r1=%u r2=%u held=%016llX trg=%016llX",
                    static_cast<unsigned>(r1), static_cast<unsigned>(r2),
                    static_cast<unsigned long long>(held),
                    static_cast<unsigned long long>(triggered));
      LogLine(line);
    }
    previousR1 = r1;
    previousR2 = r2;
  }
  ToggleSettingsOnF1(isLeon);
  if (isLeon && g_menuOpen.load()) {
    g_velocityForward = g_velocitySide = 0.0f;
    return;  // keep game movement/actions from consuming input while the settings UI owns it
  }
  // RE4's r1 state 5 is the dedicated 180-degree quick-turn routine. Cancel
  // that state itself so Q, Shift+S, or any remapped binding is handled equally.
  if (isLeon && player[0xFC] == 0 && player[0xFD] == 5) {
    player[0xFD] = 0;
    player[0xFE] = 0;
  }
  if (isLeon) {
    const bool aimAction = (held & (kAim | kKnife)) != 0;
    if (aimAction && !g_previousAimAction) {
      // On aim entry, point Leon where the free camera was already looking.
      const float cameraYaw = (g_cameraEnabled.load() && g_haveLastOrbitYaw.load())
                                  ? g_lastOrbitYaw.load()
                                  : g_viewYaw.load();
      if (std::isfinite(cameraYaw)) {
        *reinterpret_cast<float*>(player + 0xA4) = cameraYaw;
        g_viewYaw.store(cameraYaw);
        g_haveViewYaw.store(true);
      }
    }
    g_previousAimAction = aimAction;
  }
  const float dt = FrameSeconds();
  ApplyTpsInput(player, held, isLeon, dt);
  if (isLeon) ApplyAimWalk(player, dt);
  if (isLeon && player[0xFC] == 0 && player[0xFD] == 5) {
    player[0xFD] = 0;
    player[0xFE] = 0;
  }
}

void RotateAroundPlayer(float* point, const float* pivot, float yaw) {
  const float x = point[0] - pivot[0], z = point[2] - pivot[2];
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  point[0] = pivot[0] + x * cy + z * sy;
  point[2] = pivot[2] + z * cy - x * sy;
}

void PitchAroundLeon(float* point, const float* pivot, float pitch, float yaw) {
  // Pitch around the camera's local right axis, not world X; world-X rotation
  // makes the vertical arc turn into a roll/circle as yaw changes.
  const float axisX = std::cos(yaw), axisZ = -std::sin(yaw);
  const float x = point[0] - pivot[0], y = point[1] - pivot[1], z = point[2] - pivot[2];
  const float c = std::cos(pitch), s = std::sin(pitch);
  const float dot = axisX * x + axisZ * z;
  const float crossX = -axisZ * y, crossY = axisZ * x - axisX * z, crossZ = axisX * y;
  point[0] = pivot[0] + x * c + crossX * s + axisX * dot * (1.0f - c);
  point[1] = pivot[1] + y * c + crossY * s;
  point[2] = pivot[2] + z * c + crossZ * s + axisZ * dot * (1.0f - c);
}

void __cdecl LookAtHook(float* block) {
  if (!block || block != reinterpret_cast<float*>(AtVa(kCameraVa) + 0x60)) { g_lookAtOriginal(block); return; }
  auto playerSlot = reinterpret_cast<uint8_t* volatile*>(AtVa(kPlayerPtrVa));
  uint8_t* player = playerSlot ? *playerSlot : nullptr;
  float* eye = block + 0x29;
  float* target = block + 0x2C;
  const float* pos = player ? reinterpret_cast<float*>(player + 0x94) : nullptr;
  const uint8_t cameraMode = *reinterpret_cast<volatile uint8_t*>(AtVa(kCameraVa) + 0x34);
  // Keep the game's native camera for aim/knife and every non-follow camera mode.
  const bool actionHeld = (HeldActions() & (kAim | kKnife)) != 0;
  if (!player || !pos || player[0xFC] != 0 || player[0xFD] > 6 || cameraMode != 10 || actionHeld) {
    g_haveCameraYaw = false;
    InterlockedExchange(&g_mouseDx, 0);
    InterlockedExchange(&g_mouseDy, 0);
    g_viewYaw.store(player ? *reinterpret_cast<float*>(player + 0xA4) : 0.0f);
    g_haveViewYaw.store(player != nullptr);
    g_lookAtOriginal(block);
    return;
  }

  const float baseYaw = std::atan2(target[0] - eye[0], target[2] - eye[2]);
  if (!std::isfinite(baseYaw)) { g_lookAtOriginal(block); return; }
  if (g_cameraEnabled.load()) {
    if (!g_haveCameraYaw) { g_cameraYaw = baseYaw; g_haveCameraYaw = true; }
    const LONG dx = std::clamp(InterlockedExchange(&g_mouseDx, 0), -80L, 80L);
    const LONG dy = std::clamp(InterlockedExchange(&g_mouseDy, 0), -80L, 80L);
    const float sensitivity = static_cast<float>(g_mouseSensitivity.load());
    g_cameraYaw = WrapAngle(g_cameraYaw - static_cast<float>(dx) * sensitivity * 0.0004f);
    const float kPitchLimit = static_cast<float>(g_pitchLimitDegrees.load()) * (kPi / 180.0f);
    g_pitchOffset = std::clamp(g_pitchOffset + static_cast<float>(dy) * sensitivity * 0.0004f, -kPitchLimit, kPitchLimit);
    const float yawDelta = WrapAngle(g_cameraYaw - baseYaw);
    // Orbit both points around Leon. Use the native target height for the pitch
    // pivot so vertical movement is centered on his torso, not the floor.
    RotateAroundPlayer(eye, pos, yawDelta);
    RotateAroundPlayer(target, pos, yawDelta);
    const float pitchPivot[3] = {pos[0], target[1], pos[2]};
    PitchAroundLeon(eye, pitchPivot, g_pitchOffset, g_cameraYaw);
    PitchAroundLeon(target, pitchPivot, g_pitchOffset, g_cameraYaw);
    g_lastOrbitYaw.store(g_cameraYaw);
    g_haveLastOrbitYaw.store(true);
    g_viewYaw.store(g_cameraYaw);
    g_haveViewYaw.store(true);
  } else {
    g_cameraYaw = baseYaw;
    g_pitchOffset = 0.0f;
    g_haveCameraYaw = false;
    g_viewYaw.store(baseYaw);
    g_haveViewYaw.store(true);
  }
  g_lookAtOriginal(block);
}
void Install() {
  g_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!SupportedExe()) { LogLine("Refused to load: bio4.exe timestamp does not match supported Steam UHD build."); return; }
  LoadSettings();
  if (MH_Initialize() != MH_OK) { LogLine("MinHook initialization failed."); return; }
  void* dispatch = reinterpret_cast<void*>(AtVa(kDispatchVa));
  if (MH_CreateHook(dispatch, reinterpret_cast<void*>(&DispatchHook), reinterpret_cast<void**>(&g_dispatchOriginal)) != MH_OK ||
      MH_EnableHook(dispatch) != MH_OK) {
    LogLine("Could not install player movement hook."); MH_Uninitialize(); return;
  }
  void* lookAt = reinterpret_cast<void*>(AtVa(kLookAtVa));
  if (MH_CreateHook(lookAt, reinterpret_cast<void*>(&LookAtHook), reinterpret_cast<void**>(&g_lookAtOriginal)) != MH_OK ||
      MH_EnableHook(lookAt) != MH_OK) {
    LogLine("Mouse camera hook failed; movement is still enabled.");
  }
  CreateThread(nullptr, 0, SettingsThread, nullptr, 0, nullptr);
  LogLine("Hooks installed. F1 opens settings; AIM/KNIFE use the game's logical bindings.");
}
} // namespace

extern "C" __declspec(dllexport) void InitializeASI() { Install(); }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { g_self = instance; DisableThreadLibraryCalls(instance); }
  return TRUE;
}






