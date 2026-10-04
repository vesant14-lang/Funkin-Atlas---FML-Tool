#pragma once
#include "../../src/fml_app/DiscordRpc.hpp"
#include "../../third_party/json.hpp"
#include <algorithm>
#include <vector>

namespace atlas_ui {

inline constexpr const char* atlasDiscordApplicationId = "1555896044246802532";

struct PresenceOptions {
    bool enabled = false;
    int mode = 1;
    int activityType = 0;
    bool showSource = false, showResource = false, showAnimation = false;
    bool showSong = false, showDifficulty = false, showEngine = false, showCounts = false;
    bool showElapsed = true, prioritizeSong = false, spanish = false;
    std::string imageKey;
};

enum class PresenceTool { Explorer, Characters, Stages, Builder, AssetViewer, Comparison, Importing, Exporting };
enum class PresenceSong { None, Loading, Playing, Paused };

struct PresenceContext {
    PresenceTool tool = PresenceTool::Explorer;
    PresenceSong songState = PresenceSong::None;
    bool playing = false;
    std::string source, resource, secondary, animation, engine;
    std::string song, difficulty, variation;
    size_t sources = 0, frames = 0, animations = 0;
};

inline PresenceOptions readPresenceOptions(const nlohmann::json& data) {
    PresenceOptions options;
    if (!data.is_object()) return options;
    const auto boolean = [&](const char* name, bool& value) {
        const auto it = data.find(name);
        if (it != data.end() && it->is_boolean()) value = it->get<bool>();
    };
    const auto integer = [&](const char* name, int& value, int max) {
        const auto it = data.find(name);
        if (it != data.end() && it->is_number_integer() && *it >= 0 && *it <= max) value = it->get<int>();
    };
    boolean("enabled", options.enabled); integer("mode", options.mode, 2); integer("activityType", options.activityType, 3);
    boolean("showSource", options.showSource); boolean("showResource", options.showResource);
    boolean("showAnimation", options.showAnimation); boolean("showSong", options.showSong);
    boolean("showDifficulty", options.showDifficulty); boolean("showEngine", options.showEngine);
    boolean("showCounts", options.showCounts); boolean("showElapsed", options.showElapsed);
    boolean("prioritizeSong", options.prioritizeSong); boolean("spanish", options.spanish);
    const auto key = data.find("imageKey");
    if (key != data.end() && key->is_string()) options.imageKey = fml::discordAssetKey(key->get<std::string>());
    return options;
}

inline nlohmann::json writePresenceOptions(const PresenceOptions& o) {
    return {{"enabled", o.enabled}, {"mode", o.mode}, {"activityType", o.activityType},
            {"showSource", o.showSource}, {"showResource", o.showResource}, {"showAnimation", o.showAnimation},
            {"showSong", o.showSong}, {"showDifficulty", o.showDifficulty}, {"showEngine", o.showEngine},
            {"showCounts", o.showCounts}, {"showElapsed", o.showElapsed}, {"prioritizeSong", o.prioritizeSong},
            {"spanish", o.spanish}, {"imageKey", o.imageKey}};
}

inline std::string presenceName(const std::string& value) {
    const auto separator = value.find_last_of("/\\");
    std::string name = separator == std::string::npos ? value : value.substr(separator + 1);
    if (name.size() > 1 && name[1] == ':') name.clear();
    return fml::discordText(name, 80);
}

inline fml::DiscordActivity presencePreview(const PresenceOptions& options, const PresenceContext& context, std::int64_t sessionStart) {
    const bool es = options.spanish;
    fml::DiscordActivity activity;
    activity.details = "Funkin Atlas";
    activity.state = es ? "Estudio de personajes y escenarios" : "Character and stage studio";
    activity.startedAt = options.showElapsed ? sessionStart : 0;
    activity.imageKey = fml::discordAssetKey(options.imageKey);
    if (options.mode > 0) {
        switch (context.tool) {
        case PresenceTool::Characters: activity.details = es ? "Visor de personajes" : "Character viewer"; break;
        case PresenceTool::Stages: activity.details = es ? "Visor de escenarios" : "Stage viewer"; break;
        case PresenceTool::Builder: activity.details = "Atlas Builder"; break;
        case PresenceTool::AssetViewer: activity.details = es ? "Visor avanzado de recursos" : "Advanced asset viewer"; break;
        case PresenceTool::Comparison: activity.details = es ? "Comparando recursos A / B" : "Comparing assets A / B"; break;
        case PresenceTool::Importing: activity.details = es ? "Importando recursos" : "Importing resources"; break;
        case PresenceTool::Exporting: activity.details = es ? "Exportando recursos" : "Exporting resources"; break;
        default: activity.details = es ? "Explorando mods" : "Exploring mods"; break;
        }
        if (context.songState == PresenceSong::Playing && options.prioritizeSong &&
            context.tool != PresenceTool::Importing && context.tool != PresenceTool::Exporting) activity.details = "Song Lab";
        if (context.songState == PresenceSong::Playing) activity.state = es ? "Canción en reproducción" : "Song playing";
        else if (context.songState == PresenceSong::Paused) activity.state = es ? "Canción en pausa" : "Song paused";
        else if (context.songState == PresenceSong::Loading) activity.state = es ? "Cargando audio" : "Loading audio";
        else if (context.tool == PresenceTool::Builder) activity.state = es ? "Creando texturas y animaciones" : "Creating textures and animations";
        else if (context.tool == PresenceTool::Importing || context.tool == PresenceTool::Exporting) activity.state = es ? "Trabajando con recursos visuales" : "Working with visual resources";
        else if (context.tool == PresenceTool::Explorer) activity.state = context.sources ? (es ? "Explorando recursos cargados" : "Browsing loaded resources") : (es ? "Listo para cargar mods" : "Ready to load mods");
        else if (context.tool == PresenceTool::Comparison) activity.state = context.playing ? (es ? "Comparación animada" : "Animated comparison") : (es ? "Comparación en pausa" : "Comparison paused");
        else if (context.tool == PresenceTool::Stages) activity.state = context.playing ? (es ? "Preview del escenario en curso" : "Stage preview running") : (es ? "Preview del escenario en pausa" : "Stage preview paused");
        else activity.state = context.playing ? (es ? "Animación en reproducción" : "Animation playing") : (es ? "Inspeccionando recursos" : "Inspecting resources");
    }
    if (options.mode == 2) {
        if (options.showResource && !presenceName(context.resource).empty()) {
            activity.details += ": " + presenceName(context.resource);
            if (!context.secondary.empty()) activity.details += " + " + presenceName(context.secondary);
        }
        std::vector<std::string> parts;
        const auto append = [&](bool show, const std::string& label, const std::string& value) {
            const auto name = presenceName(value); if (show && !name.empty()) parts.push_back(label + name);
        };
        append(options.showAnimation, es ? "Animación: " : "Animation: ", context.animation);
        append(options.showSong && context.songState != PresenceSong::None, es ? "Canción: " : "Song: ", context.song);
        if (options.showDifficulty && context.songState != PresenceSong::None) {
            append(true, es ? "Dificultad: " : "Difficulty: ", context.difficulty);
            append(true, es ? "Variación: " : "Variation: ", context.variation);
        }
        append(options.showSource, "Mod: ", context.source);
        append(options.showEngine, es ? "Motor: " : "Engine: ", context.engine);
        if (options.showCounts) {
            parts.push_back(std::to_string(context.sources) + " mods");
            if (context.tool == PresenceTool::Builder) parts.push_back(std::to_string(context.frames) + " frames / " + std::to_string(context.animations) + (es ? " animaciones" : " animations"));
        }
        if (!parts.empty()) {
            const std::string lead = context.songState == PresenceSong::Playing ? (es ? "Reproduciendo" : "Playing") :
                context.songState == PresenceSong::Paused ? (es ? "En pausa" : "Paused") : std::string{};
            activity.state = lead;
            for (const auto& part : parts) { if (!activity.state.empty()) activity.state += " | "; activity.state += part; }
        }
    }
    if (options.activityType == 1) activity.type = 0;
    else if (options.activityType == 2) activity.type = 2;
    else if (options.activityType == 3) activity.type = 3;
    else activity.type = context.songState == PresenceSong::Playing && options.prioritizeSong ? 2 : 3;
    activity.details = fml::discordText(activity.details);
    activity.state = fml::discordText(activity.state);
    return activity;
}

}
