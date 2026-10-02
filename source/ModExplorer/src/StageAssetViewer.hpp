#pragma once

#include "../../src/fml_app/StageAsset.hpp"
#include "../../src/fml_app/MountedSheet.hpp"
#include "../../src/fml_app/AnimationSlice.hpp"
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/miniz/miniz.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <functional>
#include <fstream>
#include <cfloat>

namespace atlas_ui {

struct AssetCanvasView {
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
    bool boundsValid = false;
    SDL_WindowID window = 0;
    ImVec2 min{0.0f, 0.0f}, max{0.0f, 0.0f}, wheelPosition{0.0f, 0.0f};
    float wheel = 0.0f;
};

struct AssetScriptReference {
    std::string path;
    bool global = false;
    bool possibleReference = false;
};

struct StageAssetViewerState {
    bool open = false;
    bool appearing = false;
    bool playing = true;
    bool loop = true;
    bool resolvedSheet = false;
    bool showBounds = true;
    bool selectSheetTab = false;
    int animation = 0;
    int frame = 0;
    int uploadedFrame = -1;
    int page = 0;
    int loadedPage = -1;
    int selectedRectangle = -1;
    int rangeFirst = 0, rangeLast = 0;
    bool useRange = false;
    std::vector<bool> selectedAnimations;
    int appearance = 0;
    int background = 0;
    int captureInspectorScroll = 0;
    float speed = 1.0f;
    double clock = 0.0;
    std::string root, sourceKey, sourceLabel, stageId, definition, format;
    std::vector<std::pair<size_t, std::string>> users;
    std::vector<AssetScriptReference> scripts;
    fml::StageAssetResource resource;
    fml::GifPrepareResult prepared;
    fml::MountedSheet mounted;
    std::vector<fml::AtlasFrame> mountedRects;
    std::string sheetError;
    unsigned int poseTexture = 0;
    unsigned int sheetTexture = 0;
    int sheetWidth = 0, sheetHeight = 0;
    AssetCanvasView poseView, sheetView;

    void releaseTextures() {
        if (poseTexture) glDeleteTextures(1, &poseTexture);
        if (sheetTexture) glDeleteTextures(1, &sheetTexture);
        poseTexture = sheetTexture = 0;
        uploadedFrame = loadedPage = -1;
        poseView.boundsValid = sheetView.boundsValid = false;
    }

    void close() {
        releaseTextures();
        resource = {};
        prepared = {};
        mounted = {};
        mountedRects.clear();
        open = false;
    }

    void prepare(int initialFrame = 0) {
        releaseTextures();
        prepared = {};
        mounted = {};
        mountedRects.clear();
        sheetError.clear();
        selectedRectangle = -1;
        animation = std::clamp(animation, 0, std::max(0, static_cast<int>(resource.animations.size()) - 1));
        prepared = fml::prepareStageAssetAnimation(resource, animation);
        rangeFirst = 0;
        rangeLast = std::max(0, static_cast<int>(prepared.animation.frames.size()) - 1);
        if (selectedAnimations.size() != resource.animations.size()) selectedAnimations.assign(resource.animations.size(), false);
        frame = std::clamp(initialFrame, 0, std::max(0, static_cast<int>(prepared.animation.frames.size()) - 1));
        clock = static_cast<double>(frame) / std::max(1, prepared.animation.fps);
    }
};

enum class StageAssetExport { Png, Gif, Sheet, SourceZip, BlockPng, AnimationBatch };

inline void uploadAssetPixels(unsigned int& texture, int width, int height,
                              const std::uint8_t* pixels, bool smooth) {
    if (!texture) glGenTextures(1, &texture);
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previous));
}

inline bool assetFlipY(const fml::StageObject& object) {
    if (const auto found = object.properties.find("flipY"); found != object.properties.end()) return found->second.asBool();
    const auto found = object.unknownAttributes.find("flipY");
    return found != object.unknownAttributes.end() && (found->second == "true" || found->second == "1");
}

