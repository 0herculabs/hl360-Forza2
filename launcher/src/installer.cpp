#include "installer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include "i18n.h"
#include "sha256.h"

namespace launcher {
namespace fs = std::filesystem;

namespace {

constexpr uint64_t kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// Game-partition offsets: XGD2, XGD1, XGD3, plain XISO.
constexpr uint64_t kPartitions[] = {0xFD90000, 0x18300000, 0x2080000, 0};

struct IsoFile {
  std::string path;
  uint64_t offset = 0;
  uint64_t size = 0;
};

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24); }

bool ReadAt(std::ifstream& in, uint64_t offset, void* out, size_t size) {
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
  return in.gcount() == static_cast<std::streamsize>(size);
}

// XDVDFS directories are binary trees of entries; walk one directory table.
bool WalkDirectory(std::ifstream& in, uint64_t partition, uint32_t sector, uint32_t size,
                   const std::string& prefix, std::vector<IsoFile>& out, int depth) {
  if (depth > 32 || size == 0 || size > (64u << 20)) {
    return depth <= 32 && size == 0;
  }
  std::vector<uint8_t> table(size);
  if (!ReadAt(in, partition + uint64_t(sector) * kSector, table.data(), size)) {
    return false;
  }
  std::vector<uint32_t> stack{0};
  std::vector<bool> seen(size / 4 + 1, false);
  while (!stack.empty()) {
    const uint32_t off = stack.back();
    stack.pop_back();
    if (off + 14 > size || seen[off / 4]) {
      continue;
    }
    seen[off / 4] = true;
    const uint8_t* e = table.data() + off;
    const uint16_t left = Le16(e), right = Le16(e + 2);
    if (left == 0xFFFF) {
      continue;  // padding
    }
    const uint32_t start = Le32(e + 4), length = Le32(e + 8);
    const uint8_t attributes = e[12], name_length = e[13];
    if (off + 14 + name_length > size) {
      continue;
    }
    const std::string name(reinterpret_cast<const char*>(e + 14), name_length);
    const std::string path = prefix + name;
    if (attributes & 0x10) {
      if (!WalkDirectory(in, partition, start, length, path + "/", out, depth + 1)) {
        return false;
      }
    } else {
      out.push_back({path, partition + uint64_t(start) * kSector, length});
    }
    if (left) stack.push_back(uint32_t(left) * 4);
    if (right) stack.push_back(uint32_t(right) * 4);
  }
  return true;
}

std::string FormatGb(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / 1e9);
  return text;
}

}  // namespace

std::vector<ManifestEntry> LoadManifest(const fs::path& file) {
  std::vector<ManifestEntry> out;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream row(line);
    ManifestEntry entry;
    row >> entry.sha256 >> entry.size;
    std::getline(row >> std::ws, entry.path);
    if (!entry.path.empty() && entry.path.back() == '\r') {
      entry.path.pop_back();
    }
    if (entry.sha256.size() != 64 || entry.path.empty()) {
      continue;
    }
    const auto same = std::find_if(out.begin(), out.end(), [&](const ManifestEntry& e) {
      return e.path == entry.path && e.size == entry.size;
    });
    if (same != out.end()) {
      same->alternatives.push_back(std::move(entry.sha256));
    } else {
      out.push_back(std::move(entry));
    }
  }
  return out;
}

GameRegion ReadGameRegion(const fs::path& xex) {
  std::ifstream in(xex, std::ios::binary);
  uint8_t header[24];
  if (!ReadAt(in, 0, header, sizeof(header)) || std::memcmp(header, "XEX2", 4) != 0) {
    return GameRegion::kUnknown;
  }
  const auto be32 = [](const uint8_t* p) { return uint32_t(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); };
  for (uint32_t i = 0, count = std::min(be32(header + 20), 64u); i < count; ++i) {
    uint8_t field[8];
    if (!ReadAt(in, 24 + uint64_t(i) * 8, field, sizeof(field))) break;
    if (be32(field) != 0x00040006) continue;  // execution info: media ID first
    uint8_t media[4];
    if (!ReadAt(in, be32(field + 4), media, sizeof(media))) break;
    switch (be32(media)) {
      case 0x3E3180AE: return GameRegion::kEurope;
      case 0x0DF2D3F6: return GameRegion::kUsa;
      default: return GameRegion::kUnknown;
    }
  }
  return GameRegion::kUnknown;
}

