#pragma once

ResourceReference resourceReference(const AtlasApp& app, int mod, int asset) {
    if (mod < 0 || mod >= static_cast<int>(app.mods.size())) return {};
    const auto& source = *app.mods[static_cast<size_t>(mod)];
    if (asset < 0 || asset >= static_cast<int>(source.catalog.assets().size())) return {};
    return {source.catalog.root().u8string(), source.catalog.assets()[static_cast<size_t>(asset)].key};
}

std::pair<int, int> locateResource(const AtlasApp& app, const ResourceReference& reference) {
    for (size_t mod = 0; mod < app.mods.size(); ++mod) {
        if (app.mods[mod]->catalog.root().u8string() != reference.root) continue;
        const auto& assets = app.mods[mod]->catalog.assets();
        for (size_t index = 0; index < assets.size(); ++index)
            if (assets[index].key == reference.key) return {static_cast<int>(mod), static_cast<int>(index)};
    }
    return {-1, -1};
}

ResourceExportReview inspectExportReference(const AtlasApp& app, const ResourceReference& reference, int target) {
    const auto [mod, index] = locateResource(app, reference);
    if (mod < 0) { ResourceExportReview result; result.errors.push_back("Source is no longer loaded"); return result; }
    const auto& source = *app.mods[static_cast<size_t>(mod)];
    return reviewResourceExport(source.catalog.vfs(), source.catalog.assets()[static_cast<size_t>(index)], target);
}

bool exportReference(AtlasApp& app, const ResourceReference& reference, int target, const fs::path& folder) {
    const auto [mod, index] = locateResource(app, reference);
    if (mod < 0) { setStatus(app, "La fuente ya no está cargada.", "Source is no longer loaded."); return false; }
    const auto review = inspectExportReference(app, reference, target);
    if (!review.ready()) { setStatus(app, "Exportación detenida: " + review.errors.front(), "Export stopped: " + review.errors.front()); return false; }
    struct RestoreSelection {
        AtlasApp& app; int mod, asset, target;
        ~RestoreSelection() { app.selectedMod = mod; app.selectedAsset = asset; app.exportTarget = target; }
    } restore{app, app.selectedMod, app.selectedAsset, app.exportTarget};
    app.selectedMod = mod; app.selectedAsset = index; app.exportTarget = target;
    if (target == 0) return exportOriginal(app, folder);
    if (target == 1) return exportVSlice(app, folder);
    return exportEngine(app, folder, target == 2);
}

void openExportReview(AtlasApp& app, int mod, int index) {
    auto& state = app.exportReview;
    state.source = resourceReference(app, mod, index);
    state.target = app.exportTarget;
    state.review = inspectExportReference(app, state.source, state.target);
    state.open = state.appearing = true;
}

void addExportJob(AtlasApp& app, int mod, int index) {
    auto& queue = app.exportQueue;
    if (queue.running || queue.reviewing || queue.jobs.size() >= 512) return;
    const auto reference = resourceReference(app, mod, index);
    if (reference.root.empty() || std::any_of(queue.jobs.begin(), queue.jobs.end(), [&](const ExportJob& job) { return job.source == reference; })) return;
    const auto& asset = app.mods[static_cast<size_t>(mod)]->catalog.assets()[static_cast<size_t>(index)];
    ExportJob job;
    job.source = reference;
    job.label = app.mods[static_cast<size_t>(mod)]->label + " / " + asset.id;
    queue.jobs.push_back(std::move(job));
}

void refreshExportQueue(AtlasApp& app) {
    auto& queue = app.exportQueue;
    if (queue.running) return;
    for (auto& job : queue.jobs) { job.review = {}; job.reviewed = false; }
    queue.reviewing = true; queue.reviewNext = 0; queue.cancel = false;
}

void writeExportQueueManifest(AtlasApp& app) {
    auto& queue = app.exportQueue;
    if (queue.folder.empty()) return;
    json manifest;
    manifest["application"] = "Funkin Atlas - FML Tool";
    manifest["target"] = queue.target;
    manifest["cancelled"] = queue.cancel;
    manifest["scope"] = queue.target == 0 ? "Original files, source scripts are not verified" : "Visual/decorative content only; original scripts and mechanics are not translated";
    manifest["jobs"] = json::array();
    for (const auto& job : queue.jobs)
        manifest["jobs"].push_back({{"sourceRoot", job.source.root}, {"resource", job.source.key},
            {"label", job.label}, {"state", !job.done ? "not exported" : job.success ? "success" : "failed"},
            {"result", job.result}, {"errors", job.review.errors}, {"warnings", job.review.warnings}});
    const fs::path path = queue.folder / "export-report.json";
    std::ofstream file(path, std::ios::binary);
    file << manifest.dump(2, ' ', false, json::error_handler_t::replace);
    file.close();
    queue.manifest = file ? path.u8string() : "Could not write export-report.json";
}

