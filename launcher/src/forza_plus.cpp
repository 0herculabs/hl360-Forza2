#include "forza_plus.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>

#include "i18n.h"
#include "miniz.h"

namespace launcher {
namespace fs = std::filesystem;

namespace {

constexpr char kModDir[] = ".forza-plus-2";

const char* ModeFolder(PfpMode mode) {
  switch (mode) {
    case PfpMode::kHard: return "Hard Mode";
    case PfpMode::kSandbox: return "Sandbox Mode";
    case PfpMode::kVanilla: return "Vanilla Mode";
    default: return "Normal Mode";
  }
}

fs::path ModeDb(const fs::path& game_dir, PfpMode mode, bool harder_ai) {
  fs::path db = game_dir / kModDir / "modes" / ModeFolder(mode);
  if (harder_ai) db /= "Harder AI";
  return db / "gamedb.slt";
}

std::string Lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A release archive, read with miniz through a FILE* so non-ASCII Windows
// paths open too.
class Zip {
 public:
  explicit Zip(const fs::path& path) {
#if defined(_WIN32)
    if (_wfopen_s(&file_, path.c_str(), L"rb") != 0) file_ = nullptr;
#else
    file_ = std::fopen(path.c_str(), "rb");
#endif
    ok_ = file_ && mz_zip_reader_init_cfile(&zip_, file_, 0, 0);
  }
  ~Zip() {
    if (ok_) mz_zip_reader_end(&zip_);
    if (file_) std::fclose(file_);
  }
  Zip(const Zip&) = delete;
  Zip& operator=(const Zip&) = delete;

  bool ok() const { return ok_; }
  mz_uint count() { return mz_zip_reader_get_num_files(&zip_); }
  bool Stat(mz_uint index, mz_zip_archive_file_stat& stat) { return mz_zip_reader_file_stat(&zip_, index, &stat); }

  // Streams one entry to `out`, counting into progress.done; stops on cancel.
  bool Extract(mz_uint index, const fs::path& out, TaskProgress& progress) {
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    struct Sink {
      std::ofstream* file;
      TaskProgress* progress;
    } sink{&file, &progress};
    const auto write = [](void* opaque, mz_uint64, const void* data, size_t size) -> size_t {
      auto* s = static_cast<Sink*>(opaque);
      s->file->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
      s->progress->done += size;
      return s->progress->cancel || !*s->file ? 0 : size;
    };
    return mz_zip_reader_extract_to_callback(&zip_, index, write, &sink, 0) && file.good();
  }

