// Premium downloadable content from the Limited Edition bonus disc.
//
// Forza2Launcher copies the disc's content packages (STFS, title 4D5307EA,
// type 00000002) to <game>/dlc. The runtime keeps installed content under the
// user-data root, which is per profile here, so at startup every package the
// signed-in profile does not have yet is installed with the SDK's
// ContentManager::InstallContent - before the title enumerates its content.
// The disc's title update (title_update.bin) is never installed: it patches
// default.xex, and the recompiled code is the unpatched build.
#include <filesystem>
#include <system_error>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "profile.h"

namespace forza2 {

void InstallBundledDlc(const std::filesystem::path& game_root,
                       const std::filesystem::path& user_root) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path dlc_dir = game_root / "dlc";
  if (!fs::is_directory(dlc_dir, ec)) return;
  // Project Forza Plus 2 (installed by the launcher) ships the DLC cars and
  // tracks in Media/ and in its own database; the packs would add them twice.
  if (fs::exists(game_root / ".forza-plus-2" / "state.txt", ec)) {
    REXLOG_INFO("forza2: Project Forza Plus 2 carries the DLC; bonus disc packages not installed");
    return;
  }
  // InstallContent writes <package file name>.header here once the package is
  // fully extracted; a failed install leaves a partial folder and no header.
  // Extraction fails on paths over MAX_PATH (deep track files, ~100 chars
  // below the package folder), so the user-data root must stay short.
  const fs::path headers = user_root / "0000000000000000" / "4D5307EA" / "Headers" / "00000002";
  auto* content = REX_KERNEL_STATE()->content_manager();
  for (const auto& entry : fs::directory_iterator(dlc_dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    if (fs::exists(headers / (entry.path().filename().string() + ".header"), ec)) continue;
    const auto result = content->InstallContent(entry.path());
    if (result == 0) {
      REXLOG_INFO("forza2: installed DLC package {}", entry.path().filename().string());
    } else {
      REXLOG_WARN("forza2: DLC package {} failed to install ({:08X})", entry.path().filename().string(),
                  result);
    }
  }
}

}  // namespace forza2