void beginExportQueue(AtlasApp& app, const fs::path& parent) {
    auto& queue = app.exportQueue;
    if (queue.running || queue.jobs.empty()) return;
    queue.folder = availablePath(parent / "FunkinAtlas-batch", true);
    std::error_code error;
    if (!fs::create_directory(queue.folder, error)) { setStatus(app, "No se pudo crear la carpeta del lote.", "Could not create the batch folder."); return; }
    queue.reviewing = false;
    for (auto& job : queue.jobs) { job.done = job.success = false; job.result.clear(); }
    queue.next = 0; queue.cancel = false; queue.running = true; queue.manifest.clear();
}

void tickExportQueue(AtlasApp& app) {
    auto& queue = app.exportQueue;
    if (queue.reviewing) {
        if (queue.cancel || queue.reviewNext >= queue.jobs.size()) { queue.reviewing = false; return; }
        auto& job = queue.jobs[queue.reviewNext++];
        try { job.review = inspectExportReference(app, job.source, queue.target); }
        catch (const std::exception& error) { job.review.errors = {ensureUtf8(error.what())}; }
        job.reviewed = true;
        return;
    }
    if (!queue.running) return;
    if (queue.cancel || queue.next >= queue.jobs.size()) {
        queue.running = false;
        writeExportQueueManifest(app);
        const size_t saved = std::count_if(queue.jobs.begin(), queue.jobs.end(), [](const ExportJob& job) { return job.done && job.success; });
        const size_t failed = std::count_if(queue.jobs.begin(), queue.jobs.end(), [](const ExportJob& job) { return job.done && !job.success; });
        setStatus(app, std::string(queue.cancel ? "Lote cancelado: " : "Lote terminado: ") + std::to_string(saved) + " guardados, " + std::to_string(failed) + " con error. " + queue.manifest,
                       std::string(queue.cancel ? "Batch cancelled: " : "Batch completed: ") + std::to_string(saved) + " saved, " + std::to_string(failed) + " failed. " + queue.manifest);
        return;
    }
    auto& job = queue.jobs[queue.next];
    try {
        job.review = inspectExportReference(app, job.source, queue.target);
        job.reviewed = true;
        if (!job.review.ready()) job.result = job.review.errors.front();
        else {
            const fs::path destination = queue.folder / fs::u8path(std::to_string(queue.next + 1) + "-" + safeName(job.label));
            std::error_code error;
            if (!fs::create_directory(destination, error)) job.result = "Could not create output folder: " + error.message();
            else { job.success = exportReference(app, job.source, queue.target, destination); job.result = app.statusEn; }
        }
    } catch (const std::exception& error) { job.result = ensureUtf8(error.what()); }
    catch (...) { job.result = "Unexpected export failure"; }
    job.done = true;
    ++queue.next;
}

void drawReviewReport(const ResourceExportReview& review, bool es) {
    ImGui::TextColored(review.ready() ? ImVec4(0.4f, 0.9f, 0.65f, 1) : ImVec4(1, 0.45f, 0.4f, 1), "%s",
        review.ready() ? (es ? "LISTO PARA EXPORTAR" : "READY TO EXPORT") : (es ? "REQUIERE ATENCIÓN" : "NEEDS ATTENTION"));
    ImGui::Text(es ? "%zu archivos · %zu hojas · %zu animaciones · %.1f MiB" : "%zu files · %zu sheets · %zu animations · %.1f MiB",
        review.files.size(), review.pages, review.animations, review.bytes / (1024.0 * 1024.0));
    for (const auto& error : review.errors) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.48f, 1)); ImGui::TextWrapped("%s", error.c_str()); ImGui::PopStyleColor(); }
    for (const auto& warning : review.warnings) ImGui::TextWrapped("%s", warning.c_str());
    if (ImGui::TreeNode(es ? "Archivos de origen" : "Source files")) {
        for (const auto& file : review.files) ImGui::TextWrapped("%s", file.c_str());
        ImGui::TreePop();
    }
}

