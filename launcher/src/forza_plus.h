#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "installer.h"

namespace launcher {

// Project Forza Plus 2: a data mod made for the American release, installed
// from its release archives ("Project Forza Plus 2 v1.3.zip" and the optional
// "Project Forza Plus 2 Update v1.3.zip"). It replaces a few files under
// Media/ and adds its own DLC cars and tracks there; the four career modes are
// alternative Media/DB/gamedb.slt files. Its default.xex is installed too: the
// game checks gamedb.slt, PhysicsSettings.ini and GameTunableSettings.ini
// against a signature table in the executable's 0D163575 resource and stops
// with a disc read error when they differ, and the mod's executable carries an
// edited table. Its code and data are the American release's, so the port's
// recompiled program runs it unchanged.
//
// Everything the launcher needs to undo it lives in <game>/.forza-plus-2:
//   state.txt  mode, harder AI, and every replaced/added path
//   backup/    the original files it replaced
//   modes/     each mode's gamedb.slt, to switch modes without the archives
enum class PfpMode { kNormal, kHard, kSandbox, kVanilla };

struct PfpState {
  bool installed = false;
  bool updated = false;  // the update archive was applied
  PfpMode mode = PfpMode::kNormal;
  bool harder_ai = false;
  std::vector<std::string> replaced;  // game files it overwrote (originals in backup/)
  std::vector<std::string> added;     // files it created
};

PfpState LoadForzaPlus(const std::filesystem::path& game_dir);

// Installs (or reinstalls) the mod. `update_zip` may be empty. Undoes its own
// changes if it fails or is cancelled.
bool InstallForzaPlus(const std::filesystem::path& game_dir, const std::filesystem::path& base_zip,
                      const std::filesystem::path& update_zip, PfpMode mode, bool harder_ai,
                      TaskProgress& progress);

// Applies the update archive over an installed mod: new career-mode
// databases and PhysicsSettings.ini. Nothing changes if it fails.
bool UpdateForzaPlus(const std::filesystem::path& game_dir, const std::filesystem::path& update_zip,
                     TaskProgress& progress);

// Restores the original files and removes everything the mod added.
bool UninstallForzaPlus(const std::filesystem::path& game_dir, TaskProgress& progress);

// Puts the chosen mode's gamedb.slt in place. Returns an error message, or empty.
std::string SetForzaPlusMode(const std::filesystem::path& game_dir, PfpMode mode, bool harder_ai);

// Field of view of the four driving cameras in Media/Camera/CameraSettings.ini,
// in degrees per speed band: [0] below 100 mph, [1] 100-150 mph, [2] above.
// The game checks that file's signature too; the mod's executable drops it
// from the table, so it can only be edited while the mod is installed.
enum PfpCamera { kChaseNear, kChaseFar, kHood, kBumper, kCameraCount };
struct PfpFov {
  int degrees[kCameraCount][3] = {};
};

// Reads the current values, or the game's own ones (`original`, from the
// backup once the file was changed). False if the file is missing.
bool ReadForzaPlusFov(const std::filesystem::path& game_dir, bool original, PfpFov& out);

// Writes the values; the first change backs the original file up so that
// uninstalling restores it. Returns an error message, or empty.
std::string SetForzaPlusFov(const std::filesystem::path& game_dir, const PfpFov& fov);

// Matches the mod's physics rate to the frame rate, as its author asks for
// frame rates other than 60 (otherwise the AI misjudges corners):
// CarPhysicsUpdateHz = round(fps * 5.79) in Media/physics/PhysicsSettings.ini
// (695 at 120 - the value of the mod's own 120 fps file).
// Returns an error message, or empty.
std::string SetForzaPlusPhysicsRate(const std::filesystem::path& game_dir, int fps);

// The manifest minus the files the mod replaced, for verification.
std::vector<ManifestEntry> WithoutModFiles(const std::vector<ManifestEntry>& manifest, const PfpState& state);

}  // namespace launcher