inline ImVec2 beginAssetCanvas(AssetCanvasView& view, const char* id, ImVec2 size,
                              int width, int height, int background, float& scale) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    view.min = ImGui::GetCursorScreenPos();
    view.max = ImVec2(view.min.x + available.x, view.min.y + available.y);
    view.boundsValid = available.x > 1 && available.y > 1;
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    view.window = static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(viewport->PlatformHandle));
    ImGui::InvisibleButton("##asset-surface", ImVec2(std::max(1.0f, available.x), std::max(1.0f, available.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const float fit = std::max(0.0001f, std::min(available.x / std::max(1, width), available.y / std::max(1, height)) * 0.90f);
    if (view.wheel != 0) {
        const float old = fit * view.zoom;
        const float next = std::clamp(view.zoom * std::pow(1.16f, std::clamp(view.wheel, -12.0f, 12.0f)), 0.02f, 64.0f);
        const float ratio = fit * next / old;
        const ImVec2 center(view.min.x + available.x * 0.5f, view.min.y + available.y * 0.5f);
        view.pan.x = (view.pan.x + center.x - view.wheelPosition.x) * ratio - center.x + view.wheelPosition.x;
        view.pan.y = (view.pan.y + center.y - view.wheelPosition.y) * ratio - center.y + view.wheelPosition.y;
        view.zoom = next;
        view.wheel = 0;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0)) {
        view.pan.x += ImGui::GetIO().MouseDelta.x;
        view.pan.y += ImGui::GetIO().MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    scale = fit * view.zoom;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(view.min, view.max, true);
    draw->AddRectFilled(view.min, view.max, background == 2 ? IM_COL32(230, 232, 235, 255) : IM_COL32(16, 19, 23, 255));
    if (background == 0) {
        for (float y = view.min.y; y < view.max.y; y += 24)
            for (float x = view.min.x; x < view.max.x; x += 24)
                if ((static_cast<int>((x - view.min.x) / 24) + static_cast<int>((y - view.min.y) / 24)) % 2 == 0)
                    draw->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + 24, view.max.x), std::min(y + 24, view.max.y)), IM_COL32(30, 34, 39, 255));
    }
    return ImVec2(view.min.x + (available.x - width * scale) * 0.5f + view.pan.x,
                  view.min.y + (available.y - height * scale) * 0.5f + view.pan.y);
}

inline void endAssetCanvas() {
    ImGui::GetWindowDrawList()->PopClipRect();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

inline void drawStageAssetPose(StageAssetViewerState& state, ImVec2 size, bool spanish) {
    const auto& image = state.prepared.animation;
    const int width = std::max(1, image.width), height = std::max(1, image.height);
    float scale = 1;
    const ImVec2 origin = beginAssetCanvas(state.poseView, "##asset-pose-canvas", size,
                                          width, height, state.background, scale);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (state.prepared.ok && !image.frames.empty()) {
        state.frame = std::clamp(state.frame, 0, static_cast<int>(image.frames.size()) - 1);
        if (state.uploadedFrame != state.frame) {
            uploadAssetPixels(state.poseTexture, width, height,
                image.frames[static_cast<size_t>(state.frame)].rgba.data(), state.resource.object.antialiasing);
            state.uploadedFrame = state.frame;
        }
        const fml::StageObject& object = state.resource.object;
        const float sx = state.appearance ? object.scale.x : 1.0f;
        const float sy = state.appearance ? object.scale.y : 1.0f;
        const float radians = state.appearance ? object.angle * 0.01745329252f : 0;
        const float cosine = std::cos(radians), sine = std::sin(radians);
        const ImVec2 center(origin.x + width * scale * 0.5f - (state.appearance ? object.frameOffset.x * scale : 0),
                            origin.y + height * scale * 0.5f - (state.appearance ? object.frameOffset.y * scale : 0));
        auto point = [&](float x, float y) {
            x *= sx * scale; y *= sy * scale;
            return ImVec2(center.x + x * cosine - y * sine, center.y + x * sine + y * cosine);
        };
        const bool flipX = state.appearance && object.flipX;
        const bool flipY = state.appearance && assetFlipY(object);
        const float left = flipX ? 1.0f : 0.0f, right = 1.0f - left;
        const float top = flipY ? 1.0f : 0.0f, bottom = 1.0f - top;
        const ImU32 tint = IM_COL32(255, 255, 255, state.appearance ? static_cast<int>(std::clamp(object.alpha, 0.0f, 1.0f) * 255) : 255);
        draw->AddImageQuad(ImTextureRef(static_cast<ImTextureID>(state.poseTexture)),
            point(-width * 0.5f, -height * 0.5f), point(width * 0.5f, -height * 0.5f),
            point(width * 0.5f, height * 0.5f), point(-width * 0.5f, height * 0.5f),
            ImVec2(left, top), ImVec2(right, top), ImVec2(right, bottom), ImVec2(left, bottom), tint);
    } else {
        draw->AddText(ImVec2(state.poseView.min.x + 14, state.poseView.min.y + 14), IM_COL32(241, 187, 104, 255),
                       spanish ? "No se pudo resolver la pose. Revisa los diagnósticos." : "Pose could not be resolved. Check diagnostics.");
    }
    endAssetCanvas();
}

inline void loadStageAssetSheet(StageAssetViewerState& state, const fml::Vfs& vfs) {
    const int key = state.resolvedSheet ? -2 : state.page;
    if (state.loadedPage == key) return;
    if (state.sheetTexture) glDeleteTextures(1, &state.sheetTexture);
    state.sheetTexture = 0;
    state.loadedPage = key;
    state.sheetError.clear();
    if (state.resolvedSheet) {
        if (state.mounted.png.frames.empty()) {
            if (state.animation < 0 || state.animation >= static_cast<int>(state.resource.animations.size()) ||
                !fml::packMountedSheet(state.prepared, state.resource.animations[static_cast<size_t>(state.animation)], state.mounted, state.sheetError)) return;
            fml::DiagnosticSink sink(fml::DiagnosticScope::WorkspaceInventory);
            auto atlas = fml::parseSparrowAtlas(state.mounted.xml, "mounted.xml", sink);
            if (atlas) state.mountedRects = std::move(atlas.value().frames);
        }
        state.sheetWidth = state.mounted.png.width;
        state.sheetHeight = state.mounted.png.height;
        uploadAssetPixels(state.sheetTexture, state.sheetWidth, state.sheetHeight,
                           state.mounted.png.frames.front().rgba.data(), false);
        return;
    }
    if (state.page < 0 || state.page >= static_cast<int>(state.resource.pages.size())) return;
    const auto& page = state.resource.pages[static_cast<size_t>(state.page)];
    auto bytes = state.resource.generated ? std::optional<std::vector<std::uint8_t>>(state.resource.encodedImage)
                                         : vfs.readBytes(page.imagePath, 128u * 1024u * 1024u);
    int width = 0, height = 0, channels = 0;
    if (!bytes || bytes->empty() || !stbi_info_from_memory(bytes->data(), static_cast<int>(bytes->size()), &width, &height, &channels) ||
        width <= 0 || height <= 0 || static_cast<size_t>(width) * height * 4 > 256u * 1024u * 1024u) {
        state.sheetError = "Source page is missing, invalid or exceeds the image-memory limit";
        return;
    }
    std::unique_ptr<stbi_uc, void(*)(void*)> pixels(stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &width, &height, &channels, 4), stbi_image_free);
    if (!pixels) { state.sheetError = "Source page could not be decoded"; return; }
    state.sheetWidth = width; state.sheetHeight = height;
    uploadAssetPixels(state.sheetTexture, width, height, pixels.get(), false);
}

