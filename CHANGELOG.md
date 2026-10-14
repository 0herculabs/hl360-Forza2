# Changelog

## 0.3.0

- **Refresh rate: 60 or 120 Hz.** *Graphics → Refresh rate* replaces the old
  *Vertical sync* switch. Turning that switch off let the game run unpaced, at
  several hundred frames per second, with constant stutter; the option is
  gone, and settings that still have it off open at 60 Hz. 60 Hz holds a
  steady 60 fps in races; 120 Hz runs races at up to 120 fps (111–115 fps in
  our tests) and needs a fast CPU. Menus run at half the rate, as on the Xbox
  360. 144 Hz is not offered: races then waited two refreshes per frame and
  ran slower than at 60 Hz.
- **Smoother races.** The game reports about 1,400 harmless graphics errors per
  second and all of them were written to the log: the filter meant to drop
  them never switched on. It works now (races at 120 Hz went from 64–105 fps
  to 111–115 fps), and log files stay small.
- **Project Forza Plus 2:** the launcher sets the mod's `CarPhysicsUpdateHz` to
  match the refresh rate (347 at 60 Hz, 695 at 120 Hz), as the mod's author
  asks, so the AI judges corners properly.
- The cover art is no longer required to build or package.

## 0.2.0

- **American release supported.** The European and American discs hold the
  same program and the same game files; only the header of `default.xex`
  differs, and the launcher accepts both.
- **Limited Edition bonus disc:** *Install → Downloadable content → Import bonus
  disc ISO* copies the 8 premium DLC packs (car packs, Road America, Motegi);
  each profile installs them the next time it plays. The disc's Title Update #4
  is not used: it patches the executable, and the port runs the unpatched one.
- **New Forza Plus 2 tab:** installs the Project Forza Plus 2 mod from its
  `.zip` (American release only), then its update; switches the career mode and
  the harder AI; sets the field of view of each camera; uninstalls it with the
  original files restored. The mod carries the DLC cars and tracks itself.
- **Install tab in two steps:** before the game is installed it only offers
  importing an ISO or choosing the game folder; afterwards it shows the
  downloadable content, file verification and the shader cache.

## 0.1.0

- First release: the European release of Forza Motorsport 2 recompiled for
  Windows, with the launcher (profiles, graphics, controls, language, ISO
  import and verification, shader warm-up).