 private:
  mz_zip_archive zip_{};
  FILE* file_ = nullptr;
  bool ok_ = false;
};

struct Copy {
  Zip* zip;
  mz_uint index;
  std::string dest;  // '/'-separated, relative to the game folder
  uint64_t size;
};

// Adds every file under `<root><from>` to the plan as `<to><rest>`. Keyed
// case-insensitively, so the update archive overrides the base one.
void Plan(Zip& zip, const std::string& root, const std::string& from, const std::string& to,
          std::map<std::string, Copy>& plan) {
  const std::string prefix = root + from;
  mz_zip_archive_file_stat stat;
  for (mz_uint i = 0; i < zip.count(); ++i) {
    if (!zip.Stat(i, stat) || stat.m_is_directory) continue;
    const std::string name = stat.m_filename;
    if (name.rfind(prefix, 0) != 0) continue;
    const std::string dest = to + name.substr(prefix.size());
    plan[Lower(dest)] = {&zip, i, dest, stat.m_uncomp_size};
  }
}

// The folder that holds `folder` inside the archive ("Project Forza Plus 2/"
// for "Main Game Changes/"); nullopt when the archive has no such folder.
std::optional<std::string> FindRoot(Zip& zip, const std::string& folder) {
  mz_zip_archive_file_stat stat;
  for (mz_uint i = 0; i < zip.count(); ++i) {
    if (!zip.Stat(i, stat)) continue;
    const std::string name = stat.m_filename;
    const auto pos = name.find(folder);
    if (pos != std::string::npos && (pos == 0 || name[pos - 1] == '/')) return name.substr(0, pos);
  }
  return std::nullopt;
}

bool WriteState(const fs::path& game_dir, const PfpState& state) {
  std::ofstream out(game_dir / kModDir / "state.txt", std::ios::trunc);
  out << "mode " << int(state.mode) << "\nharder_ai " << int(state.harder_ai) << "\nupdated " << int(state.updated)
      << "\n";
  for (const auto& p : state.replaced) out << "replaced " << p << "\n";
  for (const auto& p : state.added) out << "added " << p << "\n";
  return bool(out);
}

// Removes `dir` and its parents while they are empty, up to `stop`.
void RemoveEmptyDirs(fs::path dir, const fs::path& stop) {
  std::error_code ec;
  while (dir != stop && dir.has_parent_path() && fs::is_empty(dir, ec) && !ec) {
    fs::remove(dir, ec);
    dir = dir.parent_path();
  }
}

// Puts the original files back and deletes the added ones and the mod folder.
bool Restore(const fs::path& game_dir, const PfpState& state, TaskProgress& progress) {
  std::error_code ec;
  bool ok = true;
  for (const auto& p : state.replaced) {
    fs::copy_file(game_dir / kModDir / "backup" / fs::u8path(p), game_dir / fs::u8path(p),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
      progress.SetStatus(std::string(L("Could not restore ", "No se pudo restaurar ")) + p);
      ok = false;
    }
  }
  for (const auto& p : state.added) {
    const fs::path file = game_dir / fs::u8path(p);
    fs::remove(file, ec);
    RemoveEmptyDirs(file.parent_path(), game_dir / "Media");
  }
  if (ok) fs::remove_all(game_dir / kModDir, ec);
  return ok;
}

}  // namespace

PfpState LoadForzaPlus(const fs::path& game_dir) {
  PfpState state;
  std::ifstream in(game_dir / kModDir / "state.txt");
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto space = line.find(' ');
    if (space == std::string::npos) continue;
    const std::string key = line.substr(0, space), value = line.substr(space + 1);
    state.installed = true;
    if (key == "mode") state.mode = PfpMode(std::clamp(std::atoi(value.c_str()), 0, 3));
    if (key == "harder_ai") state.harder_ai = value == "1";
    if (key == "updated") state.updated = value == "1";
    if (key == "replaced") state.replaced.push_back(value);
    if (key == "added") state.added.push_back(value);
  }
  return state;
}

