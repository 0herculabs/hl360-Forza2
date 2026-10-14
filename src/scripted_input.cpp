// Scripted controller for automated sessions (--scripted_input=<file>).
//
// Tools like tools/pad.ps1 write the pad state to a small text file; this
// wrapper around XamInputGetState merges it into controller 0. Unlike posted
// keyboard messages it works with the window unfocused or off-screen, which is
// how the shader-cache recorder runs while the PC is in use. Off by default;
// the real controller and the keyboard keep working either way.
//
// File format, one line: buttons=<hex> lt=<0-255> rt=<0-255> lx=<int16> ly=<int16> rx=<int16> ry=<int16>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/types.h>

#if defined(_WIN32)
#include <windows.h>
#endif

REXCVAR_DEFINE_STRING(scripted_input, "", "Forza2",
                      "Path of a pad-state file merged into controller 0 (automation)");

namespace {

using GuestFunc = void (*)(PPCContext&, uint8_t*);

struct PadState {
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

PadState ReadPadFile(const std::string& path) {
  // Re-read at most every 8 ms; the game polls more often than that.
  static std::mutex mutex;
  static PadState cached;
  static auto last = std::chrono::steady_clock::time_point{};
  std::lock_guard lock(mutex);
  const auto now = std::chrono::steady_clock::now();
  if (now - last < std::chrono::milliseconds(8)) return cached;
  last = now;
  PadState s;
  if (FILE* f = std::fopen(path.c_str(), "rb")) {
    unsigned buttons = 0;
    int lt = 0, rt = 0, lx = 0, ly = 0, rx = 0, ry = 0;
    if (std::fscanf(f, "buttons=%x lt=%d rt=%d lx=%d ly=%d rx=%d ry=%d", &buttons, &lt, &rt, &lx, &ly, &rx,
                    &ry) >= 1) {
      s.buttons = uint16_t(buttons);
      s.lt = uint8_t(lt), s.rt = uint8_t(rt);
      s.lx = int16_t(lx), s.ly = int16_t(ly), s.rx = int16_t(rx), s.ry = int16_t(ry);
    }
    std::fclose(f);
  }
  cached = s;
  return s;
}

GuestFunc Original() {
  static GuestFunc original = [] {
#if defined(_WIN32)
    return reinterpret_cast<GuestFunc>(
        GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "__imp__XamInputGetState"));
#else
    return GuestFunc(nullptr);
#endif
  }();
  return original;
}

}  // namespace

// u32 XamInputGetState(u32 user_index, u32 flags, X_INPUT_STATE* state)
extern "C" void __imp__XamInputGetState(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t user_index = ctx.r3.u32;
  const uint32_t state_ptr = ctx.r5.u32;
  if (GuestFunc original = Original()) original(ctx, base);
  const std::string path = REXCVAR_GET(scripted_input);
  if (path.empty() || user_index != 0 || !state_ptr) return;

  const PadState s = ReadPadFile(path);
  // X_INPUT_STATE: be32 packet_number, then X_INPUT_GAMEPAD (big-endian).
  auto* packet = reinterpret_cast<rex::be<uint32_t>*>(base + state_ptr);
  auto* buttons = reinterpret_cast<rex::be<uint16_t>*>(base + state_ptr + 4);
  uint8_t* triggers = base + state_ptr + 6;
  auto* sticks = reinterpret_cast<rex::be<int16_t>*>(base + state_ptr + 8);
  if (ctx.r3.u32 != 0) {
    // No physical pad on slot 0: present a clean one.
    std::memset(base + state_ptr, 0, 16);
  }
  *buttons = uint16_t(uint16_t(*buttons) | s.buttons);
  triggers[0] = std::max(triggers[0], s.lt);
  triggers[1] = std::max(triggers[1], s.rt);
  const int16_t axes[4] = {s.lx, s.ly, s.rx, s.ry};
  for (int i = 0; i < 4; ++i) {
    if (axes[i]) sticks[i] = axes[i];
  }
  *packet = uint32_t(*packet) + 1;
  ctx.r3.u64 = 0;  // ERROR_SUCCESS
}
