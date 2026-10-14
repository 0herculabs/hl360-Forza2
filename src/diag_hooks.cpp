// Mid-asm diagnostics (declared in forza2_manifest.toml, [[entrypoint.midasm_hook]]).
//
// The asset loader at 0x8253F780 opens an inner stream through the title's
// file system (fs->vtbl[0x5C](&out, path, ...)) and then wraps it in a
// CBufferedStream without checking the result. These hooks run right after
// each open and log the path when it came back null, which is what later
// crashes the loader (see stream_guard.cpp).
#include <cstring>
#include <string>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

namespace {

uint32_t Load32(uint8_t* base, uint32_t addr) {
  return *reinterpret_cast<rex::be<uint32_t>*>(base + addr);
}

// MSVC 2005 std::basic_string on Xenon (32-bit, big-endian): allocator at +0,
// inline buffer / heap pointer at +4, size at +0x14, capacity at +0x18.
// Handles both char and wchar_t strings.
std::string ReadGuestString(uint8_t* base, uint32_t str) {
  const uint32_t size = Load32(base, str + 0x14);
  const uint32_t capacity = Load32(base, str + 0x18);
  if (size == 0 || size > 1024) return "<size " + std::to_string(size) + ">";
  const bool wide_guess = capacity < 8;  // wstring keeps 8 chars inline
  const uint32_t inline_chars = wide_guess ? 8 : 16;
  const uint32_t data = capacity < inline_chars ? str + 4 : Load32(base, str + 4);
  const uint8_t* p = base + data;
  // Wide strings: every high byte zero for ASCII paths.
  if (p[0] == 0 && p[1] != 0) {
    std::string out;
    for (uint32_t i = 0; i < size; ++i) out += char(p[i * 2 + 1]);
    return out;
  }
  return std::string(reinterpret_cast<const char*>(p), size);
}

void LogIfNull(PPCRegister& r1, uint32_t result_offset, uint32_t path_offset, const char* site) {
  uint8_t* base = REX_KERNEL_MEMORY()->virtual_membase();
  const uint32_t sp = r1.u32;
  if (Load32(base, sp + result_offset) == 0) {
    REXLOG_WARN("asset open failed at {}: \"{}\"", site, ReadGuestString(base, sp + path_offset));
  }
}

}  // namespace

// 0x8253F864: after fs->Open into [r1+0x5C], path std::string at r1+0x70.
void Forza2AssetOpen1(PPCRegister& r1) { LogIfNull(r1, 0x5C, 0x70, "8253F864"); }

// 0x8253F93C: after fs->Open into [r1+0x60], path std::string at r1+0x90.
void Forza2AssetOpen2(PPCRegister& r1) { LogIfNull(r1, 0x60, 0x90, "8253F93C"); }
