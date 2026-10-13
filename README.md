# Forza Motorsport 2 — Native PC Port

Play Forza Motorsport 2 (Xbox 360) natively on Windows.

> **No game files are included.** You need your own original copy of
> Forza Motorsport 2 for Xbox 360 (a disc image, `.iso`).

**[⬇ Download the latest release](../../releases/latest)** ·
**[☕ Support on Ko-fi](https://ko-fi.com/post/Forza-Motorsport-2-native-on-PC-E2Q428PZ5Y)**

---

## Requirements

- Windows 10 or 11, 64-bit
- A GPU with Direct3D 12 support
- About 5 GB of free disk space
- Your own Forza Motorsport 2 disc image (`.iso`), multi-language edition
  (title ID `4D5307EA`)

Nothing else needs installing.

## Getting started

1. Download `Forza2-Native-<version>-windows-x64.zip` from
   [Releases](../../releases) and extract it to any folder.
2. Run **`Forza2Launcher.exe`**. It opens in English; press **ES** (top right)
   for Spanish.
3. Go to **Install** → **Choose ISO and import** and pick your disc image. The
   game is extracted to the `game\` folder and every file is checked. It takes
   a minute or two.
4. Go to **Profiles** and create your profile.
5. Press **PLAY**.

The first time you press PLAY, and after changing the internal resolution or
MSAA, the launcher prepares the shaders first (with a progress bar) and then
starts the game.

## The launcher

| Tab | What you can do |
|---|---|
| **Profiles** | Create, choose and delete profiles. Each profile has its own saves. |
| **Graphics** | Fullscreen or window, monitor, window size, VSync, quality presets (Original 720p / High 1440p / Ultra 4K), internal resolution, anisotropic filtering, MSAA, anti-aliasing and upscaling. |
| **Controls** | Controller or keyboard, mouse for the camera, and key remapping. |
| **Language & sound** | Game language (English, Español, Français, Deutsch, Italiano) and mute. |
| **Install** | Import the game from an ISO, choose the game folder, verify files and prepare shaders. |
| **Support** | Link to the project's Ko-fi. |

Changes are saved automatically.

## Controls

Any Xbox-compatible controller works without setup. Default keyboard layout:

| Action | Key |
|---|---|
| Accelerate | E |
| Brake | Q |
| Steer | A / D |
| Accept | Space |
| Back | Backspace |
| Start / pause | Enter |
| Camera | Arrow keys |

All keys can be changed in **Controls**.

## Where things are

| What | Where |
|---|---|
| Saves | `Documents\forza2\profiles\<profile>\` |
| Deleted profiles | `Documents\forza2\profiles-papelera\` |
| Shader cache | `Documents\forza2\cache\` |
| Settings | `forza2.user.toml`, next to the launcher |
| Error logs | `logs\`, next to the launcher (only kept when something goes wrong) |

## Known issues

- The first time you visit a track or see a new car you may notice some
  stutter while its shaders are compiled. After that they are prepared
  before you play.
- The game runs at 60 fps, but motion may not feel perfectly smooth yet.
- Career mode and split-screen are not fully tested.
- Xbox Live features are not available.

If the game closes with an error, the launcher tells you. Please open an
[issue](../../issues) and attach the file from the `logs\` folder.

## Support

If you enjoy the project and want it to keep improving, you can support it on
**[Ko-fi](https://ko-fi.com/post/Forza-Motorsport-2-native-on-PC-E2Q428PZ5Y)**.
Thank you! 🏁

## Legal

Not affiliated with or endorsed by Microsoft, Xbox Game Studios or Turn 10
Studios. Forza Motorsport is a trademark of Microsoft. No game files are
distributed; you must own the game. Third-party licenses are in
`THIRD_PARTY_NOTICES.txt`, included in the download.

---

### 🇪🇸 En español

Juega a Forza Motorsport 2 (Xbox 360) de forma nativa en Windows.
**No incluye archivos del juego**: necesitas tu propia copia original (`.iso`).

1. Descarga el `.zip` de [Releases](../../releases) y descomprímelo.
2. Abre `Forza2Launcher.exe` y pulsa **ES** (arriba a la derecha) para ponerlo
   en castellano.
3. En **Instalación** → **Elegir ISO e importar**, elige tu imagen de disco.
4. En **Perfiles**, crea tu perfil.
5. Pulsa **JUGAR**.

Requisitos: Windows 10/11 de 64 bits, GPU con Direct3D 12 y unos 5 GB libres.
Apoya el proyecto en
[Ko-fi](https://ko-fi.com/post/Forza-Motorsport-2-native-on-PC-E2Q428PZ5Y). ¡Gracias!