inline void drawStageAssetSheet(StageAssetViewerState& state, const fml::Vfs& vfs, ImVec2 size) {
    loadStageAssetSheet(state, vfs);
    float scale = 1;
    const ImVec2 origin = beginAssetCanvas(state.sheetView, "##asset-sheet-canvas", size,
        state.sheetWidth, state.sheetHeight, state.background, scale);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (state.sheetTexture) {
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(state.sheetTexture)), origin,
                      ImVec2(origin.x + state.sheetWidth * scale, origin.y + state.sheetHeight * scale));
        const auto& rects = state.resolvedSheet ? state.mountedRects : state.resource.pages[static_cast<size_t>(state.page)].frames;
        std::vector<fml::AtlasFrame> active;
        if (state.resolvedSheet) {
            if (state.frame >= 0 && state.frame < static_cast<int>(rects.size())) active.push_back(rects[static_cast<size_t>(state.frame)]);
        } else active = fml::activeStageAssetRects(state.resource, state.animation, state.frame, static_cast<size_t>(state.page));
        const bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (size_t index = 0; index < rects.size(); ++index) {
            const auto& rectangle = rects[index];
            const ImVec2 min(origin.x + rectangle.x * scale, origin.y + rectangle.y * scale);
            const ImVec2 max(min.x + rectangle.w * scale, min.y + rectangle.h * scale);
            const bool playing = std::any_of(active.begin(), active.end(), [&](const fml::AtlasFrame& frame) { return frame.name == rectangle.name; });
            if (state.showBounds || playing || state.selectedRectangle == static_cast<int>(index))
                draw->AddRect(min, max, playing ? IM_COL32(90, 227, 164, 255) : state.selectedRectangle == static_cast<int>(index)
                    ? IM_COL32(246, 195, 99, 255) : IM_COL32(143, 154, 173, 100), 0, 0, playing ? 2.0f : 1.0f);
            if (!clicked || mouse.x < min.x || mouse.x >= max.x || mouse.y < min.y || mouse.y >= max.y) continue;
            state.selectedRectangle = static_cast<int>(index);
            int logical = -1;
            if (state.resolvedSheet) logical = static_cast<int>(index);
            else if (!state.resource.isAnimate && state.animation >= 0 && state.animation < static_cast<int>(state.resource.animations.size())) {
                const auto frames = fml::stageAssetFrames(state.resource, state.resource.animations[static_cast<size_t>(state.animation)]);
                const auto found = std::find(frames.begin(), frames.end(), index);
                if (found != frames.end()) logical = static_cast<int>(found - frames.begin());
            }
            if (logical >= 0 && logical < static_cast<int>(state.prepared.animation.frames.size())) {
                state.frame = logical;
                state.clock = static_cast<double>(logical) / std::max(1, state.prepared.animation.fps);
                state.playing = false;
            }
            break;
        }
    }
    endAssetCanvas();
}