bool InstallForzaPlus(const fs::path& game_dir, const fs::path& base_zip, const fs::path& update_zip,
                      PfpMode mode, bool harder_ai, TaskProgress& progress) {
  if (ReadGameRegion(game_dir / "default.xex") != GameRegion::kUsa) {
    progress.SetStatus(L("Project Forza Plus 2 needs the American release of the game (import its ISO first).",
                         "Project Forza Plus 2 necesita la versión americana del juego (importa antes su ISO)."));
    return false;
  }
  if (LoadForzaPlus(game_dir).installed && !UninstallForzaPlus(game_dir, progress)) return false;

  Zip base(base_zip);
  std::unique_ptr<Zip> update;
  if (!update_zip.empty()) update = std::make_unique<Zip>(update_zip);
  if (!base.ok() || (update && !update->ok())) {
    progress.SetStatus(L("Could not open the archive.", "No se pudo abrir el archivo comprimido."));
    return false;
  }
  const auto root = FindRoot(base, "Main Game Changes/");
  if (!root) {
    progress.SetStatus(L("This is not the Project Forza Plus 2 archive (no \"Main Game Changes\" folder).",
                         "No es el archivo de Project Forza Plus 2 (falta la carpeta \"Main Game Changes\")."));
    return false;
  }
  std::map<std::string, Copy> plan;
  Plan(base, *root, "Main Game Changes/default.xex", "default.xex", plan);
  Plan(base, *root, "Main Game Changes/Media/", "Media/", plan);
  Plan(base, *root, "DLC Cars and Tracks/Media/", "Media/", plan);
  Plan(base, *root, "Game Mode DBs/", std::string(kModDir) + "/modes/", plan);
  if (update) {
    const auto update_root = FindRoot(*update, "Update Files/");
    if (!update_root) {
      progress.SetStatus(L("This is not the Project Forza Plus 2 update archive (no \"Update Files\" folder).",
                           "No es la actualización de Project Forza Plus 2 (falta la carpeta \"Update Files\")."));
      return false;
    }
    Plan(*update, *update_root, "Update Files/Game Mode DBs/", std::string(kModDir) + "/modes/", plan);
    Plan(*update, *update_root, "Update Files/physics/", "Media/physics/", plan);
  }

  uint64_t total = 0;
  for (const auto& [key, copy] : plan) total += copy.size;
  progress.total = total;
  progress.done = 0;

  std::error_code ec;
  fs::remove_all(game_dir / kModDir, ec);
  fs::create_directories(game_dir / kModDir, ec);
  PfpState state;
  state.installed = true;
  state.updated = update != nullptr;
  state.mode = mode;
  state.harder_ai = harder_ai;
  const auto fail = [&](const std::string& message) {
    Restore(game_dir, state, progress);
    progress.SetStatus(message);
    return false;
  };
  for (const auto& [key, copy] : plan) {
    if (progress.cancel) return fail(L("Installation cancelled; the game was left as it was.",
                                       "Instalación cancelada; el juego se ha dejado como estaba."));
    const fs::path out = game_dir / fs::u8path(copy.dest);
    fs::create_directories(out.parent_path(), ec);
    if (copy.dest.rfind(kModDir, 0) != 0) {
      if (fs::exists(out, ec)) {
        const fs::path backup = game_dir / kModDir / "backup" / fs::u8path(copy.dest);
        fs::create_directories(backup.parent_path(), ec);
        fs::copy_file(out, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail(std::string(L("Could not back up ", "No se pudo guardar una copia de ")) + copy.dest);
        state.replaced.push_back(copy.dest);
      } else {
        state.added.push_back(copy.dest);
      }
    }
    progress.SetStatus(std::string(L("Installing ", "Instalando ")) + copy.dest);
    if (!copy.zip->Extract(copy.index, out, progress)) {
      return fail(progress.cancel ? L("Installation cancelled; the game was left as it was.",
                                      "Instalación cancelada; el juego se ha dejado como estaba.")
                                  : std::string(L("Could not write ", "No se pudo escribir ")) + copy.dest);
    }
  }
  if (!WriteState(game_dir, state)) return fail(L("Could not save the mod state.", "No se pudo guardar el estado del mod."));
  const std::string error = SetForzaPlusMode(game_dir, mode, harder_ai);
  if (!error.empty()) return fail(error);
  progress.SetStatus(std::string(L("Project Forza Plus 2 installed: ", "Project Forza Plus 2 instalado: ")) +
                     std::to_string(state.added.size()) + L(" files added, ", " archivos añadidos, ") +
                     std::to_string(state.replaced.size()) + L(" replaced (originals kept).", " sustituidos (originales guardados)."));
  return true;
}

bool UpdateForzaPlus(const fs::path& game_dir, const fs::path& update_zip, TaskProgress& progress) {
  PfpState state = LoadForzaPlus(game_dir);
  if (!state.installed) {
    progress.SetStatus(L("Install Project Forza Plus 2 first.", "Instala antes Project Forza Plus 2."));
    return false;
  }
  Zip update(update_zip);
  if (!update.ok()) {
    progress.SetStatus(L("Could not open the archive.", "No se pudo abrir el archivo comprimido."));
    return false;
  }
  const auto root = FindRoot(update, "Update Files/");
  if (!root) {
    progress.SetStatus(L("This is not the Project Forza Plus 2 update archive (no \"Update Files\" folder).",
                         "No es la actualización de Project Forza Plus 2 (falta la carpeta \"Update Files\")."));
    return false;
  }
  std::map<std::string, Copy> plan;
  Plan(update, *root, "Update Files/Game Mode DBs/", std::string(kModDir) + "/modes/", plan);
  Plan(update, *root, "Update Files/physics/", "Media/physics/", plan);
  uint64_t total = 0;
  for (const auto& [key, copy] : plan) total += copy.size;
  progress.total = total;
  progress.done = 0;

  // Everything is extracted next to its target first and only moved into
  // place once the whole archive came out, so a failure leaves no half update.
  std::error_code ec;
  std::vector<std::pair<fs::path, fs::path>> staged;  // temporary, final
  const auto discard = [&](const std::string& message) {
    for (const auto& [tmp, out] : staged) fs::remove(tmp, ec);
    progress.SetStatus(message);
    return false;
  };
  for (const auto& [key, copy] : plan) {
    if (progress.cancel) return discard(L("Update cancelled; nothing was changed.", "Actualización cancelada; no se ha cambiado nada."));
    const fs::path out = game_dir / fs::u8path(copy.dest);
    const fs::path tmp = fs::path(out).concat(".pfp-new");
    fs::create_directories(out.parent_path(), ec);
    progress.SetStatus(std::string(L("Updating ", "Actualizando ")) + copy.dest);
    staged.emplace_back(tmp, out);
    if (!copy.zip->Extract(copy.index, tmp, progress)) {
      return discard(progress.cancel ? L("Update cancelled; nothing was changed.", "Actualización cancelada; no se ha cambiado nada.")
                                     : std::string(L("Could not write ", "No se pudo escribir ")) + copy.dest);
    }
  }
  for (const auto& [key, copy] : plan) {
    if (copy.dest.rfind(kModDir, 0) == 0) continue;
    const bool known = std::find(state.replaced.begin(), state.replaced.end(), copy.dest) != state.replaced.end() ||
                       std::find(state.added.begin(), state.added.end(), copy.dest) != state.added.end();
    if (known) continue;
    const fs::path out = game_dir / fs::u8path(copy.dest);
    if (fs::exists(out, ec)) {
      const fs::path backup = game_dir / kModDir / "backup" / fs::u8path(copy.dest);
      fs::create_directories(backup.parent_path(), ec);
      fs::copy_file(out, backup, fs::copy_options::overwrite_existing, ec);
      if (ec) return discard(std::string(L("Could not back up ", "No se pudo guardar una copia de ")) + copy.dest);
      state.replaced.push_back(copy.dest);
    } else {
      state.added.push_back(copy.dest);
    }
  }
  for (const auto& [tmp, out] : staged) {
    fs::rename(tmp, out, ec);
    if (ec) {
      progress.SetStatus(std::string(L("Could not replace ", "No se pudo sustituir ")) + out.u8string());
      return false;
    }
  }
  state.updated = true;
  if (!WriteState(game_dir, state)) {
    progress.SetStatus(L("Could not save the mod state.", "No se pudo guardar el estado del mod."));
    return false;
  }
  const std::string error = SetForzaPlusMode(game_dir, state.mode, state.harder_ai);
  if (!error.empty()) {
    progress.SetStatus(error);
    return false;
  }
  progress.SetStatus(L("Project Forza Plus 2 update applied.", "Actualización de Project Forza Plus 2 aplicada."));
  return true;
}

bool UninstallForzaPlus(const fs::path& game_dir, TaskProgress& progress) {
  const PfpState state = LoadForzaPlus(game_dir);
  if (!state.installed) return true;
  progress.SetStatus(L("Removing Project Forza Plus 2...", "Quitando Project Forza Plus 2..."));
  if (!Restore(game_dir, state, progress)) return false;
  progress.SetStatus(L("Project Forza Plus 2 removed; the original files are back.",
                       "Project Forza Plus 2 quitado; se han restaurado los archivos originales."));
  return true;
}

std::string SetForzaPlusMode(const fs::path& game_dir, PfpMode mode, bool harder_ai) {
  PfpState state = LoadForzaPlus(game_dir);
  if (!state.installed) return L("Project Forza Plus 2 is not installed.", "Project Forza Plus 2 no está instalado.");
  std::error_code ec;
  fs::copy_file(ModeDb(game_dir, mode, harder_ai), game_dir / "Media" / "DB" / "gamedb.slt",
                fs::copy_options::overwrite_existing, ec);
  if (ec) return L("That mode's database is missing from the mod.", "Falta la base de datos de ese modo en el mod.");
  state.mode = mode;
  state.harder_ai = harder_ai;
  if (!WriteState(game_dir, state)) return L("Could not save the mod state.", "No se pudo guardar el estado del mod.");
  return {};
}

namespace {

constexpr char kCameraFile[] = "Media/Camera/CameraSettings.ini";
// Section of each PfpCamera in CameraSettings.ini.
constexpr const char* kCameraSections[kCameraCount] = {"FollowLowCam", "FollowHighCam", "HoodCam", "BumperHighCam"};

// "<Section>\FOV\FOV<n>" -> camera and band, or false.
bool FovKey(const std::string& key, int& camera, int& band) {
  for (int c = 0; c < kCameraCount; ++c) {
    const std::string prefix = std::string(kCameraSections[c]) + "\\FOV\\FOV";
    if (key.size() == prefix.size() + 1 && key.rfind(prefix, 0) == 0 && key.back() >= '0' && key.back() <= '2') {
      camera = c;
      band = key.back() - '0';
      return true;
    }
  }
  return false;
}

// The file is plain "<key> <value>" lines (CRLF); `edit` may rewrite a line.
template <typename Edit>
bool ForEachLine(const fs::path& file, Edit edit, std::string* rewritten) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    end = end == std::string::npos ? text.size() : end + 1;
    std::string line = text.substr(start, end - start);
    std::string eol;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
      eol.insert(eol.begin(), line.back());
      line.pop_back();
    }
    edit(line);
    if (rewritten) *rewritten += line + eol;
    start = end;
  }
  return true;
}

}  // namespace

