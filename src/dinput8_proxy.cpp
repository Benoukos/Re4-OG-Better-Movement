#include <windows.h>

#include <cwchar>
#include <string>

namespace {
HINSTANCE g_self = nullptr;

std::wstring SiblingPath(const wchar_t* leaf) {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(g_self, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return {};
  slash[1] = L'\0';
  std::wstring result(path);
  result += leaf;
  return result;
}

void Log(const wchar_t* message) {
  const std::wstring path = SiblingPath(L"re4bettermovement-loader.log");
  if (path.empty()) return;
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, message, static_cast<DWORD>(wcslen(message) * sizeof(wchar_t)), &written, nullptr);
  WriteFile(file, L"\r\n", 4, &written, nullptr);
  CloseHandle(file);
}

DWORD WINAPI InitializeMovementThread(void*) {
  const std::wstring coopPath = SiblingPath(L"RE4CoopDinput8.dll");
  if (coopPath.empty() || !LoadLibraryW(coopPath.c_str())) {
    Log(L"ERROR: Could not load RE4CoopDinput8.dll.");
    return 0;
  }
  Log(L"Loaded RE4CoopDinput8.dll.");

  const std::wstring movementPath = SiblingPath(L"re4bettermovement.dll");
  HMODULE movement = movementPath.empty() ? nullptr : LoadLibraryW(movementPath.c_str());
  if (!movement) {
    Log(L"ERROR: Could not load re4bettermovement.dll.");
    return 0;
  }
  using InitializePlugin = void(__cdecl*)();
  auto initialize = reinterpret_cast<InitializePlugin>(GetProcAddress(movement, "InitializeASI"));
  if (!initialize) {
    Log(L"ERROR: re4bettermovement.dll does not export InitializeASI.");
    return 0;
  }
  initialize();
  Log(L"Called re4bettermovement.dll InitializeASI.");
  return 0;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = instance;
    DisableThreadLibraryCalls(instance);
    if (!CreateThread(nullptr, 0, InitializeMovementThread, nullptr, 0, nullptr))
      OutputDebugStringW(L"RE4 Better Movement: failed to start initialization thread.\n");
  }
  return TRUE;
}