inline bool exportStageAssetViewer(StageAssetViewerState& state, const fml::Vfs& vfs,
                                  StageAssetExport action, const std::filesystem::path& target, std::string& message) {
    namespace fs = std::filesystem;
    if (action == StageAssetExport::BlockPng) {
        if ((!state.resolvedSheet && state.resource.pages.empty()) || (state.resolvedSheet && state.mounted.png.frames.empty())) { message = "No sheet is available"; return false; }
        const auto& rects = state.resolvedSheet ? state.mountedRects : state.resource.pages.at(static_cast<size_t>(state.page)).frames;
        if (state.selectedRectangle < 0 || state.selectedRectangle >= static_cast<int>(rects.size())) { message = "Select a sheet block first"; return false; }
        const auto& block = rects[static_cast<size_t>(state.selectedRectangle)];
        std::vector<std::uint8_t> encoded;
        if (state.resolvedSheet) stbi_write_png_to_func([](void* context, void* bytes, int count) {
            auto& data = *static_cast<std::vector<std::uint8_t>*>(context);
            const auto* first = static_cast<const std::uint8_t*>(bytes); data.insert(data.end(), first, first + count);
        }, &encoded, state.mounted.png.width, state.mounted.png.height, 4, state.mounted.png.frames.front().rgba.data(), state.mounted.png.width * 4);
        else encoded = vfs.readBytes(state.resource.pages[static_cast<size_t>(state.page)].imagePath, 128u * 1024u * 1024u).value_or(std::vector<std::uint8_t>{});
        const auto prepared = fml::extractAtlasBlock(encoded, block);
        if (!prepared.ok) { message = prepared.error; return false; }
        const auto written = fml::writeMountedPng(target, prepared.animation, 0);
        message = written.ok ? target.u8string() : written.error;
        return written.ok;
    }
    if (action == StageAssetExport::AnimationBatch) {
        std::error_code error;
        if (!fs::create_directory(target, error)) { message = "Could not create animation export folder"; return false; }
        nlohmann::json manifest = nlohmann::json::array();
        bool ok = true; size_t count = 0;
        for (size_t index = 0; index < state.resource.animations.size(); ++index) {
            if (index >= state.selectedAnimations.size() || !state.selectedAnimations[index]) continue;
            ++count;
            const auto& definition = state.resource.animations[index];
            auto prepared = fml::prepareStageAssetAnimation(state.resource, static_cast<int>(index));
            std::string detail = prepared.error;
            bool saved = false;
            const fs::path folder = target / std::to_string(index + 1);
            if (prepared.ok && fs::create_directory(folder, error)) {
                prepared.animation.loop = state.loop;
                prepared.animation.fps = std::clamp(static_cast<int>(std::lround(prepared.animation.fps * state.speed)), 1, 100);
                const auto gif = fml::writeAnimatedGif(folder / "animation.gif", prepared.animation);
                fml::MountedSheet sheet;
                if (gif.ok && fml::packMountedSheet(prepared, definition, sheet, detail)) {
                    const auto png = fml::writeMountedPng(folder / "sheet.png", sheet.png, 0);
                    std::ofstream xml(folder / "sheet.xml", std::ios::binary), info(folder / "animation.txt", std::ios::binary);
                    xml << sheet.xml; info << sheet.metadata; xml.close(); info.close();
                    saved = png.ok && static_cast<bool>(xml) && static_cast<bool>(info);
                    if (!saved) detail = png.ok ? "Sheet metadata write failed" : png.error;
                } else if (!gif.ok) detail = gif.error;
            }
            if (!saved && detail.empty()) detail = "Animation export failed; partial files were retained";
            manifest.push_back({{"animation", definition.name}, {"folder", std::to_string(index + 1)}, {"success", saved}, {"error", detail}});
            ok = ok && saved;
        }
        std::ofstream report(target / "animations.json", std::ios::binary);
        report << nlohmann::json({{"resource", state.resource.object.spritePath}, {"scope", "Visual resources only; no scripts or mechanics"}, {"animations", manifest}}).dump(2); report.close();
        ok = ok && count > 0 && static_cast<bool>(report);
        message = ok ? target.u8string() : "Some animations failed; see animations.json. Completed files were retained.";
        return ok;
    }
    if (action == StageAssetExport::SourceZip) {
        std::set<std::string> files;
        for (const auto& page : state.resource.pages) {
            if (!page.imagePath.empty()) files.insert(page.imagePath);
            if (!page.atlasPath.empty()) files.insert(page.atlasPath);
        }
        if (!state.resource.object.resolvedAtlas.empty()) files.insert(state.resource.object.resolvedAtlas);
        if (files.empty()) { message = "This layer has no original visual files to package"; return false; }
        mz_zip_archive archive{};
        if (!mz_zip_writer_init_heap(&archive, 0, 0)) { message = "Could not initialize visual ZIP"; return false; }
        bool ok = true;
        size_t total = 0;
        for (const std::string& path : files) {
            const fs::path relative = fs::u8path(path);
            if (relative.is_absolute() || std::any_of(relative.begin(), relative.end(), [](const fs::path& part) { return part == ".."; })) { ok = false; break; }
            const auto bytes = vfs.readBytes(path, 128u * 1024u * 1024u);
            if (!bytes || bytes->size() > 256u * 1024u * 1024u - total) { ok = false; break; }
            total += bytes->size();
            if (!mz_zip_writer_add_mem(&archive, fml::Vfs::normalize(path).c_str(), bytes->data(), bytes->size(), MZ_DEFAULT_COMPRESSION)) { ok = false; break; }
        }
        const std::string disclaimer = "Visual resources only. No original scripts, mechanics, shaders or cutscenes are included.\n";
        if (ok) ok = mz_zip_writer_add_mem(&archive, "VISUAL_ONLY.txt", disclaimer.data(), disclaimer.size(), MZ_DEFAULT_COMPRESSION) != 0;
        void* memory = nullptr; size_t length = 0;
        if (ok) ok = mz_zip_writer_finalize_heap_archive(&archive, &memory, &length) != 0;
        mz_zip_writer_end(&archive);
        if (ok) {
            std::ofstream file(target, std::ios::binary);
            file.write(static_cast<const char*>(memory), static_cast<std::streamsize>(length));
            file.close(); ok = static_cast<bool>(file);
        }
        if (memory) mz_free(memory);
        message = ok ? target.u8string() : "Visual ZIP failed: missing files, unsafe paths, memory limit or write failure";
        return ok;
    }
    if (!state.prepared.ok) { message = state.prepared.error; return false; }
    const bool ranged = state.useRange && (action == StageAssetExport::Gif || action == StageAssetExport::Sheet);
    auto sliced = ranged ? fml::sliceMountedAnimation(state.prepared, state.rangeFirst, state.rangeLast) : fml::GifPrepareResult{};
    if (ranged && !sliced.ok) { message = sliced.error; return false; }
    auto& preparation = ranged ? sliced : state.prepared;
    auto& output = preparation.animation;
    struct RestoreMetadata {
        fml::PreparedGifAnimation& output;
        int fps;
        bool loop;
        ~RestoreMetadata() { output.fps = fps; output.loop = loop; }
    } restore{output, output.fps, output.loop};
    output.loop = state.loop;
    output.fps = std::clamp(static_cast<int>(std::lround(output.fps * state.speed)), 1, 100);
    if (action == StageAssetExport::Png || action == StageAssetExport::Gif) {
        const auto written = action == StageAssetExport::Png
            ? fml::writeMountedPng(target, output, static_cast<size_t>(std::clamp(state.frame, 0, static_cast<int>(output.frames.size()) - 1)))
            : fml::writeAnimatedGif(target, output);
        message = written.ok ? target.u8string() : written.error;
        return written.ok;
    }
    fml::MountedSheet sheet;
    const auto& animation = state.resource.animations[static_cast<size_t>(state.animation)];
    if (!fml::packMountedSheet(preparation, animation, sheet, message)) return false;
    std::error_code error;
    if (!fs::create_directory(target, error)) { message = "Could not create mounted-sheet folder: " + error.message(); return false; }
    const auto png = fml::writeMountedPng(target / "sheet.png", sheet.png, 0);
    if (!png.ok) { message = png.error; return false; }
    std::ofstream xml(target / "sheet.xml", std::ios::binary), info(target / "animation.txt", std::ios::binary);
    xml << sheet.xml; info << sheet.metadata;
    xml.close(); info.close();
    const bool ok = static_cast<bool>(xml) && static_cast<bool>(info);
    message = ok ? target.u8string() : "Mounted sheet is incomplete; written files were retained";
    return ok;
}