bool ReadForzaPlusFov(const fs::path& game_dir, bool original, PfpFov& out) {
  const fs::path backup = game_dir / kModDir / "backup" / fs::u8path(kCameraFile);
  std::error_code ec;
  const fs::path file = original && fs::exists(backup, ec) ? backup : game_dir / fs::u8path(kCameraFile);
  return ForEachLine(
      file,
      [&](const std::string& line) {
        const auto space = line.find(' ');
        int camera, band;
        if (space != std::string::npos && FovKey(line.substr(0, space), camera, band)) {
          out.degrees[camera][band] = std::atoi(line.c_str() + space + 1);
        }
      },
      nullptr);
}

std::string SetForzaPlusFov(const fs::path& game_dir, const PfpFov& fov) {
  PfpState state = LoadForzaPlus(game_dir);
  if (!state.installed) return L("Project Forza Plus 2 is not installed.", "Project Forza Plus 2 no está instalado.");
  const fs::path file = game_dir / fs::u8path(kCameraFile);
  std::error_code ec;
  if (std::find(state.replaced.begin(), state.replaced.end(), kCameraFile) == state.replaced.end()) {
    const fs::path backup = game_dir / kModDir / "backup" / fs::u8path(kCameraFile);
    fs::create_directories(backup.parent_path(), ec);
    fs::copy_file(file, backup, fs::copy_options::overwrite_existing, ec);
    if (ec) return L("Could not back up CameraSettings.ini.", "No se pudo guardar una copia de CameraSettings.ini.");
    state.replaced.push_back(kCameraFile);
    if (!WriteState(game_dir, state)) return L("Could not save the mod state.", "No se pudo guardar el estado del mod.");
  }
  std::string text;
  const bool read = ForEachLine(
      file,
      [&](std::string& line) {
        const auto space = line.find(' ');
        int camera, band;
        if (space != std::string::npos && FovKey(line.substr(0, space), camera, band)) {
          line = line.substr(0, space + 1) + std::to_string(fov.degrees[camera][band]);
        }
      },
      &text);
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!read || !(out << text)) return L("Could not write CameraSettings.ini.", "No se pudo escribir CameraSettings.ini.");
  return {};
}

