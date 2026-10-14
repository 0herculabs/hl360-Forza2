// IOSys::CBufferedStream guard.
//
// CBufferedStream (vtable 0x820367CC, ctor 0x8242BB88) wraps an inner stream
// held at +0x0C and starts out with its "valid" byte at +0x30 set to 1. The
// constructor never checks the inner stream, and the forwarding methods only
// test +0x30 (e.g. 0x8242A3C8: `if (valid) return inner->vtbl[0x2C]()`), so a
// buffered stream built over a stream that failed to open dereferences null
// the first time it is used. Seen loading a race: the streaming worker thread
// (0x82544210 -> 0x82543D50) dies on such a stream.
//
// The wrapper below runs the original constructor and, when the inner stream
// is null, clears +0x30 - the state the class itself uses for a failed stream
// (0x8242B508, 0x8242BB5C) - and logs who built it, so the cause can be found.
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/types.h>

extern "C" void __imp__sub_8242BB88(PPCContext& __restrict ctx, uint8_t* base);

namespace {

constexpr uint32_t kInnerStream = 0x0C;
constexpr uint32_t kValid = 0x30;

}  // namespace

extern "C" void sub_8242BB88(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  __imp__sub_8242BB88(ctx, base);
  const uint32_t inner = *reinterpret_cast<rex::be<uint32_t>*>(base + self + kInnerStream);
  if (!inner) {
    *(base + self + kValid) = 0;
    REXLOG_WARN("CBufferedStream {:08X} built over a null stream (caller {:08X}); marked invalid",
                self, caller);
  }
}