inline void drawStageAssetViewer(StageAssetViewerState& state, const fml::Vfs& vfs, bool es,
    const std::function<void(StageAssetExport)>& onExport,
    const std::function<void(const std::string&, bool)>& onOpenSource) {
    state.poseView.boundsValid = state.sheetView.boundsValid = false;
    if (!state.open) return;
    if (state.appearing) {
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 28, viewport->WorkPos.y + 35), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::min(1380.0f, viewport->WorkSize.x - 56), std::min(860.0f, viewport->WorkSize.y - 70)), ImGuiCond_Always);
        state.appearing = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(700, 480), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin(es ? "Visor avanzado de recursos###advanced-asset-viewer" : "Advanced Asset Viewer###advanced-asset-viewer", &state.open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!state.open) state.close();
        return;
    }
    ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.50f, 1), "%s", state.resource.object.name.empty() ? state.resource.object.spritePath.c_str() : state.resource.object.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s / %s · %s", state.sourceLabel.c_str(), state.stageId.c_str(), state.format.c_str());
    ImGui::TextDisabled("%s", es ? "Inspección independiente: no cambia el escenario ni interrumpe la canción." : "Independent inspection: does not change the stage or interrupt the song.");
    ImGui::Separator();
    const float inspectorWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.27f, 235.0f, 350.0f);
    ImGui::BeginChild("##asset-viewer-workspace", ImVec2(-inspectorWidth - 10, 0), false);
    ImGui::SetNextItemWidth(-1);
    const char* selectedName = state.animation >= 0 && state.animation < static_cast<int>(state.resource.animations.size())
        ? state.resource.animations[static_cast<size_t>(state.animation)].name.c_str() : (es ? "Sin animación" : "No animation");
    if (ImGui::BeginCombo("##asset-animation", selectedName)) {
        for (size_t index = 0; index < state.resource.animations.size(); ++index)
            if (ImGui::Selectable(state.resource.animations[index].name.c_str(), state.animation == static_cast<int>(index))) {
                state.animation = static_cast<int>(index);
                state.prepare();
                state.playing = true;
            }
        ImGui::EndCombo();
    }
    if (ImGui::Button(state.playing ? (es ? "Pausar" : "Pause") : (es ? "Reproducir" : "Play"))) state.playing = !state.playing;
    ImGui::SameLine();
    if (ImGui::Button(es ? "Inicio" : "Restart")) { state.frame = state.useRange ? state.rangeFirst : 0; state.clock = static_cast<double>(state.frame) / std::max(1, state.prepared.animation.fps); }
    ImGui::SameLine();
    ImGui::Checkbox(es ? "Repetir" : "Loop", &state.loop);
    const int count = static_cast<int>(state.prepared.animation.frames.size());
    if (count > 0) {
        if (state.playing) {
            state.clock += ImGui::GetIO().DeltaTime * state.speed;
            const int first = state.useRange ? state.rangeFirst : 0;
            const int last = state.useRange ? state.rangeLast : count - 1;
            const double start = static_cast<double>(first) / std::max(1, state.prepared.animation.fps);
            const double duration = static_cast<double>(last - first + 1) / std::max(1, state.prepared.animation.fps);
            if (state.clock < start) state.clock = start;
            if (state.loop) state.clock = start + std::fmod(state.clock - start, duration);
            else if (state.clock >= start + duration) { state.clock = start + duration; state.playing = false; }
            state.frame = std::clamp(static_cast<int>(state.clock * state.prepared.animation.fps), first, last);
        }
        auto step = [&](int amount) {
            state.playing = false;
            state.frame = std::clamp(state.frame + amount, 0, count - 1);
            state.clock = static_cast<double>(state.frame) / std::max(1, state.prepared.animation.fps);
        };
        if (ImGui::SmallButton("<##asset-previous-frame")) step(-1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(90.0f, ImGui::GetContentRegionAvail().x - 45));
        int value = state.frame + 1;
        if (ImGui::SliderInt("##asset-frame", &value, 1, count, es ? "Cuadro %d" : "Frame %d")) step(value - 1 - state.frame);
        ImGui::SameLine(); if (ImGui::SmallButton(">##asset-next-frame")) step(1);
        ImGui::TextDisabled("%d / %d · %d FPS · %d x %d", state.frame + 1, count, state.prepared.animation.fps,
                            state.prepared.animation.width, state.prepared.animation.height);
    }
    if (ImGui::BeginTabBar("##asset-viewer-tabs")) {
        if (ImGui::BeginTabItem(es ? "Pose###asset-pose-tab" : "Pose###asset-pose-tab")) {
            ImGui::SetNextItemWidth(180);
            ImGui::Combo("##asset-appearance", &state.appearance, es ? "Pose original\0Aspecto de la capa\0" : "Original pose\0Layer appearance\0");
            const float height = std::max(130.0f, ImGui::GetContentRegionAvail().y - 35);
            drawStageAssetPose(state, ImVec2(0, height), es);
            ImGui::TextDisabled("%s", es ? "Middle drag: mover · Rueda: zoom · Aspecto: escala, ángulo, opacidad y flips" : "Middle drag: pan · Wheel: zoom · Appearance: scale, angle, opacity and flips");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(es ? "Spritesheet###asset-sheet-tab" : "Spritesheet###asset-sheet-tab", nullptr,
                               state.selectSheetTab ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::Checkbox(es ? "Poses resueltas" : "Resolved poses", &state.resolvedSheet);
            ImGui::SameLine(); ImGui::Checkbox(es ? "Límites" : "Frame bounds", &state.showBounds);
            if (!state.resolvedSheet && !state.resource.pages.empty()) {
                state.page = std::clamp(state.page, 0, static_cast<int>(state.resource.pages.size()) - 1);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##asset-source-page", state.resource.pages[static_cast<size_t>(state.page)].imagePath.c_str())) {
                    for (size_t index = 0; index < state.resource.pages.size(); ++index)
                        if (ImGui::Selectable(state.resource.pages[index].imagePath.c_str(), state.page == static_cast<int>(index))) {
                            state.page = static_cast<int>(index); state.selectedRectangle = -1; state.sheetView = {};
                        }
                    ImGui::EndCombo();
                }
            }
            const float height = std::max(130.0f, ImGui::GetContentRegionAvail().y - 85);
            drawStageAssetSheet(state, vfs, ImVec2(0, height));
            const auto* rectangles = state.resolvedSheet ? &state.mountedRects : state.page >= 0 && state.page < static_cast<int>(state.resource.pages.size())
                ? &state.resource.pages[static_cast<size_t>(state.page)].frames : nullptr;
            if (rectangles && state.selectedRectangle >= 0 && state.selectedRectangle < static_cast<int>(rectangles->size())) {
                const auto& rectangle = (*rectangles)[static_cast<size_t>(state.selectedRectangle)];
                ImGui::TextWrapped("%s · (%d, %d) · %d x %d%s", rectangle.name.c_str(), rectangle.x, rectangle.y, rectangle.w, rectangle.h,
                    rectangle.rotated ? (es ? " · Rotado en atlas" : " · Rotated in atlas") : "");
            }
            if (!state.sheetError.empty()) ImGui::TextWrapped("%s", state.sheetError.c_str());
            ImGui::TextDisabled("%s", es ? "Verde: bloques activos · Click: seleccionar · Middle: pan · Rueda: zoom" : "Green: active blocks · Click: select · Middle: pan · Wheel: zoom");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        state.selectSheetTab = false;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##asset-viewer-inspector", ImVec2(0, 0), true);
    ImGui::SeparatorText(es ? "VISTA" : "VIEW");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##asset-background", &state.background, es ? "Transparencia\0Oscuro\0Claro\0" : "Transparency\0Dark\0Light\0");
    ImGui::TextDisabled("%s", es ? "Velocidad" : "Speed");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##asset-speed", &state.speed, 0.1f, 4, "%.2fx");
    ImGui::TextDisabled("%s", es ? "Zoom de pose" : "Pose zoom");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##asset-pose-zoom", &state.poseView.zoom, 0.02f, 16, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("%s", es ? "Zoom de hoja" : "Sheet zoom");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##asset-sheet-zoom", &state.sheetView.zoom, 0.02f, 16, "%.2fx", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button(es ? "Restablecer vistas" : "Reset views", ImVec2(-1, 0))) { state.poseView = {}; state.sheetView = {}; }
    const auto& object = state.resource.object;
    if (ImGui::CollapsingHeader(es ? "Origen y propiedades" : "Origin and properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("%s: %s", es ? "Sprite" : "Sprite", object.spritePath.c_str());
        ImGui::TextWrapped("%s: %s", es ? "Imagen" : "Image", object.resolvedImage.empty() ? (state.resource.generated ? "Generated rectangle" : "Missing") : object.resolvedImage.c_str());
        ImGui::TextWrapped("Atlas: %s", object.resolvedAtlas.empty() ? "—" : object.resolvedAtlas.c_str());
        ImGui::TextWrapped("%s: %s", es ? "Definición" : "Definition", state.definition.c_str());
        ImGui::Text("%s: %.1f, %.1f", es ? "Posición" : "Position", object.position.x, object.position.y);
        ImGui::Text("%s: %.2f, %.2f", es ? "Escala" : "Scale", object.scale.x, object.scale.y);
        ImGui::Text("Offset: %.1f, %.1f", object.frameOffset.x, object.frameOffset.y);
        ImGui::Text("Scroll: %.2f, %.2f", object.scroll.x, object.scroll.y);
        ImGui::Text(es ? "Ángulo: %.1f · Opacidad: %.2f" : "Angle: %.1f · Opacity: %.2f", object.angle, object.alpha);
        ImGui::Text("Flip X: %s · Flip Y: %s", object.flipX ? "yes" : "no", assetFlipY(object) ? "yes" : "no");
        ImGui::Text(es ? "%zu página(s) · %zu animaciones" : "%zu page(s) · %zu animations", state.resource.pages.size(), state.resource.animations.size());
        if (ImGui::Button(es ? "Abrir carpeta origen" : "Open source folder", ImVec2(-1, 0))) onOpenSource(object.resolvedImage.empty() ? state.definition : object.resolvedImage, false);
        if (ImGui::Button(es ? "Abrir definición" : "Open definition", ImVec2(-1, 0))) onOpenSource(state.definition, true);
    }
    if (ImGui::CollapsingHeader(es ? "Capas que comparten el recurso" : "Layers sharing this resource"))
        for (const auto& user : state.users) ImGui::TextWrapped("%zu · %s", user.first + 1, user.second.c_str());
    if (ImGui::CollapsingHeader(es ? "Scripts del stage / posibles referencias" : "Stage scripts / possible references")) {
        ImGui::TextWrapped("%s", es ? "Las coincidencias de texto no prueban que el script afecte a esta capa. No se ejecutan scripts." : "Text matches do not prove that a script affects this layer. Scripts are not executed.");
        for (size_t index = 0; index < state.scripts.size(); ++index) {
            const auto& script = state.scripts[index]; ImGui::PushID(static_cast<int>(index));
            ImGui::TextWrapped("%s · %s%s", script.global ? (es ? "General" : "Mod-wide") : (es ? "Local" : "Local"), script.path.c_str(),
                script.possibleReference ? (es ? " · Posible referencia" : " · Possible reference") : "");
            if (ImGui::SmallButton(es ? "Abrir script" : "Open script")) onOpenSource(script.path, true);
            ImGui::PopID();
        }
    }
    if (!state.resource.error.empty() || !state.prepared.error.empty() || state.resource.inferredAnimations) {
        ImGui::SeparatorText(es ? "DIAGNÓSTICOS" : "DIAGNOSTICS");
        if (!state.resource.error.empty()) ImGui::TextWrapped("%s", state.resource.error.c_str());
        else if (!state.prepared.error.empty()) ImGui::TextWrapped("%s", state.prepared.error.c_str());
        if (state.resource.inferredAnimations) ImGui::TextWrapped("%s", es ? "Animaciones deducidas del atlas: sirven para inspeccionar, no prueban cómo las ejecuta el juego." : "Animations inferred from the atlas: useful for inspection, not proof of game playback behavior.");
    }
    ImGui::SeparatorText(es ? "EXPORTAR RECURSO" : "EXPORT RESOURCE");
    ImGui::Checkbox(es ? "Usar rango de cuadros" : "Use frame range", &state.useRange);
    if (count > 0) {
        int first = state.rangeFirst + 1, last = state.rangeLast + 1;
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##asset-range-first", &first, 1, count, es ? "Desde %d" : "From %d");
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##asset-range-last", &last, 1, count, es ? "Hasta %d" : "To %d");
        state.rangeFirst = std::clamp(first - 1, 0, count - 1); state.rangeLast = std::clamp(last - 1, state.rangeFirst, count - 1);
    }
    ImGui::TextWrapped("%s", es ? "Contenido visual completo, sin recorte de cámara ni scripts. Las poses no incluyen escala, ángulo u opacidad del stage." : "Complete visual content, no camera crop or scripts. Poses do not bake stage scale, angle or opacity.");
    ImGui::BeginDisabled(!state.prepared.ok);
    if (ImGui::Button(es ? "Pose PNG" : "Pose PNG", ImVec2(-1, 0))) onExport(StageAssetExport::Png);
    if (ImGui::Button(es ? "Animación GIF" : "Animation GIF", ImVec2(-1, 0))) onExport(StageAssetExport::Gif);
    if (ImGui::Button(es ? "Hoja montada + XML" : "Mounted sheet + XML", ImVec2(-1, 0))) onExport(StageAssetExport::Sheet);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(state.selectedRectangle < 0 || !state.sheetTexture);
    if (ImGui::Button(es ? "Bloque seleccionado PNG" : "Selected block PNG", ImVec2(-1, 0))) onExport(StageAssetExport::BlockPng);
    ImGui::EndDisabled();
    if (std::any_of(state.selectedAnimations.begin(), state.selectedAnimations.end(), [](bool selected) { return selected; })) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(es ? "Exportar varias animaciones" : "Export multiple animations")) {
        if (ImGui::SmallButton(es ? "Todas" : "All")) state.selectedAnimations.assign(state.resource.animations.size(), true);
        ImGui::SameLine();
        if (ImGui::SmallButton(es ? "Ninguna" : "None")) state.selectedAnimations.assign(state.resource.animations.size(), false);
        for (size_t index = 0; index < state.resource.animations.size(); ++index) {
            bool selected = state.selectedAnimations[index]; ImGui::PushID(static_cast<int>(index));
            if (ImGui::Checkbox(state.resource.animations[index].name.c_str(), &selected)) state.selectedAnimations[index] = selected;
            ImGui::PopID();
        }
        ImGui::BeginDisabled(std::none_of(state.selectedAnimations.begin(), state.selectedAnimations.end(), [](bool selected) { return selected; }));
        if (ImGui::Button(es ? "Exportar selección GIF + PNG/XML" : "Export selected GIF + PNG/XML", ImVec2(-1, 0))) onExport(StageAssetExport::AnimationBatch);
        ImGui::EndDisabled();
    }
    if (ImGui::Button(es ? "Archivos visuales originales ZIP" : "Original visual files ZIP", ImVec2(-1, 0))) onExport(StageAssetExport::SourceZip);
    if (state.captureInspectorScroll > 0) ImGui::SetScrollY(static_cast<float>(state.captureInspectorScroll));
    ImGui::EndChild();
    ImGui::End();
    if (!state.open) state.close();
}

}
