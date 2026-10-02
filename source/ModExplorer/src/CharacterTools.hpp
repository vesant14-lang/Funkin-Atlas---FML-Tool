#pragma once

std::string characterSheetImage(AtlasApp& app, const UniversalCharacter& character) {
    std::vector<std::string> pages = character.resolvedImages;
    if (AtlasStore::isAnimatePath(character.resolvedAtlas))
        if (const AnimateAtlas* atlas = app.atlases.getAnimate(character.resolvedAtlas)) pages = atlas->imagePaths();
    if (pages.empty()) pages.push_back(character.resolvedImage);
    const std::string key = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.root().u8string() + "|" + character.sourcePath;
    int& selected = app.characterSheetPages[key];
    selected = std::clamp(selected, 0, static_cast<int>(pages.size()) - 1);
    if (pages.size() > 1) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo(app.spanish ? "Página##character-sheet-page" : "Page##character-sheet-page", pages[static_cast<size_t>(selected)].c_str())) {
            for (size_t page = 0; page < pages.size(); ++page)
                if (ImGui::Selectable(pages[page].c_str(), selected == static_cast<int>(page))) selected = static_cast<int>(page);
            ImGui::EndCombo();
        }
    }
    return pages[static_cast<size_t>(selected)];
}

int frameCountFor(AtlasApp& app, const AnimationDef& animation) {
    const std::string& atlasPath = app.previewCharacter.resolvedAtlas;
    if (AtlasStore::isAnimatePath(atlasPath)) {
        const AnimateAtlas* atlas = app.atlases.getAnimate(atlasPath);
        return !animation.indices.empty() ? static_cast<int>(animation.indices.size())
             : atlas ? atlas->frameCount(animation.atlasPrefix) : 0;
    }
    const SparrowAtlas* atlas = app.atlases.getCharacter(app.previewCharacter);
    if (!atlas) return 0;
    if (!animation.indices.empty()) return static_cast<int>(animation.indices.size());
    return animation.allAtlasFrames ? static_cast<int>(atlas->frames.size())
         : static_cast<int>(atlas->framesFor(animation.atlasPrefix).size());
}

void refreshPreviewCharacter(AtlasApp& app) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || !asset->valid || asset->kind != ModExplorerAsset::Kind::Character) return;
    app.previewCharacter = std::get<UniversalCharacter>(asset->parsed);
    if (!asset->previewAnimations.empty()) app.previewCharacter.anims = asset->previewAnimations;
    ensureCharacterAtlas(app);
    app.previewCharacter.flipX = app.previewCharacter.flipX != app.flipX;
    if (app.pixelPerfect) app.previewCharacter.antialiasing = false;
    for (AnimationDef& animation : app.previewCharacter.anims)
        animation.loop = animation.loop || app.loopPreview;
    if (app.previewCharacter.anims.empty()) {
        AnimationDef raw;
        raw.name = "raw sheet";
        raw.allAtlasFrames = true;
        raw.fps = 24;
        raw.loop = true;
        app.previewCharacter.anims.push_back(raw);
    }
    std::vector<int> selectedFrames;
    int firstManual = 0;
    if (!app.tracedFrames.empty() && !AtlasStore::isAnimatePath(app.previewCharacter.resolvedAtlas)) {
        SparrowAtlas combined;
        if (const SparrowAtlas* original = app.atlases.getCharacter(app.previewCharacter))
            combined = *original;
        firstManual = static_cast<int>(combined.frames.size());
        for (size_t index = 0; index < app.tracedFrames.size(); ++index) {
            AtlasFrame frame = app.tracedFrames[index];
            frame.name = "atlas_manual_" + std::to_string(index);
            combined.frames.push_back(std::move(frame));
        }
        const std::string manualPath = "atlas-manual:" + asset->key;
        app.atlases.putSparrow(manualPath, std::move(combined));
        app.previewCharacter.resolvedAtlas = manualPath;
    }
    const SparrowAtlas* sourceAtlas = AtlasStore::isAnimatePath(app.previewCharacter.resolvedAtlas)
        ? nullptr : app.atlases.getCharacter(app.previewCharacter);
    const UniversalCharacter& original = std::get<UniversalCharacter>(asset->parsed);
    const auto& authored = asset->previewAnimations.empty() ? original.anims : asset->previewAnimations;
    const AnimationDef* animateSource = AtlasStore::isAnimatePath(app.previewCharacter.resolvedAtlas) &&
        !authored.empty() ? &authored[static_cast<size_t>(std::clamp(app.animateSourceIndex,
            0, static_cast<int>(authored.size()) - 1))] : nullptr;
    const int animateFrames = animateSource ? frameCountFor(app, *animateSource) : 0;
    for (const CustomPose& pose : app.manualOrder) {
        if (pose.traced) {
            if (pose.index >= 0 && pose.index < static_cast<int>(app.tracedFrames.size()))
                selectedFrames.push_back(firstManual + pose.index);
        } else if (animateSource && pose.index >= 0 && pose.index < animateFrames) {
            selectedFrames.push_back(animateSource->indices.empty() ? pose.index
                : animateSource->indices[static_cast<size_t>(pose.index)]);
        } else if (sourceAtlas && pose.index >= 0 && pose.index < static_cast<int>(sourceAtlas->frames.size()) && (app.tracedFrames.empty() || pose.index < firstManual)) {
            selectedFrames.push_back(pose.index);
        }
    }
    if (!selectedFrames.empty()) {
        AnimationDef custom = animateSource ? *animateSource : AnimationDef{};
        custom.name = app.poseName[0] ? app.poseName.data() : "custom frames";
        custom.allAtlasFrames = !animateSource;
        custom.indices = std::move(selectedFrames);
        custom.fps = std::clamp(app.customFps, 1, 100);
        custom.loop = true;
        app.previewCharacter.anims.push_back(std::move(custom));
    }
    app.animationIndex = std::clamp(app.animationIndex, 0,
        std::max(0, static_cast<int>(app.previewCharacter.anims.size()) - 1));
    app.animator.reset(app.previewStage);
    app.songLab.viewDirty = true;
    if (!app.previewCharacter.anims.empty()) app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
    if (app.secondaryMarkerIndex >= 0 && !app.previewSecondaryCharacter.anims.empty())
        app.animator.play(static_cast<size_t>(app.secondaryMarkerIndex),
            std::clamp(app.secondaryAnimationIndex, 0,
                       static_cast<int>(app.previewSecondaryCharacter.anims.size()) - 1));
    app.sequenceToken = -1;
}

void updateCharacterSequence(AtlasApp& app, float deltaMs) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character ||
        app.sequence.empty() || app.manualPreviewOverride) return;
    const auto& animations = app.previewCharacter.anims;
    double totalMs = 0.0;
    for (const AtlasSequenceStep& step : app.sequence) {
        if (step.animation < 0 || step.animation >= static_cast<int>(animations.size())) continue;
        const AnimationDef& animation = animations[static_cast<size_t>(step.animation)];
        totalMs += static_cast<double>(std::max(1, frameCountFor(app, animation))) *
                   std::clamp(step.repeat, 1, 64) * 1000.0 / std::max(1, animation.fps);
    }
    if (totalMs <= 0.0) return;
    if (app.playing) app.sequenceClockMs += deltaMs;
    double time = app.loopPreview ? std::fmod(app.sequenceClockMs, totalMs)
                                  : std::min(app.sequenceClockMs, totalMs - 0.001);
    int token = 0;
    for (const AtlasSequenceStep& step : app.sequence) {
        if (step.animation < 0 || step.animation >= static_cast<int>(animations.size())) continue;
        const AnimationDef& animation = animations[static_cast<size_t>(step.animation)];
        const double duration = static_cast<double>(std::max(1, frameCountFor(app, animation))) *
                                1000.0 / std::max(1, animation.fps);
        for (int n = 0; n < std::clamp(step.repeat, 1, 64); ++n, ++token) {
            if (time < duration) {
                if (app.sequenceToken != token) {
                    app.animator.play(static_cast<size_t>(app.characterMarkerIndex), step.animation);
                    app.sequenceToken = token;
                }
                return;
            }
            time -= duration;
        }
    }
}

