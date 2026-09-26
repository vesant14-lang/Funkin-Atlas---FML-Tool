# Funkin Atlas - FML Tool

Funkin Atlas is a standalone character and stage explorer for Friday Night Funkin' mods. It shares the FML parsers and renderer but does not require the full FML editor. English is the default language; Spanish is available in the app.

It opens up to three mod folders, ZIP archives, or individual character/stage files. The Characters and Stages tabs include animation playback, a live spritesheet, a scene hierarchy, stage inspection, and decorative asset exports. Psych Engine, Codename Engine and V-Slice sources are supported where their resources can be resolved. A loaded chart can drive character singing; scripts, cutscenes and mechanics are not executed in the standalone preview or translated by the converted exporters.

## Build on Windows

1. Install Visual Studio 2022 or Build Tools with **Desktop development with C++** and the Windows SDK.
2. Run `build.bat` from a Developer Command Prompt or regular Command Prompt. The script locates MSVC if needed.
3. Run `FunkinAtlas.exe`. Keep `SDL3.dll` beside the executable.

The repository contains the shared FML source modules used by this app and the vendored build dependencies. No Python environment or complete FML installation is needed. `build.bat check` creates `FunkinAtlas-check.exe` without replacing a running stable executable.

## Use

Open a mod folder or ZIP, then choose Characters or Stages. The list on the right is grouped by source. Middle-drag pans a preview, and the wheel zooms at the cursor. Both spritesheet views start off: the character preview has the full width, and the pose editor opens its sheet when you choose to pick or trace frames. Use **Show live spritesheet** to inspect frames beside the preview. The Songs panel lets you choose a chart, assign strumlines to one or two characters, map each note direction to an animation, and save or load per-character presets.

The app keeps gallery links and custom animations under `%LOCALAPPDATA%\FunkinAtlas\gallery.json`. It does not edit the source mod. Converted engine packages contain visual/decorative content only. Always test an export in the selected game engine before distributing it.

## Source layout

- `ModExplorer/src`: standalone UI and character/song tools.
- `src/fml_*`: shared FML parsing, scene, render, audio and export modules.
- `third_party`: vendored dependencies with their original notices.
- `docs/FEATURES.md`: fuller feature and limitation notes.
- `docs/CORE_SYNC.md`: how to bring improvements back to the FML suite.

Changes to shared behavior should be made in `src/fml_*` in the FML workspace first, then copied into this repository with the packaging script. The standalone UI remains small and independent.

## Contributing and safety

Read [CONTRIBUTING.md](CONTRIBUTING.md) before a pull request, [SECURITY.md](SECURITY.md) for private vulnerability reports, and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency licensing. Do not upload third-party mod assets without permission. The application code is MIT-licensed; vendored dependencies keep their own licenses.

## Español

Funkin Atlas es un explorador independiente de personajes y escenarios que reutiliza el núcleo de FML, sin requerir la suite completa. Abre carpetas, ZIP y archivos individuales de Psych, Codename y V-Slice. Permite revisar animaciones, spritesheets y escenarios, y exportar elementos decorativos. Ambos spritesheets empiezan ocultos para dar todo el ancho a la vista; elegir cuadros o trazar un área abre la hoja del editor, y el spritesheet en vivo se muestra con su casilla. Las canciones pueden accionar animaciones configurables mediante presets por personaje. No ejecuta scripts ni convierte mecánicas. Para compilar en Windows, instala MSVC con C++ y ejecuta `build.bat`; deja `SDL3.dll` junto al ejecutable.
