#pragma once
#include "../../src/fml_formats/TextureAtlas.hpp"
#include "../../src/fml_app/TextureAsset.hpp"
#include "../../src/fml_core/TextEncoding.hpp"
#include <array>
#include <functional>

namespace atlas_ui {

enum class BuilderOperation { Import, Build, Save, Open, Export, Autosave };
enum class BuilderDialog { Images, Folder, Open, Save, Export };
enum class BuilderPending { None, New, Open, Close };
struct BuilderInbox {
    std::mutex mutex;
    bool ready = false;
    BuilderDialog action = BuilderDialog::Images;
    std::vector<std::filesystem::path> paths;
};
struct BuilderResult {
    fml::TextureImport imported;
    fml::TextureProject project;
    std::shared_ptr<fml::TextureBuild> built;
    std::filesystem::path path;
    std::string error;
    bool success = false;
};
struct AtlasBuilderState {
    fml::TextureProject project;
    fml::TextureProject savedProject;
    fml::TextureHistory history;
    std::shared_ptr<fml::TextureBuild> built;
    fml::BackgroundTask<BuilderResult> task;
    std::shared_ptr<BuilderInbox> inbox = std::make_shared<BuilderInbox>();
    BuilderOperation operation = BuilderOperation::Import;
    std::filesystem::path projectPath, recoveryPath;
    std::string status, orderError, failure;
    std::array<char, 129> name{}, animationName{};
    std::array<char, 129> frameName{}, frameSearch{};
    std::array<char, 32768> order{};
    AssetCanvasView poseView, sheetView, sourceView;
    int selectedAnimation = 0, selectedFrame = -1, selectedStep = -1, page = 0;
    int background = 0, cell[2]{16, 16}, region[4]{0, 0, 16, 16};
    unsigned int poseTexture = 0, sheetTexture = 0, sourceTexture = 0;
    const fml::AtlasPixels* uploadedPose = nullptr;
    const fml::AtlasPixels* uploadedSource = nullptr;
    const fml::TextureBuild* uploadedBuild = nullptr;
    int uploadedPage = -1;
    bool active = false, selectNext = false, playing = true, showBounds = true;
    bool dialogBusy = false, animateExport = false, trace = false, tracing = false;
    bool dirty = false, recoveryAvailable = false, closeReady = false, rawImages = false, showSheet = true, narrowSheet = false;
    BuilderPending pending = BuilderPending::None;
    std::filesystem::path pendingPath;
    ImGuiID historyWidget = 0;
    float panelWidth = 285, previewHeight = 330;
    unsigned long long revision = 0, builtRevision = 0, savedRevision = 0, recoveryRevision = 0, taskRevision = 0;
    double clock = 0, lastAutosave = 0;
    ImVec2 traceStart{};
    int sourceFrame = 0;
    void releaseTextures() {
        if (poseTexture) glDeleteTextures(1, &poseTexture);
        if (sheetTexture) glDeleteTextures(1, &sheetTexture);
        if (sourceTexture) glDeleteTextures(1, &sourceTexture);
        poseTexture = sheetTexture = sourceTexture = 0; uploadedPose = uploadedSource = nullptr; uploadedBuild = nullptr;
    }
};

inline void builderChanged(AtlasBuilderState& state, bool merge = false) {
    state.history.record(state.project, merge);
    ++state.revision; state.dirty = !fml::sameTextureProject(state.project, state.savedProject); state.status.clear(); state.failure.clear();
    if (!merge) state.historyWidget = 0;
}
inline void builderWidgetChanged(AtlasBuilderState& state, bool changed) {
    if (ImGui::IsItemActivated()) state.historyWidget = 0;
    if (!changed) return;
    const auto id = ImGui::GetItemID();
    builderChanged(state, state.historyWidget == id && ImGui::IsItemActive());
    state.historyWidget = id;
}
inline void builderSyncFields(AtlasBuilderState& state) {
    state.animateExport = state.project.animate;
    std::snprintf(state.name.data(), state.name.size(), "%s", state.project.name.c_str());
    state.order.fill(0); state.animationName.fill(0);
    if (state.selectedAnimation >= 0 && state.selectedAnimation < static_cast<int>(state.project.animations.size())) {
        const auto& animation = state.project.animations[state.selectedAnimation];
        std::snprintf(state.animationName.data(), state.animationName.size(), "%s", animation.name.c_str());
        std::string order;
        for (size_t index : animation.frames) { if (!order.empty()) order += ", "; order += std::to_string(index + 1); }
        std::snprintf(state.order.data(), state.order.size(), "%s", order.c_str());
    }
    state.clock = 0; state.selectedStep = -1; state.orderError.clear();
    state.selectedFrame = std::clamp(state.selectedFrame, -1, static_cast<int>(state.project.frames.size()) - 1);
    if (state.selectedFrame >= 0) std::snprintf(state.frameName.data(), state.frameName.size(), "%s", state.project.frames[state.selectedFrame].name.c_str());
    else state.frameName.fill(0);
}
inline void builderClearRecovery(AtlasBuilderState& state) {
    if (state.recoveryPath.filename() == "builder-recovery.fmlatlas") { std::error_code error; std::filesystem::remove(state.recoveryPath, error); }
    state.recoveryAvailable = false;
}
inline void builderResetProject(AtlasBuilderState& state) {
    state.project = {}; state.built.reset(); state.projectPath.clear();
    state.savedProject = state.project; state.history.reset(state.project); state.historyWidget = 0;
    state.selectedAnimation = 0; state.selectedFrame = -1; state.sourceFrame = 0;
    state.uploadedPose = state.uploadedSource = nullptr; state.uploadedBuild = nullptr; state.uploadedPage = -1;
    state.poseView = {}; state.sheetView = {}; state.sourceView = {};
    state.clock = 0; state.tracing = false; state.failure.clear(); state.status.clear();
    ++state.revision; state.dirty = false; state.recoveryRevision = state.revision; builderSyncFields(state);
}
inline void builderUndo(AtlasBuilderState& state, bool redo = false) {
    if (state.task.active() || state.dialogBusy || !(redo ? state.history.redo(state.project) : state.history.undo(state.project))) return;
    ++state.revision; state.dirty = !fml::sameTextureProject(state.project, state.savedProject); state.historyWidget = 0;
    state.selectedAnimation = std::clamp(state.selectedAnimation, 0, std::max(0, static_cast<int>(state.project.animations.size()) - 1));
    state.sourceFrame = std::clamp(state.sourceFrame, 0, std::max(0, static_cast<int>(state.project.frames.size()) - 1));
    state.uploadedPose = state.uploadedSource = nullptr; state.failure.clear(); builderSyncFields(state);
    state.status = redo ? "Change redone" : "Change undone";
    if (!state.dirty) builderClearRecovery(state);
}
inline void builderSelectFrame(AtlasBuilderState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.project.frames.size())) return;
    state.selectedFrame = state.sourceFrame = index; state.playing = false; state.selectedStep = -1;
    std::snprintf(state.frameName.data(), state.frameName.size(), "%s", state.project.frames[index].name.c_str());
    if (state.selectedAnimation >= 0 && state.selectedAnimation < static_cast<int>(state.project.animations.size())) {
        const auto& frames = state.project.animations[state.selectedAnimation].frames;
        const auto found = std::find(frames.begin(), frames.end(), static_cast<size_t>(index));
        if (found != frames.end()) state.selectedStep = static_cast<int>(found - frames.begin());
    }
}
inline void builderSelectStep(AtlasBuilderState& state, int step) {
    if (state.selectedAnimation < 0 || state.selectedAnimation >= static_cast<int>(state.project.animations.size())) return;
    const auto& order = state.project.animations[state.selectedAnimation].frames;
    if (step < 0 || step >= static_cast<int>(order.size())) return;
    builderSelectFrame(state, static_cast<int>(order[step])); state.selectedStep = step; state.clock = step;
}
inline void builderSelectAnimation(AtlasBuilderState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.project.animations.size())) return;
    state.selectedAnimation = index; builderSyncFields(state); state.playing = true;
    const auto& frames = state.project.animations[index].frames;
    if (!frames.empty()) { state.selectedFrame = state.sourceFrame = static_cast<int>(frames.front()); std::snprintf(state.frameName.data(), state.frameName.size(), "%s", state.project.frames[frames.front()].name.c_str()); }
}
inline void builderRemoveFrame(AtlasBuilderState& state, int index) {
    if (index < 0 || !fml::removeTextureFrames(state.project, {static_cast<size_t>(index)})) return;
    state.selectedAnimation = std::min(state.selectedAnimation, std::max(0, static_cast<int>(state.project.animations.size()) - 1));
    state.selectedFrame = state.sourceFrame = state.project.frames.empty() ? -1 : std::min(index, static_cast<int>(state.project.frames.size()) - 1);
    state.uploadedPose = state.uploadedSource = nullptr; builderChanged(state); builderSyncFields(state);
}
inline int builderCurrentFrame(AtlasBuilderState& state, double seconds, int& step) {
    step = -1;
    if (state.selectedAnimation < 0 || state.selectedAnimation >= static_cast<int>(state.project.animations.size())) return state.selectedFrame;
    const auto& animation = state.project.animations[state.selectedAnimation];
    if (animation.frames.empty() || (!state.playing && state.selectedStep < 0)) return state.selectedFrame;
    if (state.playing) state.clock += std::max(0.0, seconds) * std::max(1, animation.fps);
    if (!animation.loop && state.clock >= animation.frames.size()) { state.playing = false; state.selectedStep = static_cast<int>(animation.frames.size()) - 1; }
    step = state.selectedStep >= 0 ? state.selectedStep : (animation.loop ? static_cast<int>(std::fmod(state.clock, animation.frames.size())) :
        static_cast<int>(std::min(state.clock, static_cast<double>(animation.frames.size() - 1))));
    step = std::clamp(step, 0, static_cast<int>(animation.frames.size()) - 1);
    return static_cast<int>(animation.frames[step]);
}
inline void builderResume(AtlasBuilderState& state) {
    if (state.selectedStep >= 0) state.clock = state.selectedStep;
    if (state.selectedAnimation >= 0 && state.selectedAnimation < static_cast<int>(state.project.animations.size())) {
        const auto& animation = state.project.animations[state.selectedAnimation];
        if (!animation.loop && state.selectedStep == static_cast<int>(animation.frames.size()) - 1) state.clock = 0;
    }
    state.selectedStep = -1; state.playing = true;
}
inline void builderMoveStep(AtlasBuilderState& state, int step, int direction) {
    if (state.selectedAnimation < 0 || state.selectedAnimation >= static_cast<int>(state.project.animations.size())) return;
    auto& frames = state.project.animations[state.selectedAnimation].frames;
    if (step < 0 || step >= static_cast<int>(frames.size()) || step + direction < 0 || step + direction >= static_cast<int>(frames.size())) return;
    std::swap(frames[step], frames[step + direction]); builderChanged(state); builderSyncFields(state);
    state.selectedStep = step + direction; state.playing = false; state.selectedFrame = static_cast<int>(frames[state.selectedStep]);
}
inline void builderMoveStepTo(AtlasBuilderState& state, int from, int to) {
    if (state.selectedAnimation < 0 || state.selectedAnimation >= static_cast<int>(state.project.animations.size())) return;
    auto& frames = state.project.animations[state.selectedAnimation].frames;
    if (from < 0 || to < 0 || from >= static_cast<int>(frames.size()) || to >= static_cast<int>(frames.size()) || from == to) return;
    const size_t value = frames[from]; frames.erase(frames.begin() + from); frames.insert(frames.begin() + to, value);
    builderChanged(state); builderSyncFields(state); builderSelectStep(state, to);
}
inline bool builderApplyOrder(AtlasBuilderState& state) {
    if (state.selectedAnimation < 0 || state.selectedAnimation >= static_cast<int>(state.project.animations.size())) return false;
    std::vector<size_t> order;
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return true;
        if (token.size() > 8 || !std::all_of(token.begin(), token.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
        const size_t value = static_cast<size_t>(std::stoul(token)); token.clear();
        if (!value || value > state.project.frames.size() || order.size() >= 4096) return false;
        order.push_back(value - 1); return true;
    };
    for (char c : std::string(state.order.data())) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) { if (!flush()) { state.orderError = "Invalid frame order"; return false; } }
        else token += c;
    }
    if (!flush()) { state.orderError = "Frame numbers must refer to the frame bank"; return false; }
    state.project.animations[state.selectedAnimation].frames = std::move(order);
    builderChanged(state); state.clock = 0; state.selectedStep = -1; state.orderError.clear(); return true;
}
inline void builderAppendImport(AtlasBuilderState& state, fml::TextureImport imported) {
    if (!imported.error.empty()) { state.status = imported.error; return; }
    if (imported.frames.size() > 1024 - state.project.frames.size()) { state.status = state.failure = "Project exceeds 1024 frames"; return; }
    if (imported.animations.size() > 256 - state.project.animations.size() || imported.warnings.size() > 1024 - state.project.sourceWarnings.size()) {
        state.status = state.failure = "Project exceeds animation or warning limits"; return;
    }
    size_t memory = 0;
    for (const auto& frame : state.project.frames) memory += frame.pixels->rgba.size();
    for (const auto& frame : imported.frames) {
        if (frame.pixels->rgba.size() > 256u * 1024u * 1024u - memory) { state.status = state.failure = "Project exceeds 256 MB"; return; }
        memory += frame.pixels->rgba.size();
    }
    const size_t offset = state.project.frames.size();
    if (!imported.animations.empty()) state.selectedAnimation = static_cast<int>(state.project.animations.size());
    for (auto& animation : imported.animations) {
        for (auto& index : animation.frames) index += offset;
        const std::string original = animation.name;
        size_t suffix = 1;
        while (std::any_of(state.project.animations.begin(), state.project.animations.end(), [&](const fml::TextureAnimation& existing) { return existing.name == animation.name; }))
            animation.name = original + "-" + std::to_string(++suffix);
        state.project.animations.push_back(std::move(animation));
    }
    for (auto& frame : imported.frames) state.project.frames.push_back(std::move(frame));
    state.selectedFrame = static_cast<int>(offset); state.sourceFrame = state.selectedFrame;
    state.playing = true;
    for (const auto& warning : imported.warnings) state.project.sourceWarnings.push_back(warning);
    builderChanged(state); builderSyncFields(state);
    state.status = std::to_string(imported.frames.size()) + " frames imported";
}
template<class F> inline bool builderStart(AtlasBuilderState& state, BuilderOperation operation, F function) {
    if (state.task.active()) return false;
    state.operation = operation; state.taskRevision = state.revision; state.status.clear(); state.failure.clear();
    return state.task.start(std::move(function));
}
inline void builderImportPaths(AtlasBuilderState& state, std::vector<std::filesystem::path> paths) {
    builderStart(state, BuilderOperation::Import, [paths = std::move(paths), raw = state.rawImages](fml::TaskProgress& progress) {
        BuilderResult result; result.imported = fml::importTextureInputs(paths, raw, &progress); result.error = result.imported.error; return result;
    });
}
inline void builderExtract(AtlasBuilderState& state, bool all) {
    if (state.sourceFrame < 0 || state.sourceFrame >= static_cast<int>(state.project.frames.size())) return;
    const auto frame = state.project.frames[state.sourceFrame];
    std::vector<fml::AtlasFrame> regions;
    if (all) {
        const int columns = frame.pixels->w / std::max(1, state.cell[0]), rows = frame.pixels->h / std::max(1, state.cell[1]);
        if (!columns || !rows || static_cast<size_t>(columns) * rows > 1024 - state.project.frames.size()) {
            state.status = "Grid exceeds the remaining frame count, or cells are larger than the image"; return;
        }
        for (int row = 0; row < rows; ++row) for (int column = 0; column < columns; ++column) {
            fml::AtlasFrame region; region.x = column * state.cell[0]; region.y = row * state.cell[1]; region.w = state.cell[0]; region.h = state.cell[1];
            regions.push_back(region);
        }
    } else {
        fml::AtlasFrame region; region.x = state.region[0]; region.y = state.region[1]; region.w = state.region[2]; region.h = state.region[3]; regions.push_back(region);
    }
    builderStart(state, BuilderOperation::Import, [frame, regions = std::move(regions)](fml::TaskProgress& progress) {
        BuilderResult result; result.imported = fml::extractTextureRegions(frame, regions, &progress); result.error = result.imported.error; return result;
    });
}
inline void builderBuild(AtlasBuilderState& state) {
    auto project = state.project;
    builderStart(state, BuilderOperation::Build, [project = std::move(project)](fml::TaskProgress& progress) {
        BuilderResult result; result.built = std::make_shared<fml::TextureBuild>(fml::buildTextureAtlas(project, &progress));
        result.error = result.built->error; return result;
    });
}
inline void builderSave(AtlasBuilderState& state, std::filesystem::path path, bool recovery) {
    auto project = state.project;
    if (path.extension() != ".fmlatlas") path += ".fmlatlas";
    builderStart(state, recovery ? BuilderOperation::Autosave : BuilderOperation::Save,
        [path, project = std::move(project)](fml::TaskProgress& progress) {
            BuilderResult result; result.path = path; result.success = fml::saveTextureProject(path, project, result.error, &progress); if (result.success) result.project = project; return result;
        });
}
inline void builderOpen(AtlasBuilderState& state, const std::filesystem::path& path) {
    builderStart(state, BuilderOperation::Open, [path](fml::TaskProgress& progress) {
        BuilderResult result; result.path = path; result.success = fml::loadTextureProject(path, result.project, result.error, &progress); return result;
    });
}
inline void builderCompletePending(AtlasBuilderState& state) {
    const auto pending = state.pending; const auto path = state.pendingPath;
    state.pending = BuilderPending::None; state.pendingPath.clear();
    if (pending == BuilderPending::New) builderResetProject(state);
    else if (pending == BuilderPending::Open) builderOpen(state, path);
    else if (pending == BuilderPending::Close) { state.dirty = false; state.closeReady = true; }
}
inline void builderRequest(AtlasBuilderState& state, BuilderPending pending, const std::filesystem::path& path = {}) {
    state.pending = pending; state.pendingPath = path;
    if (!state.dirty && !state.task.active() && !state.dialogBusy) builderCompletePending(state);
}
inline void builderExport(AtlasBuilderState& state, const std::filesystem::path& folder) {
    if (!state.built || state.builtRevision != state.revision) { state.status = "Build the atlas again after editing"; return; }
    auto project = state.project; auto built = state.built; const bool animate = state.animateExport;
    builderStart(state, BuilderOperation::Export, [folder, project = std::move(project), built, animate](fml::TaskProgress& progress) {
        BuilderResult result; result.success = fml::exportTextureAtlas(folder, project, *built, animate, result.path, result.error, &progress); return result;
    });
}
struct BuilderDialogRequest { std::shared_ptr<BuilderInbox> inbox; BuilderDialog action; };
inline void SDLCALL builderDialogSelected(void* userdata, const char* const* files, int) {
    std::unique_ptr<BuilderDialogRequest> request(static_cast<BuilderDialogRequest*>(userdata));
    std::lock_guard<std::mutex> lock(request->inbox->mutex);
    request->inbox->paths.clear();
    if (files) for (size_t index = 0; files[index] && index < 1024; ++index) request->inbox->paths.push_back(std::filesystem::u8path(files[index]));
    request->inbox->action = request->action; request->inbox->ready = true;
}
inline void builderDialog(AtlasBuilderState& state, SDL_Window* window, BuilderDialog action) {
    if (state.dialogBusy || state.task.active()) return;
    state.dialogBusy = true;
    auto* request = new BuilderDialogRequest{state.inbox, action};
    static const SDL_DialogFileFilter images[] = {{"PNG, Sparrow/Packer, Animate Animation.json", "png;xml;txt;json"}};
    static const SDL_DialogFileFilter projects[] = {{"Funkin Atlas project", "fmlatlas"}};
    if (action == BuilderDialog::Folder || action == BuilderDialog::Export)
        SDL_ShowOpenFolderDialog(builderDialogSelected, request, window, nullptr, false);
    else if (action == BuilderDialog::Save)
        SDL_ShowSaveFileDialog(builderDialogSelected, request, window, projects, 1,
            state.projectPath.empty() ? nullptr : state.projectPath.u8string().c_str());
    else SDL_ShowOpenFileDialog(builderDialogSelected, request, window, action == BuilderDialog::Open ? projects : images, 1, nullptr, action == BuilderDialog::Images);
}
inline void builderTick(AtlasBuilderState& state, bool autosave = true) {
    if (state.task.ready()) {
        try {
            auto result = state.task.take();
            if (!result.error.empty()) state.status = state.failure = result.error;
            else if (state.operation == BuilderOperation::Import) builderAppendImport(state, std::move(result.imported));
            else if (state.operation == BuilderOperation::Build) {
                state.built = std::move(result.built); state.builtRevision = state.taskRevision; state.page = 0;
                state.sheetView.zoom = 1; state.sheetView.pan = ImVec2(0, 0); state.uploadedBuild = nullptr;
                state.status = "Atlas ready";
            } else if (state.operation == BuilderOperation::Open && result.success) {
                state.project = std::move(result.project); state.built.reset(); state.projectPath = result.path;
                state.history.reset(state.project); state.savedProject = state.project; state.historyWidget = 0;
                state.uploadedPose = state.uploadedSource = nullptr; state.uploadedBuild = nullptr; state.uploadedPage = -1;
                state.poseView = {}; state.sheetView = {}; state.sourceView = {};
                state.selectedAnimation = 0; state.selectedFrame = state.project.frames.empty() ? -1 : 0; state.sourceFrame = 0;
                state.playing = true;
                ++state.revision; state.savedRevision = state.revision; state.dirty = false;
                builderSyncFields(state); state.status = "Project opened";
                if (result.path == state.recoveryPath) { state.projectPath.clear(); state.savedProject = {}; state.dirty = true; }
            } else if (state.operation == BuilderOperation::Save && result.success) {
                state.projectPath = result.path; state.savedRevision = state.taskRevision;
                state.savedProject = std::move(result.project);
                state.dirty = !fml::sameTextureProject(state.project, state.savedProject); state.status = "Project saved: " + result.path.u8string();
                if (!state.dirty) { builderClearRecovery(state); builderCompletePending(state); }
            } else if (state.operation == BuilderOperation::Autosave && result.success) {
                state.recoveryRevision = state.taskRevision; state.recoveryAvailable = true;
            } else if (state.operation == BuilderOperation::Export && result.success) state.status = "Atlas exported: " + result.path.u8string();
        } catch (const std::exception& error) { state.status = state.failure = fml::ensureUtf8(error.what()); }
    }
    std::vector<std::filesystem::path> paths; BuilderDialog action{}; bool ready = false;
    {
        std::lock_guard<std::mutex> lock(state.inbox->mutex);
        if (state.inbox->ready) { paths.swap(state.inbox->paths); action = state.inbox->action; state.inbox->ready = false; ready = true; }
    }
    if (ready) {
        state.dialogBusy = false;
        if (!paths.empty()) {
            if (action == BuilderDialog::Save) builderSave(state, paths.front(), false);
            else if (action == BuilderDialog::Open) {
                builderRequest(state, BuilderPending::Open, paths.front());
            } else if (action == BuilderDialog::Export) builderExport(state, paths.front());
            else builderImportPaths(state, std::move(paths));
        }
    }
    if (state.pending != BuilderPending::None && !state.dirty && !state.task.active() && !state.dialogBusy) builderCompletePending(state);
    if (autosave && state.dirty && !state.task.active() && !state.dialogBusy && !state.recoveryPath.empty() &&
        state.recoveryRevision != state.revision && ImGui::GetTime() - state.lastAutosave > 8) {
        state.lastAutosave = ImGui::GetTime(); builderSave(state, state.recoveryPath, true);
    }
}
inline void builderSameLine(float width) {
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (right - ImGui::GetItemRectMax().x > width + ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
}
inline std::string builderMessage(const std::string& message, bool es) {
    if (!es) return message;
    static const std::pair<const char*, const char*> messages[] = {
        {"Atlas ready", "Atlas listo"}, {"Project opened", "Proyecto abierto"}, {"Project saved: ", "Proyecto guardado: "},
        {"Atlas exported: ", "Atlas exportado: "}, {"Change undone", "Cambio deshecho"}, {"Change redone", "Cambio rehecho"},
        {"Operation cancelled", "Operación cancelada"}, {"Build the atlas again after editing", "Construye el atlas de nuevo después de editar"},
        {"Invalid frame order", "Orden de frames no válido"}, {"Frame numbers must refer to the frame bank", "Los números deben corresponder al banco de frames"},
        {"Invalid packing settings", "Configuración de empaquetado no válida"}, {"A project needs 1 to 1024 frames", "El proyecto necesita entre 1 y 1024 frames"},
        {"Invalid or duplicate animation: ", "Animación no válida o repetida: "}, {"Invalid frame: ", "Frame no válido: "},
        {"Frame does not fit selected page size: ", "El frame no cabe en el tamaño de página elegido: "},
        {"Frames exceed the page count or 256 MB output limit", "Los frames superan las páginas permitidas o los 256 MB de salida"},
        {"Animation sequences exceed 4096 output frames", "Las secuencias superan los 4096 pasos de salida"},
        {"Animation references a missing frame", "Una animación referencia un frame que falta"},
        {"Source frames exceed 256 MB", "Los frames fuente superan los 256 MB"}, {"Project exceeds 1024 frames", "El proyecto supera los 1024 frames"},
        {"Project exceeds 256 MB", "El proyecto supera los 256 MB"}, {"Project exceeds animation or warning limits", "El proyecto supera el límite de animaciones o avisos"},
        {"Project exceeds name, animation or warning limits", "El proyecto supera el límite de nombre, animaciones o avisos"},
        {"Import exceeds 1024 frames", "La importación supera los 1024 frames"}, {"Imported frames exceed 256 MB", "Los frames importados superan los 256 MB"},
        {"No PNG frames found", "No se encontraron frames PNG"}, {"No supported frames found", "No se encontraron frames compatibles"},
        {"Build a valid atlas with animations first", "Primero construye un atlas válido con animaciones"},
        {"Animate export is limited to 8 pages; increase page size", "Animate admite hasta 8 páginas; aumenta el tamaño de página"},
        {"Importing images", "Importando imágenes"}, {"Import complete", "Importación terminada"},
        {"Trimming and deduplicating", "Recortando y reutilizando imágenes"}, {"Packing pages", "Empaquetando páginas"},
        {"Resolving character poses", "Resolviendo poses del personaje"}, {"Resolving stage asset poses", "Resolviendo poses de la capa"},
        {"Resolving Animate poses", "Resolviendo poses de Animate"}, {"Extracting source regions", "Extrayendo áreas de la imagen fuente"},
        {"Encoding and verifying exported pages", "Codificando y verificando las páginas exportadas"},
        {"Saving project frames", "Guardando los frames del proyecto"}, {"Loading project frames", "Cargando los frames del proyecto"},
        {"Frame lies outside PNG: ", "El frame queda fuera del PNG: "}, {"A selected region lies outside the source image", "Una región seleccionada queda fuera de la imagen fuente"},
        {"Grid exceeds the remaining frame count, or cells are larger than the image", "La cuadrícula supera los frames disponibles o las celdas son mayores que la imagen"},
        {"Folder exceeds 4096 entries; choose individual images", "La carpeta supera las 4096 entradas; elige imágenes individuales"},
        {"Unsupported input: ", "Entrada no compatible: "}, {"Atlas imagePath must stay inside its source folder", "imagePath debe permanecer dentro de la carpeta del atlas"}
    };
    for (const auto& entry : messages) if (message.rfind(entry.first, 0) == 0) return std::string(entry.second) + message.substr(std::strlen(entry.first));
    const std::string imported = " frames imported";
    if (message.size() >= imported.size() && message.compare(message.size() - imported.size(), imported.size(), imported) == 0)
        return message.substr(0, message.size() - imported.size()) + " frames importados";
    return message;
}
inline void drawBuilderConfirm(AtlasBuilderState& state, SDL_Window* window, bool es) {
    if (state.pending != BuilderPending::None && !ImGui::IsPopupOpen("##builder-unsaved")) ImGui::OpenPopup("##builder-unsaved");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const float dialogWidth = std::min(520.0f, std::max(280.0f, viewport->Size.x - 40.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(dialogWidth, 0), ImVec2(dialogWidth, FLT_MAX));
    if (ImGui::BeginPopupModal("##builder-unsaved", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(es ? "Hay trabajo sin guardar" : "Unsaved work");
        ImGui::TextWrapped("%s", es ? "Guarda el proyecto, descarta los cambios o vuelve a editar." : "Save the project, discard changes, or return to editing.");
        const bool busy = state.task.active() || state.dialogBusy;
        if (busy) ImGui::TextDisabled("%s", es ? "Esperando a que termine la operación actual..." : "Waiting for the current operation...");
        if (!state.failure.empty()) ImGui::TextWrapped("%s", builderMessage(state.failure, es).c_str());
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(es ? "Guardar y continuar" : "Save and continue")) {
            if (state.projectPath.empty()) builderDialog(state, window, BuilderDialog::Save); else builderSave(state, state.projectPath, false);
        }
        ImGui::SameLine();
        if (ImGui::Button(es ? "Descartar" : "Discard")) {
            builderClearRecovery(state); builderCompletePending(state); ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button(es ? "Cancelar" : "Cancel")) { state.pending = BuilderPending::None; state.pendingPath.clear(); ImGui::CloseCurrentPopup(); }
        if (state.pending == BuilderPending::None) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
inline void builderCanvasTexture(AssetCanvasView& view, const char* id, ImVec2 size, const fml::AtlasPixels* image,
    unsigned int& texture, const fml::AtlasPixels*& uploaded, int background, std::function<void(ImVec2, float)> overlays = {}) {
    if (image && image != uploaded) { uploadAssetPixels(texture, image->w, image->h, image->rgba.data(), false); uploaded = image; }
    float scale = 1;
    const auto origin = beginAssetCanvas(view, id, size, image ? image->w : 256, image ? image->h : 256, background, scale);
    if (image) ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture), origin, ImVec2(origin.x + image->w * scale, origin.y + image->h * scale));
    if (overlays) overlays(origin, scale);
    endAssetCanvas();
}
inline void drawAtlasBuilder(AtlasBuilderState& state, SDL_Window* window, bool es) {
    const bool busy = state.task.active() || state.dialogBusy;
    state.poseView.boundsValid = state.sheetView.boundsValid = state.sourceView.boundsValid = false;
    if (!state.name[0]) builderSyncFields(state);
    ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.50f, 1), "%s", es ? "CREADOR DE ATLAS" : "TEXTURE ATLAS BUILDER");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.dirty ? (es ? "Sin guardar" : "Unsaved changes") : state.projectPath.empty() ? (es ? "Proyecto nuevo" : "New project") : (es ? "Guardado" : "Saved"));
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(es ? "Importar frames" : "Import frames")) builderDialog(state, window, BuilderDialog::Images);
    builderSameLine(130);
    if (ImGui::Button(es ? "Importar carpeta" : "Import folder")) builderDialog(state, window, BuilderDialog::Folder);
    builderSameLine(110);
    if (ImGui::Button(es ? "Abrir proyecto" : "Open project")) builderDialog(state, window, BuilderDialog::Open);
    builderSameLine(110);
    if (ImGui::Button(es ? "Guardar proyecto" : "Save project")) {
        if (state.projectPath.empty()) builderDialog(state, window, BuilderDialog::Save); else builderSave(state, state.projectPath, false);
    }
    builderSameLine(110);
    if (ImGui::Button(es ? "Guardar como..." : "Save as...")) builderDialog(state, window, BuilderDialog::Save);
    builderSameLine(70);
    if (ImGui::Button(es ? "Nuevo" : "New")) builderRequest(state, BuilderPending::New);
    builderSameLine(75);
    ImGui::BeginDisabled(!state.history.canUndo());
    if (ImGui::Button(es ? "Deshacer" : "Undo")) builderUndo(state);
    ImGui::EndDisabled(); builderSameLine(75);
    ImGui::BeginDisabled(!state.history.canRedo());
    if (ImGui::Button(es ? "Rehacer" : "Redo")) builderUndo(state, true);
    ImGui::EndDisabled();
    builderSameLine(95);
    if (ImGui::Button(es ? "Construir atlas" : "Build atlas")) builderBuild(state);
    builderSameLine(130);
    ImGui::BeginDisabled(!state.built || !state.built->ok() || state.builtRevision != state.revision);
    if (ImGui::Button(state.animateExport ? (es ? "Exportar Animate..." : "Export Animate...") : (es ? "Exportar Sparrow..." : "Export Sparrow..."))) builderDialog(state, window, BuilderDialog::Export);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!busy && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) builderUndo(state, ImGui::GetIO().KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) builderUndo(state, true);
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (state.projectPath.empty()) builderDialog(state, window, BuilderDialog::Save); else builderSave(state, state.projectPath, false); }
    }
    if (state.recoveryAvailable && state.project.frames.empty() && !busy) {
        if (ImGui::Button(es ? "Recuperar último trabajo" : "Recover last workspace")) builderOpen(state, state.recoveryPath);
        builderSameLine(140); ImGui::TextDisabled("%s", es ? "Copia de recuperación local disponible." : "Local recovery copy available.");
    }
    if (state.task.active()) {
        const auto progress = state.task.progress;
        ImGui::TextUnformatted(builderMessage(progress->description(), es).c_str());
        ImGui::ProgressBar(progress->total ? static_cast<float>(progress->completed) / progress->total : 0.0f, ImVec2(-125, 0));
        ImGui::SameLine();
        if (ImGui::Button(es ? "Cancelar tarea" : "Cancel task")) state.task.cancel();
    }
    if (!state.status.empty()) ImGui::TextWrapped("%s", builderMessage(state.status, es).c_str());
    if (!state.project.sourceWarnings.empty() && ImGui::CollapsingHeader(es ? "Avisos de importación · revisa lo omitido" : "Import warnings · review omitted content")) {
        for (const auto& warning : state.project.sourceWarnings) ImGui::TextWrapped("%s", warning.c_str());
    }
    const float width = ImGui::GetContentRegionAvail().x;
    state.panelWidth = std::clamp(state.panelWidth, 210.0f, std::max(210.0f, width * 0.55f));
    const float left = state.panelWidth;
    ImGui::BeginChild("##builder-settings", ImVec2(left, 0), true);
    ImGui::BeginDisabled(busy);
    ImGui::TextDisabled("%s", es ? "PROYECTO" : "PROJECT");
    ImGui::SetNextItemWidth(-1);
    const bool renamed = ImGui::InputText("##builder-name", state.name.data(), state.name.size());
    if (renamed) state.project.name = state.name.data(); builderWidgetChanged(state, renamed);
    if (!state.projectPath.empty()) { ImGui::TextWrapped("%s", state.projectPath.filename().u8string().c_str()); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", state.projectPath.u8string().c_str()); }
    if (ImGui::Checkbox(es ? "PNG como imagen fuente" : "PNG as source image", &state.rawImages)) state.status.clear();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", es ? "No separar automáticamente el PNG usando su XML/TXT vecino. Ideal para recortar una hoja propia." : "Do not split PNGs using nearby XML/TXT metadata. Use this to slice your own source sheet.");
    ImGui::TextDisabled("%s", es ? "ANIMACIONES" : "ANIMATIONS");
    ImGui::BeginChild("##builder-animation-list", ImVec2(0, 135), true);
    for (size_t i = 0; i < state.project.animations.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(state.project.animations[i].name.c_str(), state.selectedAnimation == static_cast<int>(i))) {
            builderSelectAnimation(state, static_cast<int>(i));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(state.project.animations.size() >= 256);
    if (ImGui::Button(es ? "+ Animación" : "+ Animation")) {
        std::string name = "animation-" + std::to_string(state.project.animations.size() + 1);
        while (std::any_of(state.project.animations.begin(), state.project.animations.end(), [&](const fml::TextureAnimation& animation) { return animation.name == name; })) name += "_";
        state.project.animations.push_back({name, {}}); state.selectedAnimation = static_cast<int>(state.project.animations.size()) - 1;
        builderChanged(state); builderSyncFields(state);
    }
    ImGui::EndDisabled();
    builderSameLine(65);
    if (ImGui::Button(es ? "Borrar" : "Delete") && state.selectedAnimation >= 0 && state.selectedAnimation < static_cast<int>(state.project.animations.size())) {
        state.project.animations.erase(state.project.animations.begin() + state.selectedAnimation); state.selectedAnimation = 0;
        builderChanged(state); builderSyncFields(state);
    }
    if (state.selectedAnimation >= 0 && state.selectedAnimation < static_cast<int>(state.project.animations.size())) {
        auto& animation = state.project.animations[state.selectedAnimation];
        ImGui::SetNextItemWidth(-1);
        const bool animationRenamed = ImGui::InputText("##builder-animation-name", state.animationName.data(), state.animationName.size());
        if (animationRenamed) animation.name = state.animationName.data(); builderWidgetChanged(state, animationRenamed);
        ImGui::TextDisabled("FPS");
        ImGui::SetNextItemWidth(-1);
        builderWidgetChanged(state, ImGui::SliderInt("##builder-fps", &animation.fps, 1, 240));
        builderWidgetChanged(state, ImGui::Checkbox(es ? "Repetir animación" : "Loop animation", &animation.loop));
        ImGui::TextDisabled("%s", es ? "Offset de animación (X / Y)" : "Animation offset (X / Y)");
        ImGui::SetNextItemWidth((ImGui::GetContentRegionAvail().x - 9) * 0.5f);
        bool changed = ImGui::InputInt("##builder-offset-x", &animation.offsetX, 0);
        animation.offsetX = std::clamp(animation.offsetX, -16384, 16384); builderWidgetChanged(state, changed);
        ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
        changed = ImGui::InputInt("##builder-offset-y", &animation.offsetY, 0);
        animation.offsetY = std::clamp(animation.offsetY, -16384, 16384); builderWidgetChanged(state, changed);
        ImGui::TextDisabled("%s", es ? "Orden de frames · números desde 1" : "Frame order · numbers start at 1");
        ImGui::InputTextMultiline("##builder-order", state.order.data(), state.order.size(), ImVec2(-1, 65));
        if (ImGui::Button(es ? "Aplicar orden" : "Apply order")) builderApplyOrder(state);
        builderSameLine(75);
        if (ImGui::Button(es ? "Todos" : "All frames")) {
            animation.frames.resize(state.project.frames.size()); std::iota(animation.frames.begin(), animation.frames.end(), size_t{0});
            builderChanged(state); builderSyncFields(state);
        }
        if (!state.orderError.empty()) ImGui::TextWrapped("%s", builderMessage(state.orderError, es).c_str());
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", es ? "EMPAQUETADO" : "PACKING");
    auto& options = state.project.options;
    const char* edges[] = {"256", "512", "1024", "2048", "4096", "8192"};
    const int values[] = {256, 512, 1024, 2048, 4096, 8192};
    int edge = 3; for (int i = 0; i < 6; ++i) if (values[i] == options.maxSide) edge = i;
    ImGui::TextDisabled("%s", es ? "Tamaño máximo de página" : "Maximum page size"); ImGui::SetNextItemWidth(-1);
    const bool sizeChanged = ImGui::Combo("##builder-size", &edge, edges, 6);
    if (sizeChanged) options.maxSide = values[edge]; builderWidgetChanged(state, sizeChanged);
    ImGui::TextDisabled("%s", es ? "Margen / páginas máximas" : "Padding / maximum pages");
    ImGui::SetNextItemWidth((ImGui::GetContentRegionAvail().x - 9) * 0.5f);
    bool changed = ImGui::InputInt("##builder-padding", &options.padding, 0);
    options.padding = std::clamp(options.padding, 0, 32); builderWidgetChanged(state, changed);
    ImGui::SameLine(); ImGui::SetNextItemWidth(-1); changed = ImGui::InputInt("##builder-pages", &options.maxPages, 0);
    options.maxPages = std::clamp(options.maxPages, 1, state.animateExport ? 8 : 32); builderWidgetChanged(state, changed);
    builderWidgetChanged(state, ImGui::Checkbox(es ? "Recortar transparencia" : "Trim transparency", &options.trim));
    builderWidgetChanged(state, ImGui::Checkbox(es ? "Compartir frames idénticos" : "Share identical frames", &options.deduplicate));
    builderWidgetChanged(state, ImGui::Checkbox(es ? "Potencia de dos" : "Power of two", &options.powerOfTwo));
    ImGui::Separator();
    const char* formats[] = {"Sparrow · PNG / XML", "Animate · PNG / JSON"};
    int format = state.animateExport ? 1 : 0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##builder-export-format", &format, formats, 2)) {
        state.project.animate = state.animateExport = format != 0;
        if (state.animateExport && options.maxPages > 8) options.maxPages = 8;
        builderChanged(state);
    }
    if (state.animateExport) ImGui::TextDisabled("%s", es ? "Animate: máximo 8 páginas" : "Animate: up to 8 pages");
    ImGui::BeginDisabled(!state.built || !state.built->ok() || state.builtRevision != state.revision);
    if (ImGui::Button(es ? "Exportar atlas..." : "Export atlas...", ImVec2(-1, 0))) builderDialog(state, window, BuilderDialog::Export);
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", es ? "Solo texturas y metadata. No genera scripts ni una definición de personaje." : "Textures and metadata only. No scripts or character definition generated.");
    if (ImGui::CollapsingHeader(es ? "Revisión de exportación" : "Export review")) {
        ImGui::Text("%zu %s · %zu %s", state.project.frames.size(), es ? "frames fuente" : "source frames", state.project.animations.size(), es ? "animaciones" : "animations");
        if (state.built && state.builtRevision == state.revision && state.built->ok()) {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.65f, 1), "%s", es ? "Atlas actualizado" : "Atlas is up to date");
            for (const auto& page : state.built->pages) ImGui::BulletText("%s · %d × %d", page.name.c_str(), page.pixels.w, page.pixels.h);
        } else ImGui::TextWrapped("%s", es ? "Construye el atlas antes de exportar." : "Build the atlas before exporting.");
        if (state.animateExport && state.project.animations.empty()) ImGui::TextWrapped("%s", es ? "Animate necesita al menos una animación." : "Animate needs at least one animation.");
        if (!state.animateExport) ImGui::TextWrapped("%s", es ? "Para varias páginas, el motor necesita un cargador multi-atlas. Los offsets del manifiesto conservan el anclaje." : "Multiple pages need a multi-atlas engine loader. Manifest offsets preserve the anchor.");
        if (!state.project.sourceWarnings.empty()) ImGui::TextWrapped("%s", es ? "Hay avisos de recursos omitidos. Revísalos antes de exportar." : "Some resources were omitted. Review the source warnings before exporting.");
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::InvisibleButton("##builder-splitter", ImVec2(6, ImGui::GetContentRegionAvail().y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) state.panelWidth = std::clamp(state.panelWidth + ImGui::GetIO().MouseDelta.x, 210.0f, std::max(210.0f, width * 0.55f));
    ImGui::SameLine();
    ImGui::BeginChild("##builder-workspace", ImVec2(0, 0), false);
    ImGui::TextDisabled("%s", es ? "VISTA PREVIA · clic central: pan · rueda: zoom" : "PREVIEW · middle drag: pan · wheel: zoom");
    if (ImGui::Button(state.playing ? (es ? "Pausar" : "Pause") : (es ? "Reproducir" : "Play"))) {
        if (state.playing) {
            int current = -1; state.selectedFrame = builderCurrentFrame(state, 0, current); state.selectedStep = current; state.playing = false;
        } else builderResume(state);
    }
    ImGui::SameLine();
    if (ImGui::Button(es ? "Restablecer vista" : "Reset view")) { state.poseView = {}; state.sheetView = {}; state.sourceView = {}; }
    ImGui::SameLine(); ImGui::Checkbox(es ? "Límites" : "Bounds", &state.showBounds);
    builderSameLine(140); ImGui::Checkbox(es ? "Ver hoja" : "Show sheet", &state.showSheet);
    const char* backgrounds[] = {es ? "Transparencia" : "Transparency", es ? "Oscuro" : "Dark", es ? "Claro" : "Light"};
    builderSameLine(155); ImGui::SetNextItemWidth(145); ImGui::Combo("##builder-background", &state.background, backgrounds, 3);
    const fml::TextureAnimation* animation = state.selectedAnimation >= 0 && state.selectedAnimation < static_cast<int>(state.project.animations.size()) ? &state.project.animations[state.selectedAnimation] : nullptr;
    int step = -1;
    const int source = builderCurrentFrame(state, ImGui::GetIO().DeltaTime, step);
    if (animation && step >= 0) {
        ImGui::Text("%s: %s · %d / %zu", es ? "Animación" : "Animation", animation->name.c_str(), step + 1, animation->frames.size());
    } else ImGui::TextDisabled("%s", source >= 0 ? (es ? "Frame seleccionado" : "Selected frame") : (es ? "Importa frames y crea una animación." : "Import frames and create an animation."));
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float canvasHeight = std::clamp(state.previewHeight, 160.0f, std::max(160.0f, ImGui::GetContentRegionAvail().y - 200));
    const bool dual = state.showSheet && contentWidth > 640;
    if (state.showSheet && !dual) {
        if (ImGui::RadioButton(es ? "Animación" : "Animation", !state.narrowSheet)) state.narrowSheet = false;
        ImGui::SameLine(); if (ImGui::RadioButton(es ? "Hoja empaquetada" : "Packed sheet", state.narrowSheet)) state.narrowSheet = true;
    }
    const float canvasWidth = dual ? (contentWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f : contentWidth;
    const fml::TextureFrame* frame = source >= 0 && source < static_cast<int>(state.project.frames.size()) ? &state.project.frames[source] : nullptr;
    int minX = 0, minY = 0, maxX = 1, maxY = 1;
    auto includeFrame = [&](const fml::TextureFrame& candidate) {
        const int x = candidate.x - (animation ? animation->offsetX : 0), y = candidate.y - (animation ? animation->offsetY : 0);
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x + candidate.pixels->w); maxY = std::max(maxY, y + candidate.pixels->h);
    };
    if (animation && step >= 0) for (size_t index : animation->frames) includeFrame(state.project.frames[index]);
    else if (frame) includeFrame(*frame);
    float scale = 1;
    if (dual || !state.showSheet || !state.narrowSheet) {
    const auto origin = beginAssetCanvas(state.poseView, "##builder-pose", ImVec2(canvasWidth, canvasHeight), maxX - minX, maxY - minY, state.background, scale);
    if (frame) {
        if (state.uploadedPose != frame->pixels.get()) { uploadAssetPixels(state.poseTexture, frame->pixels->w, frame->pixels->h, frame->pixels->rgba.data(), false); state.uploadedPose = frame->pixels.get(); }
        const int offsetX = animation ? animation->offsetX : 0, offsetY = animation ? animation->offsetY : 0;
        const ImVec2 top(origin.x + (frame->x - minX - offsetX) * scale, origin.y + (frame->y - minY - offsetY) * scale);
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(state.poseTexture), top, ImVec2(top.x + frame->pixels->w * scale, top.y + frame->pixels->h * scale));
        const ImVec2 anchor(origin.x - minX * scale, origin.y - minY * scale);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(anchor.x - 7, anchor.y), ImVec2(anchor.x + 7, anchor.y), IM_COL32(255, 194, 99, 255));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(anchor.x, anchor.y - 7), ImVec2(anchor.x, anchor.y + 7), IM_COL32(255, 194, 99, 255));
    }
    endAssetCanvas();
    }
    if (dual) ImGui::SameLine();
    const fml::TexturePage* page = state.built && state.page >= 0 && state.page < static_cast<int>(state.built->pages.size()) ? &state.built->pages[state.page] : nullptr;
    const fml::AtlasPixels* uploadedSheet = state.uploadedBuild == state.built.get() && state.uploadedPage == state.page && page ? &page->pixels : nullptr;
    if (state.showSheet && (dual || state.narrowSheet)) builderCanvasTexture(state.sheetView, "##builder-sheet", ImVec2(dual ? canvasWidth : -1, canvasHeight), page ? &page->pixels : nullptr, state.sheetTexture, uploadedSheet, state.background,
        [&](ImVec2 at, float zoom) {
            if (!page || !state.built) return;
            for (const auto& placement : state.built->frames) if (placement.page == static_cast<size_t>(state.page)) {
                const bool active = placement.source == static_cast<size_t>(std::max(0, source));
                if (!active && !state.showBounds) continue;
                const ImVec2 top(at.x + placement.x * zoom, at.y + placement.y * zoom), bottom(top.x + placement.w * zoom, top.y + placement.h * zoom);
                ImGui::GetWindowDrawList()->AddRect(top, bottom, active ? IM_COL32(92, 232, 162, 255) : IM_COL32(184, 155, 102, 130), 0, 0, active ? 2.5f : 1.0f);
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(top, bottom)) {
                    builderSelectFrame(state, static_cast<int>(placement.source));
                    break;
                }
            }
        });
    if (page && state.showSheet && (dual || state.narrowSheet)) { state.uploadedBuild = state.built.get(); state.uploadedPage = state.page; }
    ImGui::InvisibleButton("##builder-height", ImVec2(-1, 6));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) state.previewHeight = std::clamp(state.previewHeight + ImGui::GetIO().MouseDelta.y, 160.0f, 800.0f);
    if (state.built) {
        ImGui::Text("%zu %s · %zu %s · %.1f MB", state.built->frames.size(), es ? "frames" : "frames", state.built->pages.size(), es ? "páginas" : "pages", state.built->outputBytes / (1024.0 * 1024.0));
        ImGui::SameLine(); ImGui::TextDisabled("%zu %s", state.built->uniqueImages, es ? "imágenes únicas" : "unique images");
        if (state.builtRevision != state.revision) ImGui::TextColored(ImVec4(1, 0.72f, 0.35f, 1), "%s", es ? "Atlas desactualizado: construye de nuevo para exportar." : "Atlas is outdated: build again before exporting.");
        if (state.built->pages.size() > 1) {
            ImGui::SetNextItemWidth(180);
            ImGui::SliderInt(es ? "Página" : "Page", &state.page, 0, static_cast<int>(state.built->pages.size()) - 1);
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(busy);
    if (animation && !animation->frames.empty()) {
        ImGui::TextDisabled("%s", es ? "SECUENCIA · clic para elegir · arrastra para reordenar" : "TIMELINE · click to select · drag to reorder");
        ImGui::BeginChild("##builder-timeline", ImVec2(0, 82), true, ImGuiWindowFlags_HorizontalScrollbar);
        const float pitch = 78;
        const int first = std::max(0, static_cast<int>(ImGui::GetScrollX() / pitch) - 1);
        const int last = std::min(static_cast<int>(animation->frames.size()), first + static_cast<int>(ImGui::GetContentRegionAvail().x / pitch) + 3);
        int moveFrom = -1, moveTo = -1;
        for (int i = first; i < last; ++i) {
            ImGui::SetCursorPos(ImVec2(8 + i * pitch, 8)); ImGui::PushID(i);
            const auto& entry = state.project.frames[animation->frames[i]];
            const std::string label = std::to_string(i + 1) + "\n#" + std::to_string(animation->frames[i] + 1);
            if (i == step) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.42f, 0.32f, 1));
            if (ImGui::Button(label.c_str(), ImVec2(70, 44))) builderSelectStep(state, i);
            if (i == step) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%d × %d", entry.name.c_str(), entry.pixels->w, entry.pixels->h);
            if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("FML_ATLAS_STEP", &i, sizeof(i)); ImGui::Text("%s", entry.name.c_str()); ImGui::EndDragDropSource(); }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("FML_ATLAS_STEP")) if (payload->DataSize == sizeof(int)) { moveFrom = *static_cast<const int*>(payload->Data); moveTo = i; }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
        ImGui::SetCursorPos(ImVec2(8 + animation->frames.size() * pitch, 56)); ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        if (moveFrom >= 0) builderMoveStepTo(state, moveFrom, moveTo);
    }
    ImGui::TextDisabled("%s", es ? "BANCO DE FRAMES · selecciona y añade a la animación" : "FRAME BANK · select and append to animation");
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##builder-frame-search", es ? "Buscar frame..." : "Search frames...", state.frameSearch.data(), state.frameSearch.size());
    std::string filter = state.frameSearch.data(); std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    int removeFrame = -1, duplicateFrame = -1;
    ImGui::BeginChild("##builder-bank", ImVec2(0, std::clamp(ImGui::GetContentRegionAvail().y - 190, 95.0f, 240.0f)), true);
    for (size_t i = 0; i < state.project.frames.size(); ++i) {
        const auto& entry = state.project.frames[i];
        const std::string label = std::to_string(i + 1) + " · " + entry.name;
        std::string searchable = label; std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!filter.empty() && searchable.find(filter) == std::string::npos) continue;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(label.c_str(), state.selectedFrame == static_cast<int>(i))) {
            builderSelectFrame(state, static_cast<int>(i));
        }
        if (ImGui::BeginPopupContextItem("##builder-frame-context")) {
            if (ImGui::MenuItem(es ? "Añadir a animación" : "Append to animation", nullptr, false, animation != nullptr)) {
                state.project.animations[state.selectedAnimation].frames.push_back(i); builderChanged(state); builderSyncFields(state);
            }
            if (ImGui::MenuItem(es ? "Duplicar frame" : "Duplicate frame", nullptr, false, state.project.frames.size() < 1024)) duplicateFrame = static_cast<int>(i);
            if (ImGui::MenuItem(es ? "Eliminar del proyecto" : "Delete from project")) removeFrame = static_cast<int>(i);
            ImGui::TextDisabled("%d × %d", entry.pixels->w, entry.pixels->h);
            ImGui::TextWrapped("%s", entry.source.c_str()); ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (state.selectedFrame >= 0 && state.selectedFrame < static_cast<int>(state.project.frames.size())) {
        auto& entry = state.project.frames[state.selectedFrame];
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x));
        const bool changedName = ImGui::InputText(es ? "Nombre de frame" : "Frame name", state.frameName.data(), state.frameName.size());
        if (changedName) entry.name = state.frameName.data(); builderWidgetChanged(state, changedName);
        if (ImGui::CollapsingHeader(es ? "Anclaje y origen del frame" : "Frame anchor and source")) {
            int position[2]{entry.x, entry.y}; ImGui::SetNextItemWidth(160);
            const bool moved = ImGui::InputInt2(es ? "Anclaje X / Y" : "Anchor X / Y", position);
            if (moved) { entry.x = std::clamp(position[0], -16384, 16384); entry.y = std::clamp(position[1], -16384, 16384); }
            builderWidgetChanged(state, moved);
            ImGui::Text("%d × %d", entry.pixels->w, entry.pixels->h); ImGui::TextWrapped("%s", entry.source.c_str());
        }
    }
    ImGui::BeginDisabled(!animation || state.selectedFrame < 0);
    if (ImGui::Button(es ? "Añadir frame seleccionado" : "Append selected frame")) {
        state.project.animations[state.selectedAnimation].frames.push_back(static_cast<size_t>(state.selectedFrame)); builderChanged(state); builderSyncFields(state);
    }
    ImGui::EndDisabled(); builderSameLine(145);
    if (ImGui::Button(es ? "Quitar paso actual" : "Remove current step") && animation && step >= 0 && step < static_cast<int>(animation->frames.size())) {
        state.project.animations[state.selectedAnimation].frames.erase(state.project.animations[state.selectedAnimation].frames.begin() + step);
        builderChanged(state); builderSyncFields(state);
    }
    builderSameLine(80);
    if (ImGui::Button(es ? "Paso antes" : "Move earlier")) builderMoveStep(state, step, -1);
    builderSameLine(80);
    if (ImGui::Button(es ? "Paso después" : "Move later")) builderMoveStep(state, step, 1);
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", es ? "Arrastra PNG, XML/TXT o carpetas aquí. Los originales no se modifican." : "Drag PNG, XML/TXT or folders here. Original files are never modified.");
    if (ImGui::CollapsingHeader(es ? "Recortar imagen fuente · cuadrícula / área manual" : "Slice source image · grid / manual area")) {
        ImGui::BeginDisabled(busy);
        ImGui::TextWrapped("%s", es ? "Selecciona una imagen del banco. Clic: elegir celda. Área manual: arrastrar con botón izquierdo. Pan y zoom no cambian el recorte." :
            "Select a frame-bank image. Click: pick a cell. Manual area: left-drag a region. Pan and zoom do not change the crop.");
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputInt2(es ? "Celda X / Y" : "Cell X / Y", state.cell)) { state.cell[0] = std::clamp(state.cell[0], 1, 8192); state.cell[1] = std::clamp(state.cell[1], 1, 8192); }
        ImGui::SameLine(); ImGui::Checkbox(es ? "Área manual" : "Manual area", &state.trace);
        ImGui::SetNextItemWidth(std::max(90.0f, std::min(450.0f, ImGui::GetContentRegionAvail().x - 130)));
        ImGui::InputInt4(es ? "X / Y / ancho / alto" : "X / Y / width / height", state.region);
        const auto* sourceImage = state.sourceFrame >= 0 && state.sourceFrame < static_cast<int>(state.project.frames.size()) ? state.project.frames[state.sourceFrame].pixels.get() : nullptr;
        builderCanvasTexture(state.sourceView, "##builder-source", ImVec2(-1, 285), sourceImage, state.sourceTexture, state.uploadedSource, state.background,
            [&](ImVec2 at, float zoom) {
                if (!sourceImage) return;
                auto* draw = ImGui::GetWindowDrawList();
                const int firstX = std::max(0, static_cast<int>((state.sourceView.min.x - at.x) / zoom) / state.cell[0] * state.cell[0]);
                const int firstY = std::max(0, static_cast<int>((state.sourceView.min.y - at.y) / zoom) / state.cell[1] * state.cell[1]);
                const int lastX = std::min(sourceImage->w, static_cast<int>((state.sourceView.max.x - at.x) / zoom) + state.cell[0]);
                const int lastY = std::min(sourceImage->h, static_cast<int>((state.sourceView.max.y - at.y) / zoom) + state.cell[1]);
                if (state.cell[0] * zoom >= 4) for (int x = firstX; x <= lastX; x += state.cell[0]) draw->AddLine(ImVec2(at.x + x * zoom, at.y), ImVec2(at.x + x * zoom, at.y + sourceImage->h * zoom), IM_COL32(178, 162, 120, 85));
                if (state.cell[1] * zoom >= 4) for (int y = firstY; y <= lastY; y += state.cell[1]) draw->AddLine(ImVec2(at.x, at.y + y * zoom), ImVec2(at.x + sourceImage->w * zoom, at.y + y * zoom), IM_COL32(178, 162, 120, 85));
                const auto mouse = ImGui::GetIO().MousePos;
                const int x = std::clamp(static_cast<int>(std::floor((mouse.x - at.x) / zoom)), 0, sourceImage->w - 1);
                const int y = std::clamp(static_cast<int>(std::floor((mouse.y - at.y) / zoom)), 0, sourceImage->h - 1);
                const bool inside = mouse.x >= at.x && mouse.y >= at.y && mouse.x < at.x + sourceImage->w * zoom && mouse.y < at.y + sourceImage->h * zoom;
                if (!busy && inside && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (state.trace) { state.traceStart = ImVec2(static_cast<float>(x), static_cast<float>(y)); state.tracing = true; }
                    else {
                        state.region[0] = x / state.cell[0] * state.cell[0]; state.region[1] = y / state.cell[1] * state.cell[1];
                        state.region[2] = std::min(state.cell[0], sourceImage->w - state.region[0]); state.region[3] = std::min(state.cell[1], sourceImage->h - state.region[1]);
                    }
                }
                if (state.tracing) {
                    state.region[0] = std::min(x, static_cast<int>(state.traceStart.x)); state.region[1] = std::min(y, static_cast<int>(state.traceStart.y));
                    state.region[2] = std::abs(x - static_cast<int>(state.traceStart.x)) + 1; state.region[3] = std::abs(y - static_cast<int>(state.traceStart.y)) + 1;
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) state.tracing = false;
                }
                draw->AddRect(ImVec2(at.x + state.region[0] * zoom, at.y + state.region[1] * zoom),
                    ImVec2(at.x + (state.region[0] + state.region[2]) * zoom, at.y + (state.region[1] + state.region[3]) * zoom), IM_COL32(94, 212, 255, 255), 0.0f, ImDrawFlags_None, 2.0f);
            });
        if (ImGui::Button(es ? "Añadir recorte al proyecto" : "Append selected crop")) builderExtract(state, false);
        builderSameLine(155);
        if (ImGui::Button(es ? "Extraer cuadrícula completa" : "Extract full grid")) builderExtract(state, true);
        ImGui::TextDisabled("%s", es ? "La cuadrícula usa celdas completas, en orden de filas. El banco original se conserva." : "The grid uses full cells in row order. The original frame bank is kept.");
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    if (removeFrame >= 0) builderRemoveFrame(state, removeFrame);
    else if (duplicateFrame >= 0) {
        auto copy = state.project.frames[duplicateFrame]; copy.name += "-copy"; state.project.frames.push_back(std::move(copy));
        builderChanged(state); builderSyncFields(state); builderSelectFrame(state, static_cast<int>(state.project.frames.size()) - 1);
    }
}

}