size_t CountDlcPackages(const fs::path& game_dir) {
  std::error_code ec;
  size_t count = 0;
  for (const auto& entry : fs::directory_iterator(game_dir / "dlc", ec)) {
    if (entry.is_regular_file(ec)) ++count;
  }
  return count;
}

DiscKind ExtractIso(const fs::path& iso, const fs::path& dest, TaskProgress& progress) {
  std::ifstream in(iso, std::ios::binary);
  if (!in) {
    progress.SetStatus(std::string(L("Cannot open the image: ", "No se puede abrir la imagen: ")) + iso.u8string());
    return DiscKind::kFailed;
  }
  uint64_t partition = UINT64_MAX;
  char magic[20];
  for (uint64_t candidate : kPartitions) {
    if (ReadAt(in, candidate + 32 * kSector, magic, sizeof(magic)) &&
        std::memcmp(magic, kMagic, sizeof(magic)) == 0) {
      partition = candidate;
      break;
    }
  }
  if (partition == UINT64_MAX) {
    progress.SetStatus(L("Not a recognisable Xbox 360 disc image (XDVDFS).", "No es una imagen de disco de Xbox 360 (XDVDFS) reconocible."));
    return DiscKind::kFailed;
  }
  uint8_t root[8];
  ReadAt(in, partition + 32 * kSector + 20, root, sizeof(root));
  std::vector<IsoFile> files;
  progress.SetStatus(L("Reading the disc index...", "Leyendo el índice del disco..."));
  if (!WalkDirectory(in, partition, Le32(root), Le32(root + 4), "", files, 0)) {
    progress.SetStatus(L("The disc index is damaged or the image is incomplete.", "El índice del disco está dañado o la imagen está incompleta."));
    return DiscKind::kFailed;
  }
  files.erase(std::remove_if(files.begin(), files.end(),
                             [](const IsoFile& f) { return f.path.rfind("$SystemUpdate/", 0) == 0; }),
              files.end());
  const bool bonus = std::any_of(files.begin(), files.end(),
                                 [](const IsoFile& f) { return f.path == "ExpansionInstaller.ini"; });
  if (bonus) {
    // content/0000000000000000/FFED2000/FFFFFFFF/<package>: the 8 premium
    // packs. forza2.exe installs them into each profile (src/dlc.cpp).
    files.erase(std::remove_if(files.begin(), files.end(),
                               [](const IsoFile& f) { return f.path.rfind("content/", 0) != 0; }),
                files.end());
    for (auto& f : files) f.path = "dlc/" + f.path.substr(f.path.rfind('/') + 1);
  }
  uint64_t total = 0;
  for (const auto& f : files) total += f.size;
  progress.total = total;
  progress.done = 0;

  std::error_code ec;
  fs::create_directories(dest, ec);
  const auto space = fs::space(dest, ec);
  if (!ec && space.available < total) {
    progress.SetStatus(std::string(L("Not enough space: ", "No hay espacio suficiente: ")) + FormatGb(total) + L(" needed, ", " necesarios, ") +
                       FormatGb(space.available) + L(" free.", " libres."));
    return DiscKind::kFailed;
  }

  std::vector<char> buffer(8u << 20);
  for (const auto& f : files) {
    if (progress.cancel) {
      progress.SetStatus(L("Import cancelled.", "Importación cancelada."));
      return DiscKind::kFailed;
    }
    progress.SetStatus(std::string(L("Extracting ", "Extrayendo ")) + f.path);
    const fs::path out_path = dest / fs::u8path(f.path);
    fs::create_directories(out_path.parent_path(), ec);
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      progress.SetStatus(std::string(L("Cannot write ", "No se puede escribir ")) + out_path.u8string());
      return DiscKind::kFailed;
    }
    in.clear();
    in.seekg(static_cast<std::streamoff>(f.offset));
    uint64_t left = f.size;
    while (left) {
      const size_t chunk = size_t(std::min<uint64_t>(left, buffer.size()));
      in.read(buffer.data(), static_cast<std::streamsize>(chunk));
      if (in.gcount() != static_cast<std::streamsize>(chunk)) {
        progress.SetStatus(std::string(L("The image is truncated (failed reading ", "La imagen está truncada (fallo leyendo ")) + f.path + ").");
        return DiscKind::kFailed;
      }
      out.write(buffer.data(), static_cast<std::streamsize>(chunk));
      left -= chunk;
      progress.done += chunk;
      if (progress.cancel) break;
    }
    if (!out) {
      progress.SetStatus(std::string(L("Error writing ", "Error escribiendo ")) + out_path.u8string() + L(" (disk full?).", " (¿disco lleno?)."));
      return DiscKind::kFailed;
    }
  }
  if (bonus) {
    progress.SetStatus(std::string(L("Bonus disc: copied ", "Disco extra: copiados ")) + std::to_string(files.size()) +
                       L(" DLC packs. Each profile installs them the next time it plays.",
                         " packs de DLC. Cada perfil los instala la próxima vez que juegue."));
    return DiscKind::kBonus;
  }
  progress.SetStatus(std::string(L("Extracted ", "Extraídos ")) + std::to_string(files.size()) + L(" files (", " archivos (") + FormatGb(total) +
                     ").");
  return DiscKind::kGame;
}

