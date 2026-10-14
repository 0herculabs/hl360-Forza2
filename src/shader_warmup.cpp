// Shader warm-up mode (--shader_warmup=true), driven by Forza2Launcher.
//
// At startup the runtime rebuilds every shader and pipeline recorded in the
// shader storage (Documents/forza2/cache/shaders/shareable) before the title
// runs - InitializeShaderStorage, blocking, right before OnPostLaunchModule.
// In warm-up mode the app does exactly that with its window hidden and then
// quits, so the expensive first-time pipeline compilation (and the driver's
// own disk cache) happens in the launcher, behind a progress bar, instead of
// as hitches in the first race.
#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(shader_warmup, false, "Forza2",
                    "Build the shader/pipeline cache, then quit (used by the launcher)");

namespace forza2 {

bool ShaderWarmupMode() { return REXCVAR_GET(shader_warmup); }

// Hides every top-level window this process owns.
void HideOwnWindows() {
#if defined(_WIN32)
  EnumWindows(
      [](HWND hwnd, LPARAM) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
        return TRUE;
      },
      0);
#endif
}

}  // namespace forza2
