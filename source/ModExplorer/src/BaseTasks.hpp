#pragma once

void queueModImport(AtlasApp& app, const fs::path& path) {
    const auto key = normalizedRoot(path);
    if (key.empty()) { setStatus(app, "Elige una fuente.", "Choose a source."); return; }
    if (app.importingRoot == key || std::any_of(app.pendingImports.begin(), app.pendingImports.end(), [&](const fs::path& candidate) { return normalizedRoot(candidate) == key; })) return;
    for (const auto& mod : app.mods)
        if (normalizedRoot(mod->singleFile.empty() ? mod->catalog.root() : fs::u8path(mod->singleFile)) == key) {
            setStatus(app, "La fuente ya está cargada.", "Source is already loaded."); return;
        }
    if (app.mods.size() + app.pendingImports.size() + (app.importTask.active() ? 1 : 0) >= kMaxLoadedMods) {
        setStatus(app, "Límite de cinco fuentes, incluidas las pendientes.", "Five-source limit, including pending imports."); return;
    }
    app.pendingImports.push_back(path);
}

void tickModImports(AtlasApp& app) {
    if (app.importTask.ready()) {
        try {
            const bool cancelled = app.importTask.progress->cancelled.load();
            auto result = app.importTask.take();
            if (cancelled) setStatus(app, "Carga cancelada; las fuentes abiertas se conservan.", "Import cancelled; open sources were kept.");
            else if (!result.error.empty()) setStatus(app, result.error, result.error);
            else if (result.mod && app.mods.size() < kMaxLoadedMods) {
                const auto count = result.mod->catalog.assets().size();
                const auto label = result.mod->label;
                app.mods.push_back(std::move(result.mod)); app.stageScriptCache.clear();
                setStatus(app, std::to_string(count) + " recursos encontrados en " + label,
                    std::to_string(count) + " resources found in " + label);
                ensureSelection(app);
            }
        } catch (const std::exception& error) { setStatus(app, ensureUtf8(error.what()), ensureUtf8(error.what())); }
        app.importingRoot.clear();
    }
    if (!app.importTask.active() && !app.pendingImports.empty()) {
        const auto path = app.pendingImports.front(); app.pendingImports.erase(app.pendingImports.begin());
        app.importingRoot = normalizedRoot(path);
        app.importTask.start([path](TaskProgress& progress) {
            ModImportResult result;
            auto mod = std::make_unique<LoadedMod>();
            std::error_code ec;
            const bool single = fs::is_regular_file(path, ec) && Vfs::toLower(path.extension().u8string()) != ".zip";
            progress.update(single ? "Loading individual resource" : "Indexing mod source");
            const bool loaded = single ? mod->catalog.scanSingle(path) : mod->catalog.scan(path, &progress);
            if (progress.cancelled.load()) { result.error = "Operation cancelled"; return result; }
            if (!loaded) { result.error = mod->catalog.error(); return result; }
            mod->label = path.filename().u8string(); if (single) mod->singleFile = normalizedRoot(path);
            if (mod->label.empty()) mod->label = path.u8string();
            progress.update("Source ready", mod->catalog.assets().size(), mod->catalog.assets().size());
            result.mod = std::move(mod); return result;
        });
    }
}

void drawBaseTasks(AtlasApp& app) {
    if (app.singleExportTask.ready()) {
        try { auto result = app.singleExportTask.take(); setStatus(app, result.result, result.result); }
        catch (const std::exception& error) { setStatus(app, ensureUtf8(error.what()), ensureUtf8(error.what())); }
    }
    if (app.singleExportTask.active()) {
        ImGui::TextWrapped("%s", app.singleExportTask.progress->description().c_str());
        ImGui::TextDisabled("%s", app.spanish ? "La vista sigue disponible. La escritura actual termina de forma segura." : "The workspace remains available. The current write finishes safely.");
    }
    if (!app.importTask.active()) return;
    const auto progress = app.importTask.progress;
    ImGui::TextWrapped("%s", progress->description().c_str());
    ImGui::ProgressBar(progress->total ? static_cast<float>(progress->completed) / progress->total : 0.0f, ImVec2(-180, 0));
    ImGui::SameLine();
    if (ImGui::Button(app.spanish ? "Cancelar carga" : "Cancel import")) { app.importTask.cancel(); app.pendingImports.clear(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", app.spanish ? "Se detiene después de la lectura actual. No quita los mods abiertos." : "Stops after the current read. Does not remove open mods.");
}

void scheduleResourceExport(AtlasApp& app, const ResourceReference& reference, int target, const fs::path& folder) {
    if (app.singleExportTask.active()) { setStatus(app, "Hay una exportación en curso.", "An export is already running."); return; }
    auto snapshot = exportSourceSnapshot(app, reference);
    app.singleExportTask.start([snapshot = std::move(snapshot), reference, target, folder](TaskProgress& progress) mutable {
        return runResourceExport(std::move(snapshot), reference, target, folder, false, progress);
    });
}

void importBuilderCharacter(AtlasApp& app, int mod, int index) {
    if (app.builder.task.active()) return;
    if (mod < 0 || mod >= static_cast<int>(app.mods.size()) || index < 0 || index >= static_cast<int>(app.mods[mod]->catalog.assets().size())) {
        app.builder.status = app.builder.failure = "Select a valid character first"; return;
    }
    const auto& asset = app.mods[mod]->catalog.assets()[index];
    if (!asset.valid || asset.kind != ModExplorerAsset::Kind::Character) { app.builder.status = app.builder.failure = "Select a valid character first"; return; }
    auto vfs = app.mods[mod]->catalog.vfs(); auto character = std::get<UniversalCharacter>(asset.parsed);
    app.builder.active = app.builder.selectNext = true;
    atlas_ui::builderStart(app.builder, atlas_ui::BuilderOperation::Import, [vfs = std::move(vfs), character = std::move(character)](TaskProgress& progress) {
        atlas_ui::BuilderResult result; result.imported = importTextureCharacter(vfs, character, &progress); result.error = result.imported.error; return result;
    });
}

void importBuilderStageAsset(AtlasApp& app) {
    if (app.builder.task.active()) return;
    if (app.selectedMod < 0 || app.selectedMod >= static_cast<int>(app.mods.size()) ||
        app.selectedStageObject < 0 || app.selectedStageObject >= static_cast<int>(app.previewStage.objects.size())) {
        app.builder.status = app.builder.failure = "Select a decorative stage layer first"; return;
    }
    const auto object = app.previewStage.objects[app.selectedStageObject];
    if (!canInspectStageAsset(object)) { app.builder.status = app.builder.failure = "Select a decorative stage layer first"; return; }
    auto vfs = app.mods[app.selectedMod]->catalog.vfs();
    app.builder.active = app.builder.selectNext = true;
    atlas_ui::builderStart(app.builder, atlas_ui::BuilderOperation::Import, [vfs = std::move(vfs), object](TaskProgress& progress) {
        atlas_ui::BuilderResult result; result.imported = importTextureStageAsset(vfs, object, &progress); result.error = result.imported.error; return result;
    });
}