bool prepareCharacterExport(AtlasApp& app, bool sequence,
                            GifPrepareResult& result, AnimationDef& definition) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character ||
        app.previewCharacter.anims.empty() || app.previewCharacter.resolvedImage.empty()) {
        setStatus(app, "No hay animación o spritesheet resuelto.", "No resolved animation or spritesheet.");
        return false;
    }
    const auto& vfs = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.vfs();
    const auto source = prepareCharacterVisual(vfs, app.previewCharacter,
        AtlasStore::isAnimatePath(app.previewCharacter.resolvedAtlas) ? nullptr : app.atlases.getCharacter(app.previewCharacter));
    if (!source.error.empty()) {
        setStatus(app, "No se pudo preparar el recurso: " + source.error, "Could not prepare visual source: " + source.error);
        return false;
    }
    const auto& encoded = source.isAnimate ? source.animate.encodedImage : source.encodedImage;
    const SparrowAtlas* sparrow = source.isAnimate ? nullptr : &source.sparrow;
    const AnimateAtlas* animate = source.isAnimate ? &source.animate.atlas : nullptr;
    definition = app.previewCharacter.anims[static_cast<size_t>(app.animationIndex)];
    if (sequence && !app.sequence.empty()) {
        std::vector<AnimationSequenceStep> steps;
        for (const AtlasSequenceStep& step : app.sequence) {
            if (step.animation < 0 || step.animation >= static_cast<int>(app.previewCharacter.anims.size())) continue;
            steps.push_back({app.previewCharacter.anims[static_cast<size_t>(step.animation)],
                             std::clamp(step.repeat, 1, 64)});
        }
        result = prepareMountedAnimationSequenceGif(encoded, steps, sparrow, animate,
            app.previewCharacter.flipX, app.flipY, app.loopPreview);
    } else {
        result = prepareMountedAnimationGif(encoded, definition, sparrow, animate,
            app.previewCharacter.flipX, app.flipY, app.loopPreview);
    }
    if (!result.ok) {
        setStatus(app, "No se pudo preparar la animación: " + result.error, "Could not prepare animation: " + result.error);
        return false;
    }
    return true;
}

bool exportCharacterAnimation(AtlasApp& app, DialogAction action, const fs::path& folder) {
    GifPrepareResult prepared;
    AnimationDef definition;
    if (!prepareCharacterExport(app, action == DialogAction::AnimationGif, prepared, definition)) return false;
    const ModExplorerAsset* asset = selectedAsset(app);
    const std::string name = safeName(asset->id) + "-" +
        safeName(action == DialogAction::AnimationGif && !app.sequence.empty() ? "sequence" : definition.name);
    if (action == DialogAction::AnimationGif) {
        const fs::path target = availablePath(folder / fs::u8path(name + ".gif"), false);
        const GifWriteResult result = writeAnimatedGif(target, prepared.animation);
        setStatus(app, result.ok ? "Guardado: " + target.u8string() : "No se pudo exportar: " + result.error, result.ok ? "Saved: " + target.u8string() : "Export failed: " + result.error);
        return result.ok;
    }
    if (action == DialogAction::AnimationPng) {
        const fs::path target = availablePath(folder / fs::u8path(name + "-frame.png"), false);
        const size_t frame = static_cast<size_t>(std::clamp(app.animator.frameOf(static_cast<size_t>(app.characterMarkerIndex)), 0,
            std::max(0, static_cast<int>(prepared.animation.frames.size()) - 1)));
        const GifWriteResult result = writeMountedPng(target, prepared.animation, frame);
        setStatus(app, result.ok ? "Guardado: " + target.u8string() : "No se pudo exportar: " + result.error, result.ok ? "Saved: " + target.u8string() : "Export failed: " + result.error);
        return result.ok;
    }
    MountedSheet sheet;
    std::string error;
    if (!packMountedSheet(prepared, definition, sheet, error)) {
        setStatus(app, "No se pudo montar la hoja: " + error, "Could not build the sheet: " + error);
        return false;
    }
    const fs::path target = availablePath(folder / fs::u8path(name + "-frames"), true);
    std::error_code ec;
    if (!fs::create_directory(target, ec)) {
        setStatus(app, "No se pudo crear la carpeta: " + ec.message(), "Could not create folder: " + ec.message());
        return false;
    }
    const GifWriteResult png = writeMountedPng(target / "sheet.png", sheet.png, 0);
    if (!png.ok) { setStatus(app, "No se pudo guardar el PNG: " + png.error, "Could not save PNG: " + png.error); return false; }
    std::ofstream xml(target / "sheet.xml", std::ios::binary);
    std::ofstream info(target / "animation.txt", std::ios::binary);
    xml << sheet.xml;
    info << sheet.metadata;
    if (!xml || !info) {
        setStatus(app, "La hoja quedó incompleta; se conservaron los archivos escritos.", "The sheet is incomplete; written files were retained.");
        return false;
    }
    setStatus(app, "Hoja guardada: " + target.u8string(), "Sheet saved: " + target.u8string());
    return true;
}

void removeCustomPose(AtlasApp& app, size_t orderIndex) {
    if (orderIndex >= app.manualOrder.size()) return;
    const CustomPose removed = app.manualOrder[orderIndex];
    app.manualOrder.erase(app.manualOrder.begin() + static_cast<std::ptrdiff_t>(orderIndex));
    if (removed.traced && removed.index >= 0 &&
        removed.index < static_cast<int>(app.tracedFrames.size())) {
        const bool referenced = std::any_of(app.manualOrder.begin(), app.manualOrder.end(),
            [&](const CustomPose& pose) { return pose.traced && pose.index == removed.index; });
        if (!referenced) {
            app.tracedFrames.erase(app.tracedFrames.begin() + removed.index);
            for (CustomPose& pose : app.manualOrder)
                if (pose.traced && pose.index > removed.index) --pose.index;
        }
    }
    app.customFrames.clear();
    for (const CustomPose& pose : app.manualOrder)
        if (!pose.traced) app.customFrames.push_back(pose.index);
    app.selectedPoseIndex = -1;
    refreshPreviewCharacter(app);
}

