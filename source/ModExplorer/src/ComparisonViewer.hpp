#pragma once

void setComparisonResource(AtlasApp& app, int side, const ResourceReference& reference) {
    auto& state = app.comparison;
    auto& slot = state.slots[std::clamp(side, 0, 1)];
    if (slot.ready) slot.renderer.shutdown();
    slot.ready = slot.viewValid = false;
    slot.source = reference; slot.error.clear(); slot.label.clear(); slot.animation = 0;
    slot.atlases = AtlasStore{};
    const auto [mod, index] = locateResource(app, reference);
    if (mod < 0) { slot.error = "Source is not loaded"; return; }
    const auto& source = *app.mods[static_cast<size_t>(mod)];
    const auto& asset = source.catalog.assets()[static_cast<size_t>(index)];
    slot.label = source.label + " / " + asset.id;
    if (!asset.valid) { slot.error = asset.error; return; }
    slot.characterMode = asset.kind == ModExplorerAsset::Kind::Character;
    if (slot.characterMode) {
        slot.character = std::get<UniversalCharacter>(asset.parsed);
        if (!asset.previewAnimations.empty()) slot.character.anims = asset.previewAnimations;
        for (auto& animation : slot.character.anims) animation.loop = true;
        slot.stage = {};
        slot.stage.id = "compare-" + asset.id;
        StageObject marker;
        marker.name = "character"; marker.kind = StageObject::Kind::Opponent;
        slot.stage.objects.push_back(std::move(marker));
    } else {
        slot.stage = std::get<UniversalStage>(asset.parsed);
        prepareStageInspection(slot.stage);
        for (auto& object : slot.stage.objects)
            if (object.kind == StageObject::Kind::Player || object.kind == StageObject::Kind::Opponent || object.kind == StageObject::Kind::Girlfriend || object.kind == StageObject::Kind::Character)
                object.properties["visible"] = {PropertyValue::Type::Bool, "false"};
    }
    slot.atlases.readText = [&app, reference](const std::string& path) {
        const auto source = locateResource(app, reference);
        return source.first < 0 ? std::string() : app.mods[source.first]->catalog.vfs().readText(path).value_or("");
    };
    slot.ready = slot.renderer.init([&app, reference](const std::string& path) {
        const auto source = locateResource(app, reference);
        if (source.first < 0) return std::string();
        const auto found = app.mods[source.first]->catalog.vfs().resolve(path);
        return found ? found->u8string() : std::string();
    }, &slot.error);
    slot.animator.reset(slot.stage);
    if (slot.ready && slot.characterMode && slot.character.resolvedAtlas.empty() && !slot.character.resolvedImage.empty()) {
        const auto image = slot.renderer.previewImage(slot.character.resolvedImage);
        if (image.ok) {
            SparrowAtlas atlas; AtlasFrame frame; frame.name = "raw0000";
            frame.w = frame.frameW = image.width; frame.h = frame.frameH = image.height;
            atlas.frames.push_back(frame);
            slot.character.resolvedAtlas = "compare-raw:" + reference.key;
            slot.atlases.putSparrow(slot.character.resolvedAtlas, std::move(atlas));
            AnimationDef animation; animation.name = "raw sheet"; animation.allAtlasFrames = true; animation.loop = true;
            slot.character.anims = {animation};
        }
    }
    if (slot.characterMode && !slot.character.anims.empty()) {
        const auto idle = std::find_if(slot.character.anims.begin(), slot.character.anims.end(), [](const AnimationDef& animation) { return animation.name == "idle"; });
        slot.animation = idle == slot.character.anims.end() ? 0 : static_cast<int>(idle - slot.character.anims.begin());
        slot.animator.play(0, slot.animation);
    }
    for (auto& other : state.slots) other.viewValid = false;
    state.canvas = {};
}

void openComparison(AtlasApp& app, int side, int mod, int index) {
    setComparisonResource(app, side, resourceReference(app, mod, index));
    app.comparison.open = app.comparison.appearing = true;
}

