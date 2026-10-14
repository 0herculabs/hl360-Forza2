// Local player profile.
//
// The SDK signs in one fixed profile named "User" (xam/user_profile.cpp) with no
// way to rename it. Forza 2 shows that name on the front end, writes it into
// saves and lobby data, so the name comes from the `gamertag` cvar instead.
//
// The three XAM exports that hand the name to the title are defined here. The
// generated code references __imp__XamUser* by symbol; a definition in the exe
// wins over the import from rexruntime.dll, so the SDK versions are never used.
//
// Each gamertag also gets its own user-data folder (see ProfileUserDataRoot),
// which is where XContent saves land: one folder per profile = separate
// careers, garages and settings.
#include <algorithm>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/string.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

#include "profile.h"

REXCVAR_DEFINE_STRING(gamertag, "Player", "Profile",
                      "Name of the signed-in profile (1-15 letters, digits or spaces). Each name "
                      "keeps its own saves.");

namespace forza2 {

// Xbox 360 gamertags: up to 15 characters, letters, digits and single spaces,
// starting with a letter. Anything else is dropped so the name is also a safe
// folder name.
std::string Gamertag() {
  std::string out;
  for (char c : std::string(REXCVAR_GET(gamertag))) {
    const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum || (c == ' ' && !out.empty() && out.back() != ' ')) {
      out += c;
    }
    if (out.size() == 15) {
      break;
    }
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out.empty() ? std::string("Player") : out;
}

std::filesystem::path ProfileUserDataRoot(const std::filesystem::path& base) {
  return base / "profiles" / Gamertag();
}

}  // namespace forza2

namespace {

using rex::X_HRESULT;
using rex::X_RESULT;

// Same layout as the SDK's private X_USER_SIGNIN_INFO (xam_user.cpp).
struct X_USER_SIGNIN_INFO {
  rex::be<uint64_t> xuid;
  rex::be<uint32_t> unk08;
  rex::be<uint32_t> signin_state;
  rex::be<uint32_t> unk10;
  rex::be<uint32_t> unk14;
  char name[16];
};
static_assert(sizeof(X_USER_SIGNIN_INFO) == 40);

u32 XamUserGetName_entry(u32 user_index, mapped_string buffer, u32 buffer_len) {
  if (user_index >= 4) {
    return X_E_INVALIDARG;
  }
  if (user_index) {
    return X_E_NO_SUCH_USER;
  }
  rex::string::copy_truncating(buffer, forza2::Gamertag(), std::min(buffer_len, uint32_t(16)));
  return X_E_SUCCESS;
}

u32 XamUserGetGamerTag_entry(u32 user_index, mapped_wstring buffer, u32 buffer_len) {
  if (user_index >= 4 || !buffer || buffer_len < 16) {
    return X_E_INVALIDARG;
  }
  if (user_index) {
    return X_E_NO_SUCH_USER;
  }
  rex::string::copy_and_swap_truncating(buffer, rex::string::to_utf16(forza2::Gamertag()),
                                        std::min(buffer_len, uint32_t(16)));
  return X_E_SUCCESS;
}

i32 XamUserGetSigninInfo_entry(u32 user_index, u32 flags, ppc_ptr_t<X_USER_SIGNIN_INFO> info) {
  (void)flags;
  if (!info) {
    return X_E_INVALIDARG;
  }
  std::memset(info, 0, sizeof(X_USER_SIGNIN_INFO));
  if (user_index) {
    return X_E_NO_SUCH_USER;
  }
  const auto* profile = REX_KERNEL_STATE()->user_profile();
  info->xuid = profile->xuid();
  info->signin_state = profile->signin_state();
  rex::string::copy_truncating(info->name, forza2::Gamertag(), rex::countof(info->name));
  return X_E_SUCCESS;
}

}  // namespace

REX_HOOK(__imp__XamUserGetName, XamUserGetName_entry)
REX_HOOK(__imp__XamUserGetGamerTag, XamUserGetGamerTag_entry)
REX_HOOK(__imp__XamUserGetSigninInfo, XamUserGetSigninInfo_entry)