bool ensureMountedView(AtlasApp& app, const UniversalCharacter& character,
                       const AnimationDef& animation, int slot) {
    MountedViewCache& cache = app.mountedViews[std::clamp(slot, 0, 2)];
    std::string key = normalizedRoot(app.mods[static_cast<size_t>(app.selectedMod)]->catalog.root()) + "|" +
        character.resolvedImage + "|" + character.resolvedAtlas + "|" + animation.name + "|" +
        animation.atlasPrefix + "|" + std::to_string(animation.fps) + "|" +
        std::to_string(animation.offset.x) + "|" + std::to_string(animation.offset.y) + "|" +
        std::to_string(character.flipX) + "|" + std::to_string(animation.flipX) + "|" +
        std::to_string(animation.flipY);
    for (int index : animation.indices) key += "," + std::to_string(index);
    for (const auto& path : character.resolvedImages) key += "|image:" + path;
    for (const auto& path : character.resolvedAtlases) key += "|atlas:" + path;
    if (cache.key == key) return cache.error.empty() && cache.texture != 0;
    cache.key = std::move(key);
    cache.error.clear();
    cache.width = cache.height = cache.frameWidth = cache.frameHeight = cache.frameCount = 0;
    const Vfs& vfs = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.vfs();
    const auto source = prepareCharacterVisual(vfs, character,
        AtlasStore::isAnimatePath(character.resolvedAtlas) ? nullptr : app.atlases.getCharacter(character));
    GifPrepareResult prepared = prepareCharacterVisualAnimation(source, animation, character.flipX, false, true);
    if (!prepared.ok) { cache.error = prepared.error; return false; }
    MountedSheet sheet;
    if (!packMountedSheet(prepared, animation, sheet, cache.error)) return false;
    int maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    if (sheet.png.width > maxTexture || sheet.png.height > maxTexture) {
        cache.error = "Mounted sheet exceeds GPU texture size";
        return false;
    }
    if (!cache.texture) glGenTextures(1, &cache.texture);
    glBindTexture(GL_TEXTURE_2D, cache.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    character.antialiasing ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    character.antialiasing ? GL_LINEAR : GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sheet.png.width, sheet.png.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, sheet.png.frames.front().rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    cache.width = sheet.png.width;
    cache.height = sheet.png.height;
    cache.frameWidth = prepared.animation.width;
    cache.frameHeight = prepared.animation.height;
    cache.frameCount = static_cast<int>(prepared.animation.frames.size());
    return true;
}

void drawSpriteSheet(AtlasApp& app) {
    const auto& character = app.previewCharacter;
    if (character.resolvedImage.empty() || !app.rendererReady) return;
    const std::string pageImage = characterSheetImage(app, character);
    const auto image = app.renderer.previewImage(pageImage);
    if (!image.ok || image.width <= 0 || image.height <= 0) return;
    ImGui::SetNextItemWidth(170.0f);
    ImGui::SliderFloat(app.spanish ? "Zoom de hoja" : "Sheet zoom", &app.sheetZoom, 0.2f, 32.0f, "%.2fx");
    ImGui::TextDisabled("%s", app.spanish ? "Clic central: mover · Rueda: zoom sobre el cursor"
                                          : "Middle-drag: pan · Wheel: zoom at cursor");
    ImGui::BeginChild("##sheet-view", ImVec2(0.0f, 330.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    const float fit = std::min((ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x * 2.0f - 12.0f) / image.width,
                               295.0f / image.height);
    const float oldScale = std::max(0.01f, fit * app.sheetZoom);
    const ImVec2 oldOrigin = ImGui::GetCursorScreenPos();
    const float oldScrollX = ImGui::GetScrollX();
    const float oldScrollY = ImGui::GetScrollY();
    float targetScrollX = oldScrollX;
    float targetScrollY = oldScrollY;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) app.sheetPanning = false;
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        app.sheetPanning = true;
    if (app.sheetPanning) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        targetScrollX -= delta.x;
        targetScrollY -= delta.y;
    }
    if (app.sheetWheel != 0.0f) {
        const float before = app.sheetZoom;
        app.sheetZoom = std::clamp(before * std::pow(1.15f, app.sheetWheel), 0.2f, 32.0f);
        const float nextScale = std::max(0.01f, fit * app.sheetZoom);
        const float anchorX = (app.sheetWheelPos.x - oldOrigin.x) / oldScale;
        const float anchorY = (app.sheetWheelPos.y - oldOrigin.y) / oldScale;
        targetScrollX += anchorX * (nextScale - oldScale);
        targetScrollY += anchorY * (nextScale - oldScale);
    }
    const float scale = std::max(0.01f, fit * app.sheetZoom);
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image.texture)),
                 ImVec2(image.width * scale, image.height * scale));
    const ImVec2 origin = ImGui::GetItemRectMin();
    const bool hovered = ImGui::IsItemHovered();
    const ModExplorerAsset* asset = selectedAsset(app);
    const auto& original = std::get<UniversalCharacter>(asset->parsed);
    const SparrowAtlas* atlas = AtlasStore::isAnimatePath(original.resolvedAtlas)
        ? nullptr : app.atlases.getCharacter(original);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (atlas && app.frameGeometry) {
        for (const AtlasFrame& frame : atlas->frames) {
            if (!frame.sourceImage.empty() && frame.sourceImage != pageImage) continue;
            draw->AddRect(ImVec2(origin.x + frame.x * scale, origin.y + frame.y * scale),
                          ImVec2(origin.x + (frame.x + frame.w) * scale,
                                 origin.y + (frame.y + frame.h) * scale),
                          IM_COL32(232, 183, 107, 170));
        }
    }
    for (const AtlasFrame& frame : app.tracedFrames) {
        if (!frame.sourceImage.empty() && frame.sourceImage != pageImage) continue;
        draw->AddRect(ImVec2(origin.x + frame.x * scale, origin.y + frame.y * scale),
                      ImVec2(origin.x + (frame.x + frame.w) * scale,
                             origin.y + (frame.y + frame.h) * scale),
                      IM_COL32(111, 205, 159, 255), 0.0f, 0, 2.0f);
    }
    for (size_t order = 0; order < app.manualOrder.size(); ++order) {
        const CustomPose& pose = app.manualOrder[order];
        const AtlasFrame* frame = pose.traced
            ? (pose.index >= 0 && pose.index < static_cast<int>(app.tracedFrames.size())
                ? &app.tracedFrames[static_cast<size_t>(pose.index)] : nullptr)
            : (atlas && pose.index >= 0 && pose.index < static_cast<int>(atlas->frames.size())
                ? &atlas->frames[static_cast<size_t>(pose.index)] : nullptr);
        if (!frame) continue;
        if (!frame->sourceImage.empty() && frame->sourceImage != pageImage) continue;
        const ImU32 color = static_cast<int>(order) == app.selectedPoseIndex
            ? IM_COL32(255, 226, 105, 255) : IM_COL32(107, 229, 171, 220);
        const ImVec2 corner(origin.x + frame->x * scale, origin.y + frame->y * scale);
        draw->AddRect(corner,
                      ImVec2(corner.x + frame->w * scale, corner.y + frame->h * scale),
                      color, 0.0f, 0, 2.5f);
        draw->AddText(ImVec2(corner.x + 2.0f, corner.y + 2.0f), color,
                      std::to_string(order + 1).c_str());
    }
    if (app.traceAreas && app.gridSize > 0) {
        for (int x = 0; x <= image.width; x += app.gridSize)
            draw->AddLine(ImVec2(origin.x + x * scale, origin.y),
                          ImVec2(origin.x + x * scale, origin.y + image.height * scale),
                          IM_COL32(220, 220, 220, 40));
        for (int y = 0; y <= image.height; y += app.gridSize)
            draw->AddLine(ImVec2(origin.x, origin.y + y * scale),
                          ImVec2(origin.x + image.width * scale, origin.y + y * scale),
                          IM_COL32(220, 220, 220, 40));
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    const float sx = std::clamp((mouse.x - origin.x) / scale, 0.0f, static_cast<float>(image.width));
    const float sy = std::clamp((mouse.y - origin.y) / scale, 0.0f, static_cast<float>(image.height));
    if (app.traceAreas && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        app.traceStart = ImVec2(sx, sy);
        app.tracing = true;
    }
    if (app.traceAreas && app.tracing) {
        const float grid = static_cast<float>(std::max(1, app.gridSize));
        const float x0 = app.gridSize > 0 ? std::floor(std::min(app.traceStart.x, sx) / grid) * grid
                                          : std::floor(std::min(app.traceStart.x, sx));
        const float y0 = app.gridSize > 0 ? std::floor(std::min(app.traceStart.y, sy) / grid) * grid
                                          : std::floor(std::min(app.traceStart.y, sy));
        float x1 = app.gridSize > 0 ? std::ceil(std::max(app.traceStart.x, sx) / grid) * grid
                                     : std::ceil(std::max(app.traceStart.x, sx));
        float y1 = app.gridSize > 0 ? std::ceil(std::max(app.traceStart.y, sy) / grid) * grid
                                     : std::ceil(std::max(app.traceStart.y, sy));
        if (app.gridSize > 0) { x1 = std::max(x1, x0 + grid); y1 = std::max(y1, y0 + grid); }
        x1 = std::min(x1, static_cast<float>(image.width));
        y1 = std::min(y1, static_cast<float>(image.height));
        draw->AddRect(ImVec2(origin.x + x0 * scale, origin.y + y0 * scale),
                      ImVec2(origin.x + x1 * scale, origin.y + y1 * scale),
                      IM_COL32(111, 205, 159, 255), 0.0f, 0, 2.0f);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            app.tracing = false;
            if (x1 - x0 >= 2.0f && y1 - y0 >= 2.0f && app.tracedFrames.size() < 512u) {
                AtlasFrame frame;
                frame.sourceImage = pageImage;
                frame.x = static_cast<int>(x0); frame.y = static_cast<int>(y0);
                frame.w = static_cast<int>(x1 - x0); frame.h = static_cast<int>(y1 - y0);
                frame.frameW = frame.w; frame.frameH = frame.h;
                app.tracedFrames.push_back(frame);
                app.manualOrder.push_back({true, static_cast<int>(app.tracedFrames.size()) - 1});
                refreshPreviewCharacter(app);
                app.animationIndex = static_cast<int>(app.previewCharacter.anims.size()) - 1;
                app.manualPreviewOverride = true;
                app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
            }
        }
    } else if (atlas && app.pickFrames && hovered && app.manualOrder.size() < 512u && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        for (size_t i = 0; i < atlas->frames.size(); ++i) {
            const AtlasFrame& frame = atlas->frames[i];
            if (!frame.sourceImage.empty() && frame.sourceImage != pageImage) continue;
            if (sx < frame.x || sx >= frame.x + frame.w || sy < frame.y || sy >= frame.y + frame.h) continue;
            app.customFrames.push_back(static_cast<int>(i));
            app.manualOrder.push_back({false, static_cast<int>(i)});
            refreshPreviewCharacter(app);
            app.animationIndex = static_cast<int>(app.previewCharacter.anims.size()) - 1;
            app.manualPreviewOverride = true;
            app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
            break;
        }
    }
    ImGui::SetScrollX(std::max(0.0f, targetScrollX));
    ImGui::SetScrollY(std::max(0.0f, targetScrollY));
    if (ImGui::IsWindowHovered() || app.sheetPanning)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ImGui::EndChild();
    app.sheetBoundsMin = ImGui::GetItemRectMin();
    app.sheetBoundsMax = ImGui::GetItemRectMax();
    app.sheetBoundsValid = true;
    app.sheetWindowId = atlasCurrentWindowId();
    app.sheetWheel = 0.0f;
}
void appendLiveSheetFrame(AtlasApp& app, int slot, bool mounted,
                          int sourceAnimation, int index) {
    const bool secondary = slot == 1;
    if (secondary) switchActiveCharacter(app);
    bool valid = index >= 0 && app.manualOrder.size() < 512u;
    if (valid && mounted) {
        const ModExplorerAsset* asset = selectedAsset(app);
        valid = asset && sourceAnimation >= 0 &&
            sourceAnimation < static_cast<int>(app.previewCharacter.anims.size());
        int source = -1;
        if (valid) {
            const UniversalCharacter& original = std::get<UniversalCharacter>(asset->parsed);
            const auto& authored = asset->previewAnimations.empty() ? original.anims : asset->previewAnimations;
            const AnimationDef& shown = app.previewCharacter.anims[static_cast<size_t>(sourceAnimation)];
            for (size_t i = 0; i < authored.size(); ++i)
                if (authored[i].name == shown.name && authored[i].atlasPrefix == shown.atlasPrefix) {
                    source = static_cast<int>(i);
                    break;
                }
        }
        valid = valid && source >= 0 &&
            (app.manualOrder.empty() || app.animateSourceIndex == source);
        if (valid) {
            app.animateSourceIndex = source;
            app.manualOrder.push_back({false, index});
        }
    } else if (valid) {
        const SparrowAtlas* atlas = app.atlases.getCharacter(app.previewCharacter);
        valid = atlas && index < static_cast<int>(atlas->frames.size());
        if (valid) {
            const bool manual = app.previewCharacter.resolvedAtlas.rfind("atlas-manual:", 0) == 0;
            const int base = static_cast<int>(atlas->frames.size()) -
                (manual ? static_cast<int>(app.tracedFrames.size()) : 0);
            app.manualOrder.push_back(index >= base ? CustomPose{true, index - base}
                                                    : CustomPose{false, index});
        }
    }
    if (valid) {
        app.selectedPoseIndex = static_cast<int>(app.manualOrder.size()) - 1;
        refreshPreviewCharacter(app);
        app.animationIndex = static_cast<int>(app.previewCharacter.anims.size()) - 1;
        app.manualPreviewOverride = true;
        app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
    } else {
        setStatus(app, "Ese cuadro no corresponde a la animación que estás editando.",
                       "That frame does not belong to the animation being edited.");
    }
    if (secondary) switchActiveCharacter(app);
}