std::string SetForzaPlusPhysicsRate(const fs::path& game_dir, int fps) {
  if (!LoadForzaPlus(game_dir).installed) return {};
  const fs::path file = game_dir / "Media" / "physics" / "PhysicsSettings.ini";
  const std::string value = std::to_string(int(fps * 5.79 + 0.5));
  bool changed = false;
  std::string text;
  const bool read = ForEachLine(
      file,
      [&](std::string& line) {
        if (line.rfind("CarPhysicsUpdateHz ", 0) == 0 && line != "CarPhysicsUpdateHz " + value) {
          line = "CarPhysicsUpdateHz " + value;
          changed = true;
        }
      },
      &text);
  if (!read) return L("PhysicsSettings.ini is missing.", "Falta PhysicsSettings.ini.");
  if (!changed) return {};
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!(out << text)) return L("Could not write PhysicsSettings.ini.", "No se pudo escribir PhysicsSettings.ini.");
  return {};
}

std::vector<ManifestEntry> WithoutModFiles(const std::vector<ManifestEntry>& manifest, const PfpState& state) {
  if (!state.installed) return manifest;
  std::set<std::string> replaced;
  for (const auto& p : state.replaced) replaced.insert(Lower(p));
  std::vector<ManifestEntry> out;
  for (const auto& entry : manifest) {
    if (!replaced.count(Lower(entry.path))) out.push_back(entry);
  }
  return out;
}

}  // namespace launcher
