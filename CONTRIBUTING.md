# Contributing

Thank you for improving Funkin Atlas. Keep changes focused and describe which engine and source format you tested. Character and stage behavior should be fixed in the shared `src/fml_*` modules when possible; `ModExplorer/src` should mostly contain standalone UI and orchestration.

Before a pull request:

1. Build with `build.bat check` on Windows.
2. Open at least one Psych, Codename and V-Slice resource relevant to your change. If you cannot test a format, say so.
3. Check both English and Spanish labels for UI changes.
4. Do not commit game assets, downloaded mods, local paths, gallery data, captures, or generated binaries.
5. Include reproduction steps and a before/after description. A small synthetic fixture is preferable to a copyrighted mod archive.

Use the issue templates for bugs or feature proposals. Maintain the MIT notice in source distributions and preserve each vendored dependency's own notice. Do not claim that a converted export includes runtime scripts or mechanics when it does not.