void drawLiveSheetCanvas(AtlasApp& app, int slot, unsigned int texture,
                         int textureWidth, int textureHeight,
                         float cropX, float cropY, float cropWidth, float cropHeight,
                         const std::vector<AtlasFrame>& selectable,
                         const std::vector<AtlasFrame>& active,
                         bool canAppend, bool mounted, int sourceAnimation) {
    LiveSheetViewState& state = app.liveSheetViews[slot];
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 74.0f));
    ImGui::SliderFloat("##live-sheet-zoom", &state.zoom, 0.2f, 16.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::SmallButton(app.spanish ? "Centrar" : "Reset")) {
        state.zoom = 1.0f;
        state.pan = ImVec2(0.0f, 0.0f);
    }
    const bool selected = state.selected >= 0 &&
        state.selected < static_cast<int>(selectable.size()) &&
        (selectable[static_cast<size_t>(state.selected)].sourceImage.empty() ||
         selectable[static_cast<size_t>(state.selected)].sourceImage == state.image);
    if (selected)
        ImGui::TextDisabled("%s: %d · %s", app.spanish ? "Cuadro" : "Frame",
            state.selected + 1, selectable[static_cast<size_t>(state.selected)].name.c_str());
    else
        ImGui::TextDisabled("%s", app.spanish ? "Clic: seleccionar · Central: mover · Rueda: zoom"
                                            : "Click: select · Middle: pan · Wheel: zoom");
    if (selected && canAppend && ImGui::SmallButton(app.spanish
            ? "Añadir cuadro a la animación propia" : "Add frame to custom animation"))
        appendLiveSheetFrame(app, slot, mounted, sourceAnimation, state.selected);
    const ImVec2 space = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize(std::max(20.0f, space.x), std::max(25.0f, space.y));
    ImGui::InvisibleButton("##live-sheet-canvas", canvasSize);
    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    state.boundsMin = canvasMin;
    state.boundsMax = canvasMax;
    state.boundsValid = true;
    state.windowId = atlasCurrentWindowId();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 center((canvasMin.x + canvasMax.x) * 0.5f,
                        (canvasMin.y + canvasMax.y) * 0.5f);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) state.panning = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) state.panning = true;
    if (state.panning) {
        state.pan.x += ImGui::GetIO().MouseDelta.x;
        state.pan.y += ImGui::GetIO().MouseDelta.y;
    }
    if (state.wheel != 0.0f) {
        const float before = state.zoom;
        state.zoom = std::clamp(before * std::pow(1.15f, state.wheel), 0.2f, 16.0f);
        const float ratio = state.zoom / before;
        state.pan.x = ratio * state.pan.x + (1.0f - ratio) * (state.wheelPos.x - center.x);
        state.pan.y = ratio * state.pan.y + (1.0f - ratio) * (state.wheelPos.y - center.y);
        state.wheel = 0.0f;
    }
    const float fit = std::max(0.001f, std::min(canvasSize.x / std::max(1.0f, cropWidth),
                                                 canvasSize.y / std::max(1.0f, cropHeight)));
    const float scale = fit * state.zoom;
    const ImVec2 origin(center.x - cropWidth * scale * 0.5f + state.pan.x,
                        center.y - cropHeight * scale * 0.5f + state.pan.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvasMin, canvasMax, true);
    draw->AddRectFilled(canvasMin, canvasMax, IM_COL32(18, 21, 25, 255));
    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), origin,
        ImVec2(origin.x + cropWidth * scale, origin.y + cropHeight * scale),
        ImVec2(cropX / textureWidth, cropY / textureHeight),
        ImVec2((cropX + cropWidth) / textureWidth, (cropY + cropHeight) / textureHeight));
    auto outline = [&](const AtlasFrame& region, ImU32 color, float thickness) {
        if (!region.sourceImage.empty() && region.sourceImage != state.image) return;
        draw->AddRect(ImVec2(origin.x + (region.x - cropX) * scale,
                             origin.y + (region.y - cropY) * scale),
                      ImVec2(origin.x + (region.x + region.w - cropX) * scale,
                             origin.y + (region.y + region.h - cropY) * scale),
                      color, 0.0f, 0, thickness);
    };
    if (selectable.size() <= 512)
        for (const AtlasFrame& region : selectable)
            outline(region, IM_COL32(218, 186, 126, 75), 1.0f);
    for (const AtlasFrame& region : active)
        outline(region, IM_COL32(103, 242, 174, 255), 2.5f);
    if (selected)
        outline(selectable[static_cast<size_t>(state.selected)],
                IM_COL32(255, 224, 106, 255), 3.0f);
    draw->PopClipRect();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float x = cropX + (mouse.x - origin.x) / scale;
        const float y = cropY + (mouse.y - origin.y) / scale;
        int picked = -1;
        float area = std::numeric_limits<float>::max();
        for (size_t i = 0; i < selectable.size(); ++i) {
            const AtlasFrame& region = selectable[i];
            if (!region.sourceImage.empty() && region.sourceImage != state.image) continue;
            if (x < region.x || x >= region.x + region.w ||
                y < region.y || y >= region.y + region.h) continue;
            const float candidate = static_cast<float>(region.w) * region.h;
            if (candidate < area) { picked = static_cast<int>(i); area = candidate; }
        }
        state.selected = picked;
        if (picked >= 0 && app.followActiveSheet) app.followActiveSheet = false;
    }
    if (hovered || state.panning) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
}