void closeComparison(AtlasApp& app) {
    for (auto& slot : app.comparison.slots) { if (slot.ready) slot.renderer.shutdown(); slot.ready = false; }
    app.comparison.canvas.boundsValid = false;
    app.comparison.open = false;
}

void drawComparison(AtlasApp& app) {
    auto& state = app.comparison;
    state.canvas.boundsValid = false;
    if (!state.open) return;
    const bool es = app.spanish;
    if (state.appearing) {
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 45, viewport->WorkPos.y + 50), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::min(1400.0f, viewport->WorkSize.x - 90), std::min(820.0f, viewport->WorkSize.y - 100)), ImGuiCond_Always);
        ImGui::SetNextWindowFocus(); state.appearing = false;
    }
    if (ImGui::Begin(es ? "Comparación A / B###resource-comparison" : "A / B comparison###resource-comparison", &state.open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.50f, 1), "%s", es ? "COMPARAR RECURSOS" : "COMPARE RESOURCES");
        ImGui::TextDisabled("%s", es ? "Vista independiente · Sin scripts · Arrastra un recurso a A o B · Central: pan · Rueda: zoom" : "Independent view · No scripts · Drag a resource onto A or B · Middle: pan · Wheel: zoom");
        for (int side = 0; side < 2; ++side) {
            auto& slot = state.slots[side]; ImGui::PushID(side);
            ImGui::TextUnformatted(side ? "B" : "A"); ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x * 0.65f));
            if (ImGui::BeginCombo("##compare-resource", slot.label.empty() ? (es ? "Elegir recurso" : "Choose resource") : slot.label.c_str())) {
                for (size_t mod = 0; mod < app.mods.size(); ++mod) {
                    ImGui::PushID(static_cast<int>(mod));
                    ImGui::SeparatorText(app.mods[mod]->label.c_str());
                    const auto& assets = app.mods[mod]->catalog.assets();
                    for (size_t index = 0; index < assets.size(); ++index) {
                        if (!assets[index].valid || (state.slots[1 - side].ready && (assets[index].kind == ModExplorerAsset::Kind::Character) != state.slots[1 - side].characterMode)) continue;
                        ImGui::PushID(static_cast<int>(index));
                        if (ImGui::Selectable(assets[index].id.c_str(), slot.source == resourceReference(app, static_cast<int>(mod), static_cast<int>(index))))
                            setComparisonResource(app, side, resourceReference(app, static_cast<int>(mod), static_cast<int>(index)));
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ATLAS_RESOURCE")) {
                    if (payload->DataSize == sizeof(std::array<int, 2>)) { const auto& source = *static_cast<const std::array<int, 2>*>(payload->Data); setComparisonResource(app, side, resourceReference(app, source[0], source[1])); }
                }
                ImGui::EndDragDropTarget();
            }
            if (slot.ready && slot.characterMode && !slot.character.anims.empty()) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##compare-animation", slot.character.anims[static_cast<size_t>(slot.animation)].name.c_str())) {
                    for (size_t index = 0; index < slot.character.anims.size(); ++index)
                        if (ImGui::Selectable(slot.character.anims[index].name.c_str(), slot.animation == static_cast<int>(index))) { slot.animation = static_cast<int>(index); slot.animator.play(0, slot.animation); }
                    ImGui::EndCombo();
                }
            }
            if (!slot.error.empty()) ImGui::TextWrapped("%s", slot.error.c_str());
            ImGui::PopID();
        }
        if (ImGui::RadioButton(es ? "Lado a lado" : "Side by side", state.mode == 0)) state.mode = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton(es ? "Superposición" : "Overlay", state.mode == 1)) state.mode = 1;
        ImGui::SameLine();
        if (ImGui::Button(state.playing ? (es ? "Pausar" : "Pause") : (es ? "Reproducir" : "Play"))) state.playing = !state.playing;
        ImGui::SameLine();
        if (ImGui::Button(es ? "Reiniciar" : "Restart")) for (auto& slot : state.slots) if (slot.ready) { slot.animator.reset(slot.stage); if (slot.characterMode) slot.animator.play(0, slot.animation); }
        ImGui::SameLine();
        if (ImGui::Button(es ? "Restablecer vista" : "Reset view")) { state.canvas = {}; for (auto& slot : state.slots) slot.viewValid = false; }
        if (state.mode == 1) { ImGui::SetNextItemWidth(240); ImGui::SliderFloat(es ? "Opacidad de B" : "B opacity", &state.blend, 0, 1, "%.2f"); }
        ImGui::SetNextItemWidth(240); ImGui::SliderFloat("Zoom", &state.canvas.zoom, 0.02f, 16, "%.2fx", ImGuiSliderFlags_Logarithmic);
        RenderList lists[2];
        SceneBounds bounds;
        bool fit = false;
        for (int side = 0; side < 2; ++side) {
            auto& slot = state.slots[side];
            if (!slot.ready) continue;
            if (locateResource(app, slot.source).first < 0) { slot.renderer.shutdown(); slot.ready = false; slot.error = "Source was unloaded"; continue; }
            CharacterBinding binding;
            if (slot.characterMode) binding.opponent = &slot.character;
            if (state.playing) slot.animator.update(slot.stage, binding, slot.atlases, ImGui::GetIO().DeltaTime * 1000, 100);
            buildStageRenderList(slot.stage, binding, slot.atlases, slot.renderer, lists[side], &slot.animator);
            if (lists[side].cmds.empty() && slot.error.empty()) slot.error = "No renderable frames. Check the atlas and selected animation.";
            else if (!lists[side].cmds.empty() && slot.error == "No renderable frames. Check the atlas and selected animation.") slot.error.clear();
            for (auto& command : lists[side].cmds) {
                if (command.objectIndex < 0 || command.objectIndex >= static_cast<int>(slot.stage.objects.size())) continue;
                const auto& object = slot.stage.objects[static_cast<size_t>(command.objectIndex)];
                if (const auto visible = object.properties.find("visible"); visible != object.properties.end() && !visible->second.asBool()) command.visible = false;
            }
            includeSceneBounds(lists[side], bounds);
            fit = fit || !slot.viewValid;
        }
        if (fit) {
            const auto sharedView = fitSceneBounds(bounds, false, 960, 540, true);
            for (auto& slot : state.slots) { slot.view = sharedView; slot.viewValid = true; }
        }
        GlRenderer::PreviewImage images[2];
        for (int side = 0; side < 2; ++side) {
            auto& slot = state.slots[side];
            if (!slot.ready || !slot.renderer.beginOffscreenFrame(960, 540)) continue;
            glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
            slot.renderer.draw(lists[side], slot.view, 960, 540, true, false);
            images[side] = slot.renderer.finishOffscreenPreview();
        }
        const int width = state.mode == 0 ? 1920 : 960;
        float scale = 1;
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 origin = atlas_ui::beginAssetCanvas(state.canvas, "##comparison-canvas", size, width, 540, 0, scale);
        for (int side = 0; side < 2; ++side) {
            if (!images[side].ok) continue;
            const ImVec2 start(origin.x + (state.mode == 0 ? side * 960 * scale : 0), origin.y);
            const ImVec2 end(start.x + 960 * scale, start.y + 540 * scale);
            const ImU32 tint = IM_COL32(255, 255, 255, state.mode == 1 && side == 1 ? static_cast<int>(state.blend * 255) : 255);
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(images[side].texture)), start, end, ImVec2(0, 1), ImVec2(1, 0), tint);
            if (state.mode == 0) ImGui::GetWindowDrawList()->AddText(ImVec2(start.x + 8, start.y + 8), IM_COL32(235, 209, 168, 255), side ? "B" : "A");
        }
        atlas_ui::endAssetCanvas();
    }
    ImGui::End();
    if (!state.open) closeComparison(app);
}