void drawExportTools(AtlasApp& app, SDL_Window* window) {
    const bool es = app.spanish;
    const char* targets[] = {es ? "Archivos originales" : "Original files", "V-Slice ZIP", "Psych Engine", "Codename Engine"};
    auto& review = app.exportReview;
    if (review.open) {
        if (review.appearing) { ImGui::SetNextWindowSize(ImVec2(690, 560), ImGuiCond_Always); ImGui::SetNextWindowFocus(); review.appearing = false; }
        if (ImGui::Begin(es ? "Revisión previa###export-review" : "Export review###export-review", &review.open, ImGuiWindowFlags_NoSavedSettings)) {
            const auto [mod, index] = locateResource(app, review.source);
            if (mod >= 0) ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.50f, 1), "%s / %s", app.mods[mod]->label.c_str(), app.mods[mod]->catalog.assets()[index].id.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##review-target", &review.target, targets, 4)) review.review = inspectExportReference(app, review.source, review.target);
            ImGui::BeginChild("##review-data", ImVec2(0, -65), true);
            drawReviewReport(review.review, es);
            ImGui::TextDisabled("%s", es ? "Esta revisión no sustituye una prueba dentro del motor." : "This review does not replace testing inside the engine.");
            ImGui::EndChild();
            ImGui::BeginDisabled(!review.review.ready());
            if (ImGui::Button(es ? "Elegir carpeta y exportar" : "Choose folder and export")) { app.dialogAction = DialogAction::Export; SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false); }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(es ? "Actualizar revisión" : "Refresh review")) review.review = inspectExportReference(app, review.source, review.target);
        }
        ImGui::End();
    }
    auto& queue = app.exportQueue;
    if (queue.open) {
        if (queue.appearing) { ImGui::SetNextWindowSize(ImVec2(870, 650), ImGuiCond_Always); ImGui::SetNextWindowFocus(); queue.appearing = false; }
        if (ImGui::Begin(es ? "Exportación múltiple###batch-export" : "Batch export###batch-export", &queue.open, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.50f, 1), "%s", es ? "COLA DE RECURSOS" : "RESOURCE QUEUE");
            ImGui::TextWrapped("%s", es ? "No altera tu preview, poses ni canción. Puedes arrastrar recursos desde la lista. Cada recurso se exporta en su propia carpeta." : "Does not change your preview, poses or song. Drag resources from the list. Each resource gets its own folder.");
            ImGui::BeginDisabled(queue.running || queue.reviewing);
            ImGui::SetNextItemWidth(240);
            if (ImGui::Combo("##batch-target", &queue.target, targets, 4)) refreshExportQueue(app);
            if (ImGui::Button(es ? "Añadir selección" : "Add selected")) addExportJob(app, app.selectedMod, app.selectedAsset);
            ImGui::SameLine();
            if (ImGui::Button(es ? "Añadir lista filtrada" : "Add filtered list"))
                for (size_t mod = 0; mod < app.mods.size(); ++mod)
                    for (size_t index = 0; index < app.mods[mod]->catalog.assets().size(); ++index)
                        if (visible(app, static_cast<int>(mod), app.mods[mod]->catalog.assets()[index])) addExportJob(app, static_cast<int>(mod), static_cast<int>(index));
            ImGui::SameLine();
            if (ImGui::Button(es ? "Vaciar" : "Clear")) queue.jobs.clear();
            ImGui::EndDisabled();
            ImGui::BeginChild("##batch-jobs", ImVec2(0, -92), true);
            if (queue.jobs.empty()) ImGui::TextDisabled("%s", es ? "Añade recursos o arrástralos aquí." : "Add resources or drop them here.");
            if (!queue.running && !queue.reviewing && ImGui::BeginDragDropTargetCustom(ImRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y)), ImGui::GetID("##queue-drop"))) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ATLAS_RESOURCE")) {
                    if (payload->DataSize == sizeof(std::array<int, 2>)) { const auto& indices = *static_cast<const std::array<int, 2>*>(payload->Data); addExportJob(app, indices[0], indices[1]); }
                }
                ImGui::EndDragDropTarget();
            }
            int erase = -1;
            for (size_t index = 0; index < queue.jobs.size(); ++index) {
                const auto& job = queue.jobs[index]; ImGui::PushID(static_cast<int>(index));
                ImGui::BeginDisabled(queue.running || queue.reviewing);
                if (ImGui::SmallButton("x")) erase = static_cast<int>(index);
                ImGui::EndDisabled(); ImGui::SameLine();
                const std::string state = job.done ? (job.success ? (es ? "Guardado" : "Saved") : (es ? "Error" : "Failed")) : !job.reviewed ? (es ? "Sin revisar" : "Not reviewed") : job.review.ready() ? (es ? "Listo" : "Ready") : (es ? "Bloqueado" : "Blocked");
                ImGui::SetNextItemOpen(index == 0, ImGuiCond_Once);
                if (ImGui::TreeNode("##job", "%zu · %s · %s", index + 1, job.label.c_str(), state.c_str())) {
                    if (job.reviewed) drawReviewReport(job.review, es);
                    else ImGui::TextDisabled("%s", es ? "Se revisa antes de escribir los archivos." : "Reviewed before any files are written.");
                    if (!job.result.empty()) ImGui::TextWrapped("%s", job.result.c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (erase >= 0) queue.jobs.erase(queue.jobs.begin() + erase);
            ImGui::EndChild();
            if (queue.running || queue.reviewing) {
                ImGui::ProgressBar(static_cast<float>(queue.reviewing ? queue.reviewNext : queue.next) / std::max<size_t>(1, queue.jobs.size()), ImVec2(-1, 0));
                if (ImGui::Button(es ? "Cancelar después de este recurso" : "Cancel after current resource")) queue.cancel = true;
            } else {
                ImGui::BeginDisabled(queue.jobs.empty());
                if (ImGui::Button(es ? "Elegir carpeta y exportar lote" : "Choose folder and export batch")) { app.dialogAction = DialogAction::BatchExport; SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false); }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(es ? "Revisar todo" : "Review all")) refreshExportQueue(app);
            }
            if (!queue.manifest.empty()) ImGui::TextWrapped("%s", queue.manifest.c_str());
        }
        ImGui::End();
    }
    tickExportQueue(app);
}