void drawActiveSpriteSheet(AtlasApp& app, const UniversalCharacter& character,
                           int markerIndex, float height, const char* childId) {
    ImGui::BeginChild(childId, ImVec2(0.0f, height), true);
    ImGui::TextDisabled("%s", app.spanish ? "SPRITESHEET EN VIVO" : "LIVE SPRITESHEET");
    ImGui::TextDisabled("%s", character.id.c_str());
    ImGui::Checkbox(app.spanish ? "Seguir cuadro" : "Follow frame", &app.followActiveSheet);
    const bool isAnimate = AtlasStore::isAnimatePath(character.resolvedAtlas);
    bool& resolved = markerIndex == app.characterMarkerIndex
        ? app.resolvedAnimateSheet : app.secondaryResolvedAnimateSheet;
    const int running = app.animator.animIndexOf(static_cast<size_t>(markerIndex));
    if (isAnimate) {
        if (ImGui::RadioButton(app.spanish ? "Piezas" : "Pieces", !resolved)) resolved = false;
        ImGui::SameLine();
        if (ImGui::RadioButton(app.spanish ? "Montado" : "Mounted", resolved)) resolved = true;
    }
    if (isAnimate && resolved && !character.anims.empty()) {
        const int index = std::clamp(running, 0, static_cast<int>(character.anims.size()) - 1);
        const MountedViewCache* cache = nullptr;
        const int slot = markerIndex == app.characterMarkerIndex ? 0 : 1;
        if (ensureMountedView(app, character, character.anims[static_cast<size_t>(index)], slot))
            cache = &app.mountedViews[slot];
        if (cache && cache->frameCount > 0 && cache->frameWidth > 0 && cache->frameHeight > 0) {
            const int frame = std::clamp(app.animator.frameOf(static_cast<size_t>(markerIndex)),
                0, std::max(0, cache->frameCount - 1));
            const int columns = std::max(1, cache->width / std::max(1, cache->frameWidth));
            const int cellX = (frame % columns) * cache->frameWidth;
            const int cellY = (frame / columns) * cache->frameHeight;
            const int viewWidth = app.followActiveSheet ? cache->frameWidth : cache->width;
            const int viewHeight = app.followActiveSheet ? cache->frameHeight : cache->height;
            const float cropX = app.followActiveSheet ? static_cast<float>(cellX) : 0.0f;
            const float cropY = app.followActiveSheet ? static_cast<float>(cellY) : 0.0f;
            std::vector<AtlasFrame> frames;
            frames.reserve(static_cast<size_t>(cache->frameCount));
            for (int i = 0; i < cache->frameCount; ++i) {
                AtlasFrame cell;
                cell.name = std::to_string(i + 1);
                cell.x = (i % columns) * cache->frameWidth;
                cell.y = (i / columns) * cache->frameHeight;
                cell.w = cache->frameWidth;
                cell.h = cache->frameHeight;
                frames.push_back(std::move(cell));
            }
            drawLiveSheetCanvas(app, slot, cache->texture, cache->width, cache->height,
                cropX, cropY, static_cast<float>(viewWidth), static_cast<float>(viewHeight),
                frames, {frames[static_cast<size_t>(frame)]}, true, true, index);
        } else {
            ImGui::TextWrapped("%s", app.mountedViews[slot].error.c_str());
        }
        ImGui::EndChild();
        return;
    }
    const std::string pageImage = characterSheetImage(app, character);
    const int slot = markerIndex == app.characterMarkerIndex ? 0 : 1;
    app.liveSheetViews[slot].image = pageImage;
    const auto image = pageImage.empty() || !app.rendererReady
        ? GlRenderer::PreviewImage{} : app.renderer.previewImage(pageImage);
    if (!image.ok || image.width <= 0 || image.height <= 0) {
        ImGui::TextDisabled("%s", app.spanish ? "Hoja no disponible" : "Sheet unavailable");
        ImGui::EndChild();
        return;
    }
    std::vector<AtlasFrame> regions;
    if (running >= 0 && running < static_cast<int>(character.anims.size())) {
        const AnimationDef& animation = character.anims[static_cast<size_t>(running)];
        int frame = std::max(0, app.animator.frameOf(static_cast<size_t>(markerIndex)));
        if (!animation.indices.empty())
            frame = std::max(0, animation.indices[static_cast<size_t>(std::clamp(
                frame, 0, static_cast<int>(animation.indices.size()) - 1))]);
        if (AtlasStore::isAnimatePath(character.resolvedAtlas)) {
            if (const AnimateAtlas* atlas = app.atlases.getAnimate(character.resolvedAtlas)) {
                std::vector<AnimateElement> elements;
                atlas->flatten(animation.atlasPrefix, frame, elements);
                for (const AnimateElement& element : elements) {
                    if (!element.sprite) continue;
                    AtlasFrame region;
                    region.sourceImage = element.sprite->sourceImage;
                    region.name = element.sprite->name;
                    region.x = element.sprite->x; region.y = element.sprite->y;
                    region.w = element.sprite->w; region.h = element.sprite->h;
                    regions.push_back(std::move(region));
                }
            }
        } else if (const SparrowAtlas* atlas = app.atlases.getCharacter(character)) {
            std::vector<size_t> frames;
            if (animation.allAtlasFrames) {
                frames.resize(atlas->frames.size());
                for (size_t i = 0; i < frames.size(); ++i) frames[i] = i;
            } else frames = atlas->framesFor(animation.atlasPrefix);
            if (frame >= 0 && frame < static_cast<int>(frames.size()))
                regions.push_back(atlas->frames[frames[static_cast<size_t>(frame)]]);
        }
    }
    float cropX = 0.0f, cropY = 0.0f;
    float cropW = static_cast<float>(image.width), cropH = static_cast<float>(image.height);
    regions.erase(std::remove_if(regions.begin(), regions.end(), [&](const AtlasFrame& region) {
        return !region.sourceImage.empty() && region.sourceImage != pageImage;
    }), regions.end());
    if (app.followActiveSheet && !regions.empty()) {
        float left = static_cast<float>(regions.front().x);
        float top = static_cast<float>(regions.front().y);
        float right = left + regions.front().w;
        float bottom = top + regions.front().h;
        for (const AtlasFrame& region : regions) {
            left = std::min(left, static_cast<float>(region.x));
            top = std::min(top, static_cast<float>(region.y));
            right = std::max(right, static_cast<float>(region.x + region.w));
            bottom = std::max(bottom, static_cast<float>(region.y + region.h));
        }
        const float margin = std::max(24.0f, std::max(right - left, bottom - top) * 0.4f);
        cropX = std::max(0.0f, left - margin);
        cropY = std::max(0.0f, top - margin);
        cropW = std::min(static_cast<float>(image.width) - cropX, right - cropX + margin);
        cropH = std::min(static_cast<float>(image.height) - cropY, bottom - cropY + margin);
    }
    std::vector<AtlasFrame> frames;
    bool canAppend = false;
    if (!isAnimate) {
        if (const SparrowAtlas* atlas = app.atlases.getCharacter(character)) {
            frames = atlas->frames;
            canAppend = !frames.empty();
        }
    }
    if (frames.empty()) {
        AtlasFrame whole;
        whole.name = app.spanish ? "Hoja completa" : "Full sheet";
        whole.w = image.width;
        whole.h = image.height;
        frames.push_back(std::move(whole));
    }
    drawLiveSheetCanvas(app, slot, image.texture, image.width, image.height,
        cropX, cropY, cropW, cropH, frames, regions, canAppend, false, running);
    ImGui::EndChild();
}