VerifyReport VerifyGame(const fs::path& dir, const std::vector<ManifestEntry>& manifest,
                        bool full_hash, TaskProgress& progress) {
  VerifyReport report;
  std::vector<const ManifestEntry*> to_hash;
  uint64_t total = 0;
  for (const auto& entry : manifest) {
    std::error_code ec;
    const fs::path path = dir / fs::u8path(entry.path);
    const auto size = fs::file_size(path, ec);
    if (ec) {
      report.missing.push_back(entry.path);
    } else if (size != entry.size) {
      report.wrong_size.push_back(entry.path);
    } else if (full_hash) {
      to_hash.push_back(&entry);
      total += entry.size;
    }
  }
  if (!full_hash || !report.missing.empty() || !report.wrong_size.empty()) {
    return report;
  }

  progress.total = total;
  progress.done = 0;
  progress.SetStatus(L("Checking file integrity...", "Comprobando la integridad de los archivos..."));
  std::atomic<size_t> next{0};
  std::mutex bad_mutex;
  auto worker = [&] {
    std::vector<char> buffer(4u << 20);
    for (size_t i; (i = next++) < to_hash.size() && !progress.cancel;) {
      const ManifestEntry& entry = *to_hash[i];
      std::ifstream in(dir / fs::u8path(entry.path), std::ios::binary);
      Sha256 sha;
      while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got <= 0) break;
        sha.Update(buffer.data(), size_t(got));
        progress.done += uint64_t(got);
      }
      const std::string digest = sha.HexDigest();
      if (digest != entry.sha256 &&
          std::find(entry.alternatives.begin(), entry.alternatives.end(), digest) == entry.alternatives.end()) {
        std::lock_guard lock(bad_mutex);
        report.wrong_hash.push_back(entry.path);
      }
    }
  };
  std::vector<std::thread> threads;
  const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
  for (unsigned t = 0; t < count; ++t) threads.emplace_back(worker);
  for (auto& t : threads) t.join();
  std::sort(report.wrong_hash.begin(), report.wrong_hash.end());
  return report;
}

fs::path VerifiedMarker(const fs::path& game_dir) { return game_dir / ".forza2-verified"; }

}  // namespace launcher