void drawAnimateMountedEditor(AtlasApp& app, const std::vector<AnimationDef>& authored) {
    if (authored.empty()) return;
    const AnimationDef& source = authored[static_cast<size_t>(std::clamp(app.animateSourceIndex,
        0, static_cast<int>(authored.size()) - 1))];
    if (!ensureMountedView(app, app.previewCharacter, source, 2)) {
        ImGui::TextWrapped("%s", app.mountedViews[2].error.c_str());
        return;
    }
    const MountedViewCache& cache = app.mountedViews[2];
    ImGui::SetNextItemWidth(170.0f);
    ImGui::SliderFloat(app.spanish ? "Zoom de hoja" : "Sheet zoom", &app.sheetZoom,
                       0.2f, 32.0f, "%.2fx");
    ImGui::TextDisabled("%s", app.spanish
        ? "Haz clic en cada pose montada en el orden deseado · Clic central: mover · Rueda: zoom"
        : "Click mounted poses in order · Middle-drag: pan · Wheel: zoom");
    ImGui::BeginChild("##animate-mounted-editor", ImVec2(0.0f, 330.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const float fit = std::min((ImGui::GetWindowSize().x - 32.0f) / std::max(1, cache.width),
                               295.0f / std::max(1, cache.height));
    const float oldScale = std::max(0.01f, fit * app.sheetZoom);
    const ImVec2 oldOrigin = ImGui::GetCursorScreenPos();
    float targetScrollX = ImGui::GetScrollX();
    float targetScrollY = ImGui::GetScrollY();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) app.sheetPanning = false;
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        app.sheetPanning = true;
    if (app.sheetPanning) {
        targetScrollX -= ImGui::GetIO().MouseDelta.x;
        targetScrollY -= ImGui::GetIO().MouseDelta.y;
    }
    if (app.sheetWheel != 0.0f) {
        app.sheetZoom = std::clamp(app.sheetZoom * std::pow(1.15f, app.sheetWheel), 0.2f, 32.0f);
        const float nextScale = std::max(0.01f, fit * app.sheetZoom);
        targetScrollX += (app.sheetWheelPos.x - oldOrigin.x) / oldScale * (nextScale - oldScale);
        targetScrollY += (app.sheetWheelPos.y - oldOrigin.y) / oldScale * (nextScale - oldScale);
    }
    const float scale = std::max(0.01f, fit * app.sheetZoom);
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(cache.texture)),
                 ImVec2(cache.width * scale, cache.height * scale));
    const ImVec2 origin = ImGui::GetItemRectMin();
    const int columns = std::max(1, cache.width / std::max(1, cache.frameWidth));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (app.frameGeometry) {
        for (int i = 0; i < cache.frameCount; ++i) {
            const int x = (i % columns) * cache.frameWidth;
            const int y = (i / columns) * cache.frameHeight;
            draw->AddRect(ImVec2(origin.x + x * scale, origin.y + y * scale),
                          ImVec2(origin.x + (x + cache.frameWidth) * scale,
                                 origin.y + (y + cache.frameHeight) * scale),
                          IM_COL32(232, 183, 107, 150));
        }
    }
    for (size_t order = 0; order < app.manualOrder.size(); ++order) {
        const int index = app.manualOrder[order].index;
        if (index < 0 || index >= cache.frameCount) continue;
        const int x = (index % columns) * cache.frameWidth;
        const int y = (index / columns) * cache.frameHeight;
        const ImU32 color = static_cast<int>(order) == app.selectedPoseIndex
            ? IM_COL32(255, 226, 105, 255) : IM_COL32(107, 229, 171, 220);
        draw->AddRect(ImVec2(origin.x + x * scale, origin.y + y * scale),
                      ImVec2(origin.x + (x + cache.frameWidth) * scale,
                             origin.y + (y + cache.frameHeight) * scale), color, 0.0f, 0, 2.5f);
        draw->AddText(ImVec2(origin.x + x * scale + 2.0f, origin.y + y * scale + 2.0f),
                      color, std::to_string(order + 1).c_str());
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        app.manualOrder.size() < 512u) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const int x = static_cast<int>((mouse.x - origin.x) / scale) / std::max(1, cache.frameWidth);
        const int y = static_cast<int>((mouse.y - origin.y) / scale) / std::max(1, cache.frameHeight);
        const int index = y * columns + x;
        if (x >= 0 && y >= 0 && index >= 0 && index < cache.frameCount) {
            app.manualOrder.push_back({false, index});
            app.selectedPoseIndex = static_cast<int>(app.manualOrder.size()) - 1;
            refreshPreviewCharacter(app);
            app.animationIndex = static_cast<int>(app.previewCharacter.anims.size()) - 1;
            app.manualPreviewOverride = true;
            app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
        }
    }
    ImGui::SetScrollX(std::max(0.0f, targetScrollX));
    ImGui::SetScrollY(std::max(0.0f, targetScrollY));
    if (ImGui::IsWindowHovered() || app.sheetPanning)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ImGui::EndChild();
    app.sheetBoundsMin = ImGui::GetItemRectMin();
    app.sheetBoundsMax = ImGui::GetItemRectMax();
    app.sheetBoundsValid = true;
    app.sheetWindowId = atlasCurrentWindowId();
    app.sheetWheel = 0.0f;
}

void drawCharacterAnimationList(AtlasApp& app) {
    const bool es = app.spanish;
    const auto& animations = app.previewCharacter.anims;
    if (animations.empty()) {
        ImGui::TextDisabled("%s", es ? "Este personaje no declara animaciones." : "This character has no declared animations.");
        return;
    }
    ImGui::SeparatorText(es ? "ANIMACIÓN EN VIVO" : "LIVE ANIMATION");
    const bool songDriven = app.songLab.audio.playing();
    const int runningIndex = app.animator.animIndexOf(static_cast<size_t>(app.characterMarkerIndex));
    if (runningIndex >= 0 && runningIndex < static_cast<int>(animations.size())) {
        const AnimationDef& running = animations[static_cast<size_t>(runningIndex)];
        const int frames = frameCountFor(app, running);
        const int frame = app.animator.frameOf(static_cast<size_t>(app.characterMarkerIndex));
        ImGui::TextColored(ImVec4(0.40f, 0.89f, 0.65f, 1.0f), "%s: %s  ·  %d/%d",
            es ? "En curso" : "Now playing", running.name.c_str(),
            frames > 0 ? std::clamp(frame + 1, 1, frames) : 0, frames);
    }
    ImGui::InputTextWithHint("##animation-filter", es ? "Buscar animación" : "Search animations",
                             app.animationFilter.data(), app.animationFilter.size());
    ImGui::BeginChild("##animation-list", ImVec2(0.0f, 220.0f), true);
    for (size_t i = 0; i < animations.size(); ++i) {
        if (app.animationFilter[0] && lower(animations[i].name).find(lower(app.animationFilter.data())) == std::string::npos)
            continue;
        ImGui::PushID(static_cast<int>(i));
        const bool running = songDriven && runningIndex == static_cast<int>(i);
        if (running) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.44f, 0.32f, 1.0f));
        const std::string label = (running ? "● " : "") + animations[i].name;
        if (ImGui::Selectable(label.c_str(), songDriven ? running :
                app.animationIndex == static_cast<int>(i))) {
            app.animationIndex = static_cast<int>(i);
            app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
            app.manualPreviewOverride = true;
            app.scrubFrame = -1;
            app.sequenceClockMs = 0.0;
            app.sequenceToken = -1;
        }
        if (running) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void drawCharacterTools(AtlasApp& app, SDL_Window* window) {
    const bool es = app.spanish;
    const auto& animations = app.previewCharacter.anims;
    if (animations.empty()) {
        ImGui::TextDisabled("%s", es ? "Este personaje no declara animaciones." : "This character has no declared animations.");
        return;
    }
    ImGui::SeparatorText(es ? "EDITAR ANIMACIÓN" : "EDIT ANIMATION");
    const AnimationDef& selected = animations[static_cast<size_t>(app.animationIndex)];
    const int frames = frameCountFor(app, selected);
    ImGui::TextDisabled("%s: %s · %d %s · %d fps · %s", es ? "Para editar" : "For editing",
                        selected.name.c_str(), frames, es ? "fotogramas" : "frames", selected.fps,
                        AtlasStore::isAnimatePath(app.previewCharacter.resolvedAtlas) ? "Adobe Animate" : "Sparrow / Packer");
    if (!app.playing && frames > 0) {
        int frame = app.scrubFrame >= 0 ? app.scrubFrame : app.animator.frameOf(static_cast<size_t>(app.characterMarkerIndex));
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::SliderInt(es ? "Fotograma" : "Frame", &frame, 0, frames - 1)) {
            app.scrubFrame = frame;
            CharacterBinding binding;
            binding.opponent = &app.previewCharacter;
            app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
            const double clock = app.animator.timeMs() + frame * 1000.0 / std::max(1, selected.fps);
            app.animator.updateWithBeat(app.previewStage, binding, app.atlases, clock, app.animator.beat(), true);
        }
    }
    if (ImGui::Checkbox(es ? "Repetir vista" : "Loop preview", &app.loopPreview)) refreshPreviewCharacter(app);
    ImGui::SameLine();
    if (ImGui::Checkbox(es ? "Voltear X###flipx" : "Flip X###flipx", &app.flipX)) refreshPreviewCharacter(app);
    ImGui::SameLine();
    ImGui::Checkbox(es ? "Voltear Y###flipy" : "Flip Y###flipy", &app.flipY);
    ImGui::SameLine();
    if (ImGui::Checkbox(es ? "Píxel exacto" : "Pixel perfect", &app.pixelPerfect)) refreshPreviewCharacter(app);
    if (ImGui::CollapsingHeader(es ? "Secuencia de animaciones###sequence" : "Animation sequence###sequence")) {
        if (!app.sequence.empty() && app.manualPreviewOverride &&
            ImGui::Button(es ? "Reanudar secuencia" : "Resume sequence")) {
            app.manualPreviewOverride = false;
            app.sequenceClockMs = 0.0;
            app.sequenceToken = -1;
        }
        if (ImGui::Button(es ? "Añadir animación" : "Add animation")) {
            app.sequence.push_back({app.animationIndex, 1});
            app.manualPreviewOverride = false;
            app.sequenceClockMs = 0.0;
            app.sequenceToken = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button(es ? "Vaciar" : "Clear")) {
            app.sequence.clear();
            app.manualPreviewOverride = false;
            app.sequenceToken = -1;
            app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
        }
        std::vector<const char*> names;
        for (const AnimationDef& animation : animations) names.push_back(animation.name.c_str());
        int remove = -1, move = -1, target = -1;
        for (size_t i = 0; i < app.sequence.size(); ++i) {
            AtlasSequenceStep& step = app.sequence[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%zu", i + 1);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("##animation", &step.animation, names.data(), static_cast<int>(names.size()))) {
                app.sequenceToken = -1;
                app.manualPreviewOverride = false;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::DragInt(es ? "repetir" : "repeat", &step.repeat, 0.2f, 1, 64);
            ImGui::SameLine();
            if (ImGui::SmallButton("^") && i > 0) { move = static_cast<int>(i); target = move - 1; }
            ImGui::SameLine();
            if (ImGui::SmallButton("v") && i + 1 < app.sequence.size()) { move = static_cast<int>(i); target = move + 1; }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (move >= 0) std::swap(app.sequence[static_cast<size_t>(move)], app.sequence[static_cast<size_t>(target)]);
        if (remove >= 0) app.sequence.erase(app.sequence.begin() + remove);
        if (move >= 0 || remove >= 0) {
            app.sequenceClockMs = 0.0;
            app.sequenceToken = -1;
            app.manualPreviewOverride = false;
        }
    }
    if (ImGui::CollapsingHeader(es ? "Spritesheet y poses propias###custom-poses" : "Spritesheet and custom poses###custom-poses", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox(es ? "Mostrar spritesheet" : "Show spritesheet", &app.showSheet);
        ImGui::SameLine();
        ImGui::Checkbox(es ? "Marcos" : "Frame bounds", &app.frameGeometry);
        const ModExplorerAsset* sourceAsset = selectedAsset(app);
        const auto& sourceCharacter = std::get<UniversalCharacter>(sourceAsset->parsed);
        if (!AtlasStore::isAnimatePath(sourceCharacter.resolvedAtlas)) {
            if (ImGui::Checkbox(es ? "Elegir cuadros del atlas" : "Pick atlas frames", &app.pickFrames) && app.pickFrames) {
                app.traceAreas = false;
                app.showSheet = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox(es ? "Trazar área nueva" : "Trace new area", &app.traceAreas) && app.traceAreas) {
                app.pickFrames = false;
                app.showSheet = true;
            }
            const int grids[] = {0, 16, 32, 64, 128};
            const char* sizes[] = {es ? "Libre" : "Free", "16 x 16", "32 x 32", "64 x 64", "128 x 128"};
            int choice = 0;
            for (int i = 0; i < 5; ++i) if (app.gridSize == grids[i]) choice = i;
            ImGui::SetNextItemWidth(170.0f);
            if (ImGui::Combo(es ? "Cuadrícula" : "Grid", &choice, sizes, 5)) app.gridSize = grids[choice];
            ImGui::TextDisabled("%s", app.traceAreas
                ? (es ? "Arrastra sobre la hoja para trazar cada pose. El mod original no cambia." : "Drag over the sheet to trace each pose. The source mod stays unchanged.")
                : (es ? "Haz clic en cada cuadro del atlas en el orden deseado." : "Click atlas frames in the order you want."));
            ImGui::Text("%zu %s", app.manualOrder.size(), es ? "poses propias" : "custom poses");
            if (!app.manualOrder.empty()) {
                if (ImGui::SmallButton(es ? "Deshacer última" : "Undo last")) {
                    removeCustomPose(app, app.manualOrder.size() - 1);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(es ? "Limpiar poses" : "Clear poses")) {
                    app.customFrames.clear();
                    app.tracedFrames.clear();
                    app.manualOrder.clear();
                    app.selectedPoseIndex = -1;
                    refreshPreviewCharacter(app);
                }
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderInt("FPS##custom", &app.customFps, 1, 100)) refreshPreviewCharacter(app);
                int move = -1, target = -1, remove = -1;
                ImGui::BeginChild("##pose-order", ImVec2(0.0f, 150.0f), true);
                for (size_t i = 0; i < app.manualOrder.size(); ++i) {
                    const CustomPose& pose = app.manualOrder[i];
                    const SparrowAtlas* atlas = app.atlases.getCharacter(sourceCharacter);
                    const std::string label = pose.traced
                        ? std::string("Area ") + std::to_string(pose.index + 1)
                        : atlas && pose.index >= 0 && pose.index < static_cast<int>(atlas->frames.size())
                            ? atlas->frames[static_cast<size_t>(pose.index)].name
                            : std::string("Frame ") + std::to_string(pose.index + 1);
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable((std::to_string(i + 1) + ". " + label).c_str(),
                                          app.selectedPoseIndex == static_cast<int>(i),
                                          0, ImVec2(std::max(100.0f, ImGui::GetContentRegionAvail().x - 105.0f), 0.0f)))
                        app.selectedPoseIndex = static_cast<int>(i);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("^") && i > 0) { move = static_cast<int>(i); target = move - 1; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("v") && i + 1 < app.manualOrder.size()) { move = static_cast<int>(i); target = move + 1; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                    ImGui::PopID();
                }
                ImGui::EndChild();
                if (move >= 0) {
                    std::swap(app.manualOrder[static_cast<size_t>(move)], app.manualOrder[static_cast<size_t>(target)]);
                    app.selectedPoseIndex = target;
                }
                if (remove >= 0) {
                    removeCustomPose(app, static_cast<size_t>(remove));
                }
                if (move >= 0) refreshPreviewCharacter(app);
                if (app.selectedPoseIndex >= 0 &&
                    app.selectedPoseIndex < static_cast<int>(app.manualOrder.size())) {
                    const CustomPose& pose = app.manualOrder[static_cast<size_t>(app.selectedPoseIndex)];
                    if (pose.traced && pose.index >= 0 &&
                        pose.index < static_cast<int>(app.tracedFrames.size())) {
                        AtlasFrame& frame = app.tracedFrames[static_cast<size_t>(pose.index)];
                        int area[4] = {frame.x, frame.y, frame.w, frame.h};
                        ImGui::SetNextItemWidth(320.0f);
                        if (ImGui::DragInt4(es ? "Área X/Y/W/H" : "Area X/Y/W/H", area, 1.0f)) {
                            frame.x = std::max(0, area[0]);
                            frame.y = std::max(0, area[1]);
                            frame.w = std::max(2, area[2]);
                            frame.h = std::max(2, area[3]);
                            frame.frameW = frame.w;
                            frame.frameH = frame.h;
                            refreshPreviewCharacter(app);
                        }
                    }
                }
            }
            const std::string key = identity(*app.mods[static_cast<size_t>(app.selectedMod)], *sourceAsset);
            auto& saved = app.savedAnimations[key];
            const char* current = app.savedPoseIndex >= 0 &&
                app.savedPoseIndex < static_cast<int>(saved.size())
                ? saved[static_cast<size_t>(app.savedPoseIndex)].name.c_str()
                : (es ? "Nueva animación" : "New animation");
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::BeginCombo(es ? "Guardadas" : "Saved animations", current)) {
                if (ImGui::Selectable(es ? "Nueva animación" : "New animation", app.savedPoseIndex < 0))
                    loadSavedPose(app, -1);
                for (size_t i = 0; i < saved.size(); ++i)
                    if (ImGui::Selectable(saved[i].name.c_str(), app.savedPoseIndex == static_cast<int>(i)))
                        loadSavedPose(app, static_cast<int>(i));
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText(es ? "Nombre" : "Name", app.poseName.data(), app.poseName.size()))
                refreshPreviewCharacter(app);
            if (ImGui::Button(app.savedPoseIndex >= 0
                    ? (es ? "Guardar cambios" : "Save changes")
                    : (es ? "Guardar animación" : "Save animation")))
                saveCurrentPose(app, false);
            if (app.savedPoseIndex >= 0) {
                ImGui::SameLine();
                if (ImGui::Button(es ? "Guardar copia" : "Save a copy")) saveCurrentPose(app, true);
                ImGui::SameLine();
                if (ImGui::Button(es ? "Borrar guardada" : "Delete saved")) ImGui::OpenPopup("##delete-custom-animation");
                if (ImGui::BeginPopup("##delete-custom-animation")) {
                    ImGui::TextWrapped("%s", es ? "¿Borrar esta animación de la galería? El mod original no cambia."
                                               : "Delete this animation from the library? The source mod is unchanged.");
                    if (ImGui::Button(es ? "Sí, borrar" : "Yes, delete")) {
                        deleteSavedPose(app);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(es ? "Cancelar" : "Cancel")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
        } else {
            const auto& authored = sourceAsset->previewAnimations.empty()
                ? sourceCharacter.anims : sourceAsset->previewAnimations;
            if (!authored.empty()) {
                app.animateSourceIndex = std::clamp(app.animateSourceIndex, 0,
                    static_cast<int>(authored.size()) - 1);
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::BeginCombo(es ? "Animación de origen" : "Source animation",
                        authored[static_cast<size_t>(app.animateSourceIndex)].name.c_str())) {
                    for (size_t i = 0; i < authored.size(); ++i)
                        if (ImGui::Selectable(authored[i].name.c_str(),
                                app.animateSourceIndex == static_cast<int>(i))) {
                            app.animateSourceIndex = static_cast<int>(i);
                            refreshPreviewCharacter(app);
                        }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled("%s", es
                    ? "Poses montadas a partir de las piezas del atlas. El mod original no cambia."
                    : "Complete poses assembled from the atlas parts. The source mod is unchanged.");
                if (!app.manualOrder.empty()) {
                    if (ImGui::SmallButton(es ? "Deshacer última" : "Undo last")) {
                        removeCustomPose(app, app.manualOrder.size() - 1);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(es ? "Limpiar poses" : "Clear poses")) {
                        app.manualOrder.clear();
                        app.selectedPoseIndex = -1;
                        refreshPreviewCharacter(app);
                    }
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::SliderInt("FPS##animate-custom", &app.customFps, 1, 100))
                        refreshPreviewCharacter(app);
                    int move = -1, target = -1, remove = -1;
                    ImGui::BeginChild("##animate-pose-order", ImVec2(0.0f, 145.0f), true);
                    for (size_t i = 0; i < app.manualOrder.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        const std::string label = std::to_string(i + 1) + ". " +
                            (es ? "Pose " : "Frame ") + std::to_string(app.manualOrder[i].index + 1);
                        if (ImGui::Selectable(label.c_str(), app.selectedPoseIndex == static_cast<int>(i),
                                0, ImVec2(std::max(100.0f, ImGui::GetContentRegionAvail().x - 105.0f), 0.0f)))
                            app.selectedPoseIndex = static_cast<int>(i);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("^") && i > 0) {
                            move = static_cast<int>(i);
                            target = move - 1;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("v") && i + 1 < app.manualOrder.size()) {
                            move = static_cast<int>(i);
                            target = move + 1;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    if (move >= 0) {
                        std::swap(app.manualOrder[static_cast<size_t>(move)],
                                  app.manualOrder[static_cast<size_t>(target)]);
                        app.selectedPoseIndex = target;
                    }
                    if (remove >= 0) {
                        removeCustomPose(app, static_cast<size_t>(remove));
                    }
                    if (move >= 0) refreshPreviewCharacter(app);
                }
                const std::string key = identity(*app.mods[static_cast<size_t>(app.selectedMod)], *sourceAsset);
                auto& saved = app.savedAnimations[key];
                const char* current = app.savedPoseIndex >= 0 &&
                    app.savedPoseIndex < static_cast<int>(saved.size())
                    ? saved[static_cast<size_t>(app.savedPoseIndex)].name.c_str()
                    : (es ? "Nueva animación" : "New animation");
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::BeginCombo(es ? "Guardadas" : "Saved animations", current)) {
                    if (ImGui::Selectable(es ? "Nueva animación" : "New animation",
                            app.savedPoseIndex < 0)) loadSavedPose(app, -1);
                    for (size_t i = 0; i < saved.size(); ++i)
                        if (ImGui::Selectable(saved[i].name.c_str(),
                                app.savedPoseIndex == static_cast<int>(i)))
                            loadSavedPose(app, static_cast<int>(i));
                    ImGui::EndCombo();
                }
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::InputText(es ? "Nombre" : "Name", app.poseName.data(), app.poseName.size()))
                    refreshPreviewCharacter(app);
                if (ImGui::Button(app.savedPoseIndex >= 0
                        ? (es ? "Guardar cambios" : "Save changes")
                        : (es ? "Guardar animación" : "Save animation")))
                    saveCurrentPose(app, false);
                if (app.savedPoseIndex >= 0) {
                    ImGui::SameLine();
                    if (ImGui::Button(es ? "Guardar copia" : "Save a copy")) saveCurrentPose(app, true);
                    ImGui::SameLine();
                    if (ImGui::Button(es ? "Borrar guardada" : "Delete saved"))
                        ImGui::OpenPopup("##delete-animate-animation");
                    if (ImGui::BeginPopup("##delete-animate-animation")) {
                        ImGui::TextWrapped("%s", es ? "¿Borrar esta animación de la galería? El mod original no cambia."
                                                   : "Delete this animation from the library? The source mod is unchanged.");
                        if (ImGui::Button(es ? "Sí, borrar" : "Yes, delete")) {
                            deleteSavedPose(app);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(es ? "Cancelar" : "Cancel")) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                }
                if (app.showSheet) drawAnimateMountedEditor(app, authored);
            }
        }
        if (app.showSheet && !AtlasStore::isAnimatePath(sourceCharacter.resolvedAtlas))
            drawSpriteSheet(app);
    }
    if (ImGui::Button(app.sequence.empty() ? (es ? "GIF de animación" : "Animation GIF")
                                          : (es ? "GIF de secuencia" : "Sequence GIF"))) {
        app.dialogAction = DialogAction::AnimationGif;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button(es ? "PNG de pose" : "Pose PNG")) {
        app.dialogAction = DialogAction::AnimationPng;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button(es ? "Hoja montada" : "Mounted sheet")) {
        app.dialogAction = DialogAction::MountedSheet;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    ImGui::TextDisabled("%s", app.previewCharacter.resolvedAtlas.empty() ? "—" : app.previewCharacter.resolvedAtlas.c_str());
}
