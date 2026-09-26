#include "../../src/fml_app/ModExplorerCatalog.hpp"
#include "../../src/fml_app/AnimationGif.hpp"
#include "../../src/fml_app/GifWriter.hpp"
#include "../../src/fml_app/MountedSheet.hpp"
#include "../../src/fml_formats/VSliceExport.hpp"
#include "../../src/fml_formats/PsychCharacter.hpp"
#include "../../src/fml_formats/PsychStage.hpp"
#include "../../src/fml_audio/AudioEngine.hpp"
#include "../../src/fml_formats/LegacyChart.hpp"
#include "../../src/fml_formats/CodenameChart.hpp"
#include "../../src/fml_formats/SongMeta.hpp"
#include "../../src/fml_formats/ChartExchange.hpp"
#include "../../src/fml_formats/PsychSongAudio.hpp"
#include "../../src/fml_render/GlRenderer.hpp"
#include "../../third_party/json.hpp"
#include "../../third_party/miniz/miniz.h"
#include "../../third_party/stb_image_write.h"
#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/imgui_impl_sdl3.h"
#include "../../third_party/imgui/imgui_impl_opengl3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

using namespace fml;
using json = nlohmann::json;
namespace fs = std::filesystem;

struct LoadedMod {
    ModExplorerCatalog catalog;
    std::string label;
    std::string singleFile;
};

enum class DialogAction { None, Add, AddSingle, ManualDefinition, ManualImage, ManualAtlas, ManualSpritemap, ManualIcon, Recover, Export, AnimationGif, AnimationPng, MountedSheet, PairGifs, PairSceneGif, StageGif, StageSheet, StageZip, StageObjectPng, StageScenePng, StageSceneGif };

struct AtlasSequenceStep { int animation = 0; int repeat = 1; };
struct CustomPose { bool traced = false; int index = 0; };
struct SavedPoseAnimation {
    std::string name;
    std::string sourceAnimation;
    int fps = 24;
    std::vector<CustomPose> order;
    std::vector<AtlasFrame> tracedFrames;
};

struct SongAnimationPreset {
    std::string name;
    std::array<std::string, 4> actions;
    std::string idle;
};

struct MountedViewCache {
    std::string key;
    std::string error;
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
    int frameWidth = 0;
    int frameHeight = 0;
    int frameCount = 0;
};

struct LiveSheetViewState {
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
    bool panning = false;
    int selected = -1;
    bool boundsValid = false;
    ImVec2 boundsMin{0.0f, 0.0f};
    ImVec2 boundsMax{0.0f, 0.0f};
    ImVec2 wheelPos{0.0f, 0.0f};
    float wheel = 0.0f;
};

struct StageScriptInventory {
    std::set<std::string> local;
    std::set<std::string> global;
};

struct SongLabEntry {
    enum class Format { Psych, Codename, CodenameLegacy, VSlice } format = Format::Psych;
    std::string sourceRoot;
    std::string sourceLabel;
    std::string collectionLabel;
    std::string id;
    std::string difficulty;
    std::string path;
    std::string metadataPath;
    std::string stage;
    std::string player;
    std::string opponent;
    std::string girlfriend;
};

struct SongLabState {
    AudioEngine audio;
    std::vector<SongLabEntry> songs;
    SongLabEntry activeSong;
    std::vector<std::string> audioPaths;
    std::string indexedSignature;
    std::string activeRoot;
    std::string activeLabel;
    UniversalChart chart;
    TimeMap timeMap;
    std::string status;
    std::string audioWarning;
    std::array<char, 96> filter{};
    std::string modFilter;
    int selected = -1;
    int audioMode = 2;
    int instTrack = -1;
    float masterVolume = 1.0f;
    float instVolume = 1.0f;
    float voicesVolume = 1.0f;
    bool loopSong = false;
    bool onlyRelated = false;
    bool indexed = false;
    bool autoPlay = false;
    bool chartLoaded = false;
    bool audioPending = false;
    bool audioLoaded = false;
    bool viewDirty = false;
    bool wasPlaying = false;
    double previousMs = -1.0;
    double initialSeekMs = 0.0;
    size_t nextNote = 0;
};

struct AtlasTabState {
    bool valid = false;
    std::string root;
    std::string assetKey;
    UniversalStage stage;
    UniversalCharacter character;
    UniversalCharacter secondaryCharacter;
    std::vector<AtlasSequenceStep> sequence;
    std::vector<int> customFrames;
    std::vector<CustomPose> manualOrder;
    std::vector<AtlasFrame> tracedFrames;
    std::vector<AtlasSequenceStep> secondarySequence;
    std::vector<int> secondaryCustomFrames;
    std::vector<CustomPose> secondaryManualOrder;
    std::vector<AtlasFrame> secondaryTracedFrames;
    std::array<char, 128> poseName{};
    std::array<char, 128> secondaryPoseName{};
    int animationIndex = 0;
    int animateSourceIndex = 0;
    int secondaryAnimateSourceIndex = 0;
    bool manualPreviewOverride = false;
    bool secondaryManualPreviewOverride = false;
    int selectedStageObject = 0;
    int stageAnimationIndex = 0;
    int stageActors[3] = {-1, -1, -1};
    bool stageActorPinned[3] = {false, false, false};
    int characterBackdrop = -1;
    int characterStageRole = 1;
    int characterMarkerIndex = 0;
    int secondaryMarkerIndex = -1;
    int secondaryAssetIndex = -1;
    int secondaryAnimationIndex = 0;
    int secondarySavedPoseIndex = -1;
    int secondarySelectedPoseIndex = -1;
    int secondaryCustomFps = 24;
    int secondaryScrubFrame = -1;
    double secondarySequenceClockMs = 0.0;
    int secondarySequenceToken = -1;
    int sheetViewMode = 0;
    bool showLiveSheet = false;
    bool shareStrumline = false;
    int sharedStrumlineLine = -1;
    int savedPoseIndex = -1;
    int selectedPoseIndex = -1;
    int customFps = 24;
    float previewZoom = 1.0f;
    float previewPanX = 0.0f;
    float previewPanY = 0.0f;
    bool characterFrameValid = false;
    ViewportTransform characterFrame;
    int characterFrameWidth = 0;
    int characterFrameHeight = 0;
    float previewSpeed = 1.0f;
    float previewBpm = 100.0f;
    float sheetZoom = 1.0f;
    bool characterUseStagePosition = false;
    bool showStageCharacters = false;
    bool autoFrame = false;
    bool showSheet = false;
    bool frameGeometry = false;
    bool pickFrames = false;
    bool flipX = false;
    bool flipY = false;
    bool pixelPerfect = false;
    bool loopPreview = true;
    bool secondaryLoopPreview = true;
    bool secondaryShowSheet = false;
    bool secondaryFrameGeometry = false;
    bool secondaryPickFrames = false;
    bool secondaryFlipX = false;
    bool secondaryFlipY = false;
    bool secondaryPixelPerfect = false;
    bool resolvedAnimateSheet = true;
    bool secondaryResolvedAnimateSheet = true;
    float secondarySheetZoom = 1.0f;
    std::array<LiveSheetViewState, 2> liveSheetViews;
    int secondaryGridSize = 16;
    bool secondaryTraceAreas = false;
    bool secondaryTracing = false;
    ImVec2 secondaryTraceStart{0.0f, 0.0f};
};

struct AtlasApp {
    std::vector<std::unique_ptr<LoadedMod>> mods;
    GlRenderer renderer;
    GlRenderer quickRenderer;
    AtlasStore atlases;
    AtlasStore quickAtlases;
    StageAnimator animator;
    StageAnimator quickAnimator;
    SongLabState songLab;
    AtlasTabState tabStates[2];
    UniversalStage previewStage;
    RenderList previewHitList;
    ViewportTransform previewHitView;
    bool previewHitValid = false;
    int draggingActorObject = -1;
    UniversalStage quickStage;
    UniversalCharacter previewCharacter;
    UniversalCharacter previewSecondaryCharacter;
    std::vector<AtlasSequenceStep> sequence;
    std::vector<int> customFrames;
    std::vector<CustomPose> manualOrder;
    std::vector<AtlasFrame> tracedFrames;
    std::map<std::string, std::vector<SavedPoseAnimation>> savedAnimations;
    std::map<std::string, int> characterSongLines;
    std::map<std::string, std::array<std::string, 4>> songAnimationActions;
    std::map<std::string, std::string> songAnimationIdles;
    std::map<std::string, std::vector<SongAnimationPreset>> songAnimationPresets;
    std::map<std::string, std::string> songAnimationDraftNames;
    std::map<std::string, StageScriptInventory> stageScriptCache;
    std::array<char, 128> poseName{};
    int savedPoseIndex = -1;
    int selectedPoseIndex = -1;
    int gridSize = 16;
    bool traceAreas = false;
    bool tracing = false;
    ImVec2 traceStart{0.0f, 0.0f};
    double sequenceClockMs = 0.0;
    int sequenceToken = -1;
    int customFps = 24;
    int scrubFrame = -1;
    bool loopPreview = true;
    bool showSheet = false;
    bool followActiveSheet = true;
    bool frameGeometry = false;
    bool pickFrames = false;
    bool flipX = false;
    bool flipY = false;
    bool pixelPerfect = false;
    float sheetZoom = 1.0f;
    bool sheetPanning = false;
    bool sheetBoundsValid = false;
    ImVec2 sheetBoundsMin{0.0f, 0.0f};
    ImVec2 sheetBoundsMax{0.0f, 0.0f};
    ImVec2 sheetWheelPos{0.0f, 0.0f};
    float sheetWheel = 0.0f;
    std::map<std::string, int> engineOverrides;
    std::map<std::string, std::string> galleryRoots;
    std::vector<std::string> gallery;
    std::string pendingRecoveryKey;
    std::string statusEs;
    std::string statusEn;
    std::string capturePath;
    int captureDetailScroll = 0;
    std::string dialogPath;
    std::mutex dialogMutex;
    std::array<char, 2048> root{};
    std::array<char, 2048> manualDefinition{};
    std::array<char, 2048> manualImage{};
    std::array<char, 2048> manualAtlas{};
    std::array<char, 2048> manualSpritemap{};
    std::array<char, 2048> manualIcon{};
    int manualEngine = 0;
    std::array<char, 256> filter{};
    std::array<char, 64> animationFilter{};
    int selectedMod = -1;
    int selectedAsset = -1;
    int renderMod = -1;
    int quickRenderMod = -1;
    int quickMod = -1;
    int quickAsset = -1;
    int quickConfiguredMod = -1;
    int quickConfiguredAsset = -1;
    int tab = 0;
    bool selectTabOnNextFrame = true;
    int animationIndex = 0;
    int animateSourceIndex = 0;
    int secondaryAnimateSourceIndex = 0;
    bool manualPreviewOverride = false;
    bool secondaryManualPreviewOverride = false;
    int stageActors[3] = {-1, -1, -1};
    bool stageActorPinned[3] = {false, false, false};
    int characterBackdrop = -1;
    int characterMarkerIndex = 0;
    int secondaryMarkerIndex = -1;
    int secondaryAssetIndex = -1;
    int secondaryAnimationIndex = 0;
    int sheetViewMode = 0;
    bool showLiveSheet = false;
    bool shareStrumline = false;
    int sharedStrumlineLine = -1;
    std::vector<AtlasSequenceStep> secondarySequence;
    std::vector<int> secondaryCustomFrames;
    std::vector<CustomPose> secondaryManualOrder;
    std::vector<AtlasFrame> secondaryTracedFrames;
    std::array<char, 128> secondaryPoseName{};
    int secondarySavedPoseIndex = -1;
    int secondarySelectedPoseIndex = -1;
    int secondaryCustomFps = 24;
    int secondaryScrubFrame = -1;
    bool secondaryLoopPreview = true;
    bool secondaryShowSheet = false;
    bool secondaryFrameGeometry = false;
    bool secondaryPickFrames = false;
    bool secondaryFlipX = false;
    bool secondaryFlipY = false;
    bool secondaryPixelPerfect = false;
    bool resolvedAnimateSheet = true;
    bool secondaryResolvedAnimateSheet = true;
    float secondarySheetZoom = 1.0f;
    std::array<LiveSheetViewState, 2> liveSheetViews;
    double secondarySequenceClockMs = 0.0;
    int secondarySequenceToken = -1;
    int secondaryGridSize = 16;
    bool secondaryTraceAreas = false;
    bool secondaryTracing = false;
    ImVec2 secondaryTraceStart{0.0f, 0.0f};
    bool characterUseStagePosition = false;
    int characterStageRole = 1;
    int selectedStageObject = 0;
    int stageAnimationIndex = 0;
    MountedViewCache mountedViews[3];
    GifPrepareResult stagePose;
    std::string stagePoseKey;
    unsigned int stagePoseTexture = 0;
    int stagePoseFrame = -1;
    double stagePoseClock = 0.0;
    bool stageLoopPreview = true;
    bool includeStageGifs = false;
    int exportTarget = 0;
    bool rendererReady = false;
    bool galleryOnly = false;
    bool spanish = false;
    bool playing = true;
    bool showStageCharacters = false;
    bool autoFrame = false;
    float previewBpm = 100.0f;
    float previewSpeed = 1.0f;
    bool dialogReady = false;
    float previewZoom = 1.0f;
    float previewPanX = 0.0f;
    float previewPanY = 0.0f;
    bool characterFrameValid = false;
    ViewportTransform characterFrame;
    int characterFrameWidth = 0;
    int characterFrameHeight = 0;
    bool previewPanning = false;
    bool previewBoundsValid = false;
    ImVec2 previewBoundsMin{0.0f, 0.0f};
    ImVec2 previewBoundsMax{0.0f, 0.0f};
    ImVec2 previewWheelPos{0.0f, 0.0f};
    float previewWheel = 0.0f;
    int stageGifSeconds = 3;
    int stageGifFps = 12;
    int previewGifWidth = 960;
    int previewGifHeight = 540;
    int previewGifSeconds = 4;
    int previewGifFps = 12;
    int previewGifMaxFrames = 120;
    DialogAction dialogAction = DialogAction::None;
};

void setStatus(AtlasApp& app, std::string spanish, std::string english) {
    app.statusEs = std::move(spanish);
    app.statusEn = std::move(english);
}

const std::string& statusText(const AtlasApp& app) {
    return app.spanish ? app.statusEs : app.statusEn;
}
std::string diagnosticText(const std::string& message, bool spanish) {
    if (spanish) {
        if (message == "Sprite image not found next to the definition or in shared assets")
            return "No se encontró la imagen junto a la definición ni en los recursos compartidos.";
        const std::string alternate = " animations use a separate atlas; the original paths remain in the export";
        const size_t alternateAt = message.find(alternate);
        if (alternateAt != std::string::npos)
            return message.substr(0, alternateAt) + " animaciones usan otro atlas; se conservan las rutas originales al exportar.";
        const std::string ignored = " stage script calls were not reconstructed";
        const size_t ignoredAt = message.find(ignored);
        if (ignoredAt != std::string::npos)
            return message.substr(0, ignoredAt) + " llamadas del script del escenario no pudieron reconstruirse.";
        return message;
    }
    if (message.find("el personaje no declara") == 0 && message.find("image") != std::string::npos)
        return "The character has no image declaration; there is no atlas to draw.";
    if (message.find("el personaje no declara ninguna animacion") == 0)
        return "The character has no animations; it would remain still without idle in game.";
    if (message.find(" atlas separados por comas; Psych") != std::string::npos)
        return "The image field names multiple atlases. Psych combines them, but this preview uses the first; the original paths are preserved.";
    if (message.find("la animacion numero ") == 0)
        return "An animation has no logical name and was ignored.";
    if (message.find("la animacion ") == 0 && message.find("no declara prefijo") != std::string::npos)
        return "An animation has no atlas prefix; it would have no frames in game.";
    if (message.find("campos que FML no conoce") == 0)
        return "Unknown fields are preserved unchanged: " + message.substr(std::string("campos que FML no conoce y CONSERVA tal cual: ").size());
    if (message.find("este stage no trae") == 0 && message.find("sprites salen de su script") != std::string::npos)
        return "This stage has no declared objects. Its sprites come from the script, preserving order and scroll.";
    if (message.find("este JSON no trae") == 0)
        return "This JSON has no objects: it provides only zoom and actor markers. The scenery needs Lua/Haxe or engine source.";
    if (message.find("este stage no trae") == 0 && message.find("no monta sprites") != std::string::npos)
        return "This stage has no objects and its script does not build sprites declaratively; only zoom and actor markers are available.";
    if (message.find("STAGE PARCIAL") == 0)
        return "PARTIAL STAGE: unresolved conditions or dynamic loops were omitted. Exclusive branches were not combined; source is kept for porting.";
    if (message.find(" ajustes del script ") != std::string::npos)
        return "Some script adjustments have no XML equivalent and were omitted.";
    if (message.find("vista estatica de onCreate") == 0)
        return "Static view of onCreate/onCreatePost; callbacks, modcharts and dynamic expressions need porting and are not executed.";
    if (message.find("tenia filtros de calidad") != std::string::npos)
        return "An object used quality or blend filters without a Codename declarative equivalent; it remains visible.";
    if (message.find("hide_girlfriend se conserva") == 0)
        return "hide_girlfriend is preserved as alpha=0 on the girlfriend marker.";
    return message;
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalizedRoot(const fs::path& path) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    return lower((ec ? path : absolute).lexically_normal().u8string());
}

std::string identity(const LoadedMod& mod, const ModExplorerAsset& asset) {
    return normalizedRoot(mod.catalog.root()) + "|" + asset.key;
}

fs::path galleryPath() {
    if (const char* custom = std::getenv("FUNKIN_ATLAS_GALLERY_PATH"))
        if (*custom) return fs::u8path(custom);
    const char* local = std::getenv("LOCALAPPDATA");
    const fs::path base = local ? fs::u8path(local) : fs::temp_directory_path();
    return base / "FunkinAtlas" / "gallery.json";
}

fs::path oldGalleryPath() {
    const char* local = std::getenv("LOCALAPPDATA");
    const fs::path base = local ? fs::u8path(local) : fs::temp_directory_path();
    return base / "FmlModExplorer" / "gallery.json";
}

void loadGallery(AtlasApp& app) {
    std::ifstream input(galleryPath(), std::ios::binary);
    if (!input && !std::getenv("FUNKIN_ATLAS_GALLERY_PATH"))
        input.open(oldGalleryPath(), std::ios::binary);
    if (!input) return;
    try {
        json data = json::parse(input, nullptr, false);
        if (data.is_array()) {
            for (const json& entry : data)
                if (entry.is_string()) app.gallery.push_back(entry.get<std::string>());
        } else if (data.is_object()) {
            const json assets = data.value("assets", json::array());
            if (assets.is_array())
                for (const json& entry : assets)
                    if (entry.is_string()) app.gallery.push_back(entry.get<std::string>());
            const json roots = data.value("galleryRoots", json::object());
            if (roots.is_object())
                for (auto it = roots.begin(); it != roots.end(); ++it)
                    if (it.value().is_string()) app.galleryRoots[it.key()] = it.value().get<std::string>();
            const json engines = data.value("engineOverrides", json::object());
            if (engines.is_object())
                for (auto it = engines.begin(); it != engines.end(); ++it)
                    if (it.value().is_number_integer()) app.engineOverrides[it.key()] = it.value().get<int>();
            const json language = data.value("language", json());
            if (language.is_string()) app.spanish = language.get<std::string>() != "en";
            const json saved = data.value("customAnimations", json::object());
            if (saved.is_object()) {
                for (auto it = saved.begin(); it != saved.end(); ++it) {
                    if (!it.value().is_array()) continue;
                    auto& animations = app.savedAnimations[it.key()];
                    for (const json& item : it.value()) {
                        if (!item.is_object() || animations.size() >= 32) continue;
                        SavedPoseAnimation animation;
                        animation.name = item.value("name", std::string("Custom animation"));
                        animation.sourceAnimation = item.value("sourceAnimation", std::string());
                        animation.fps = std::clamp(item.value("fps", 24), 1, 100);
                        const json frames = item.value("tracedFrames", json::array());
                        if (frames.is_array()) {
                            for (const json& frame : frames) {
                                if (!frame.is_object() || animation.tracedFrames.size() >= 512) continue;
                                AtlasFrame parsed;
                                parsed.x = frame.value("x", 0);
                                parsed.y = frame.value("y", 0);
                                parsed.w = frame.value("w", 0);
                                parsed.h = frame.value("h", 0);
                                if (parsed.x < 0 || parsed.y < 0 || parsed.w < 2 || parsed.h < 2 ||
                                    parsed.x + parsed.w > 16384 || parsed.y + parsed.h > 16384) continue;
                                parsed.frameW = parsed.w;
                                parsed.frameH = parsed.h;
                                animation.tracedFrames.push_back(parsed);
                            }
                        }
                        const json order = item.value("order", json::array());
                        if (order.is_array()) {
                            for (const json& step : order) {
                                if (!step.is_object() || animation.order.size() >= 512) continue;
                                const CustomPose pose{step.value("traced", false), step.value("index", -1)};
                                if (pose.index < 0 || pose.index >= 65536 ||
                                    (pose.traced && pose.index >= static_cast<int>(animation.tracedFrames.size()))) continue;
                                animation.order.push_back(pose);
                            }
                        }
                        if (!animation.order.empty()) animations.push_back(std::move(animation));
                    }
                }
            }
            const json songActions = data.value("songAnimationActions", json::object());
            if (songActions.is_object()) for (auto it = songActions.begin(); it != songActions.end(); ++it) {
                if (!it.value().is_array() || it.value().size() != 4) continue;
                std::array<std::string, 4> actions;
                bool valid = true;
                for (int direction = 0; direction < 4; ++direction) {
                    if (!it.value()[direction].is_string()) { valid = false; break; }
                    actions[direction] = it.value()[direction].get<std::string>();
                    if (actions[direction].size() > 128) { valid = false; break; }
                }
                if (valid) app.songAnimationActions[it.key()] = std::move(actions);
            }
            const json songIdles = data.value("songAnimationIdles", json::object());
            if (songIdles.is_object()) for (auto it = songIdles.begin(); it != songIdles.end(); ++it)
                if (it.value().is_string()) {
                    const std::string idle = it.value().get<std::string>();
                    if (idle.size() <= 128) app.songAnimationIdles[it.key()] = idle;
                }
            const json songPresets = data.value("songAnimationPresets", json::object());
            if (songPresets.is_object()) for (auto it = songPresets.begin(); it != songPresets.end(); ++it) {
                if (!it.value().is_array()) continue;
                auto& presets = app.songAnimationPresets[it.key()];
                for (const json& item : it.value()) {
                    if (!item.is_object() || presets.size() >= 32) continue;
                    const json actions = item.value("actions", json::array());
                    if (!actions.is_array() || actions.size() != 4) continue;
                    SongAnimationPreset preset;
                    preset.name = item.value("name", std::string());
                    if (preset.name.empty() || preset.name.size() > 96) continue;
                    if (item.contains("idle") && !item["idle"].is_string()) continue;
                    preset.idle = item.value("idle", std::string());
                    if (preset.idle.size() > 128) continue;
                    bool valid = true;
                    for (int direction = 0; direction < 4; ++direction) {
                        if (!actions[direction].is_string()) { valid = false; break; }
                        preset.actions[direction] = actions[direction].get<std::string>();
                        if (preset.actions[direction].size() > 128) { valid = false; break; }
                    }
                    if (valid) presets.push_back(std::move(preset));
                }
            }
        }
    } catch (...) {
        return;
    }
    for (std::string& key : app.gallery) {
        if (key.find('|') != std::string::npos) continue;
        const auto root = app.galleryRoots.find(key);
        if (root == app.galleryRoots.end()) continue;
        const std::string oldKey = key;
        key = normalizedRoot(fs::u8path(root->second)) + "|" + oldKey;
        app.galleryRoots[key] = root->second;
        const auto engine = app.engineOverrides.find(oldKey);
        if (engine != app.engineOverrides.end()) app.engineOverrides[key] = engine->second;
    }
    std::sort(app.gallery.begin(), app.gallery.end());
    app.gallery.erase(std::unique(app.gallery.begin(), app.gallery.end()), app.gallery.end());
}

bool saveGallery(const AtlasApp& app) {
    const fs::path target = galleryPath();
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) return false;
    const fs::path temporary = target.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        json data;
        data["assets"] = app.gallery;
        data["galleryRoots"] = app.galleryRoots;
        data["engineOverrides"] = app.engineOverrides;
        data["language"] = app.spanish ? "es" : "en";
        json saved = json::object();
        for (const auto& [key, animations] : app.savedAnimations) {
            json items = json::array();
            for (const SavedPoseAnimation& animation : animations) {
                json item;
                item["name"] = animation.name;
                item["sourceAnimation"] = animation.sourceAnimation;
                item["fps"] = animation.fps;
                item["order"] = json::array();
                for (const CustomPose& pose : animation.order)
                    item["order"].push_back({{"traced", pose.traced}, {"index", pose.index}});
                item["tracedFrames"] = json::array();
                for (const AtlasFrame& frame : animation.tracedFrames)
                    item["tracedFrames"].push_back({{"x", frame.x}, {"y", frame.y}, {"w", frame.w}, {"h", frame.h}});
                items.push_back(std::move(item));
            }
            if (!items.empty()) saved[key] = std::move(items);
        }
        data["customAnimations"] = std::move(saved);
        json songActions = json::object();
        for (const auto& [key, actions] : app.songAnimationActions)
            songActions[key] = actions;
        data["songAnimationActions"] = std::move(songActions);
        json songIdles = json::object();
        for (const auto& [key, idle] : app.songAnimationIdles)
            songIdles[key] = idle;
        data["songAnimationIdles"] = std::move(songIdles);
        json songPresets = json::object();
        for (const auto& [key, presets] : app.songAnimationPresets) {
            json entries = json::array();
            for (const SongAnimationPreset& preset : presets)
                entries.push_back({{"name", preset.name}, {"actions", preset.actions}, {"idle", preset.idle}});
            if (!entries.empty()) songPresets[key] = std::move(entries);
        }
        data["songAnimationPresets"] = std::move(songPresets);
        output << data.dump(2);
        output.flush();
        if (!output) return false;
    }
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    fs::rename(temporary, target, ec);
    return !ec;
#endif
}

const ModExplorerAsset* selectedAsset(const AtlasApp& app) {
    if (app.selectedMod < 0 || app.selectedMod >= static_cast<int>(app.mods.size())) return nullptr;
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    if (app.selectedAsset < 0 || app.selectedAsset >= static_cast<int>(assets.size())) return nullptr;
    return &assets[static_cast<size_t>(app.selectedAsset)];
}

bool stageRoleDeclared(const LoadedMod& mod, const ModExplorerAsset& asset,
                       StageObject::Kind kind) {
    if (!asset.valid || asset.kind != ModExplorerAsset::Kind::Stage) return false;
    const UniversalStage& stage = std::get<UniversalStage>(asset.parsed);
    const bool hasMarker = std::any_of(stage.objects.begin(), stage.objects.end(),
        [kind](const StageObject& object) { return object.kind == kind && !object.implicit; });
    if (!hasMarker) return false;
    if (asset.format == ModExplorerAsset::Format::PsychLua) return false;
    if (asset.format != ModExplorerAsset::Format::PsychJson) return true;
    const auto source = mod.catalog.vfs().readText(asset.sourcePath);
    if (!source) return false;
    const json root = json::parse(*source, nullptr, false, true);
    const char* key = kind == StageObject::Kind::Player ? "boyfriend" :
                      kind == StageObject::Kind::Girlfriend ? "girlfriend" : "opponent";
    return root.is_object() && root.contains(key) && root[key].is_array() &&
        root[key].size() >= 2 && root[key][0].is_number() && root[key][1].is_number();
}

void chooseDefaultStageActors(AtlasApp& app) {
    if (app.selectedMod < 0) return;
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    std::array<std::vector<std::string>, 3> names{{
        {"bf", "boyfriend", "bfmii"}, {"dad", "daddy"}, {"gf", "girlfriend", "gfmii"}
    }};
    const std::string modName = lower(app.mods[static_cast<size_t>(app.selectedMod)]->label);
    const size_t versus = modName.find("vs ");
    if (versus != std::string::npos) {
        size_t start = versus + 3;
        while (start < modName.size() && !std::isalnum(static_cast<unsigned char>(modName[start]))) ++start;
        size_t end = start;
        while (end < modName.size() && std::isalnum(static_cast<unsigned char>(modName[end]))) ++end;
        if (end > start) names[1].insert(names[1].begin(), modName.substr(start, end - start));
    }
    std::set<int> used;
    for (int role = 0; role < 3; ++role) {
        app.stageActors[role] = -1;
        for (const std::string& name : names[static_cast<size_t>(role)]) {
            for (size_t i = 0; i < assets.size(); ++i) {
                if (assets[i].kind != ModExplorerAsset::Kind::Character || !assets[i].valid ||
                    lower(assets[i].id) != name || used.count(static_cast<int>(i))) continue;
                app.stageActors[role] = static_cast<int>(i);
                break;
            }
            if (app.stageActors[role] >= 0) break;
        }
        if (app.stageActors[role] < 0) {
            for (size_t i = 0; i < assets.size(); ++i) {
                if (assets[i].kind != ModExplorerAsset::Kind::Character || !assets[i].valid ||
                    used.count(static_cast<int>(i))) continue;
                app.stageActors[role] = static_cast<int>(i);
                break;
            }
        }
        if (app.stageActors[role] >= 0) used.insert(app.stageActors[role]);
    }
}

void prepareStageInspection(UniversalStage& stage) {
    int precedingImages = 0;
    for (StageObject& object : stage.objects) {
        const auto visibility = object.properties.find("visible");
        const bool authoredVisible = visibility == object.properties.end() || visibility->second.asBool();
        if (object.kind == StageObject::Kind::Box && authoredVisible &&
            precedingImages >= 2 && object.width * object.scale.x >= 1280.0f &&
            object.height * object.scale.y >= 720.0f && object.alpha >= 0.95f)
            object.properties["visible"] = {PropertyValue::Type::Bool, "false"};
        if (authoredVisible && !object.resolvedImage.empty()) ++precedingImages;
    }
}

void buildCharacterStage(AtlasApp& app) {
    app.characterFrameValid = false;
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character || app.selectedMod < 0) return;
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    if (app.characterBackdrop >= 0 && app.characterBackdrop < static_cast<int>(assets.size()) &&
        assets[static_cast<size_t>(app.characterBackdrop)].valid &&
        assets[static_cast<size_t>(app.characterBackdrop)].kind == ModExplorerAsset::Kind::Stage) {
        app.previewStage = std::get<UniversalStage>(assets[static_cast<size_t>(app.characterBackdrop)].parsed);
        prepareStageInspection(app.previewStage);
    } else {
        app.previewStage = UniversalStage{};
        app.previewStage.id = "atlas-character-" + asset->id;
    }
    if (!app.characterUseStagePosition) {
        auto& objects = app.previewStage.objects;
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const StageObject& object) {
            return object.kind == StageObject::Kind::Player ||
                   object.kind == StageObject::Kind::Opponent ||
                   object.kind == StageObject::Kind::Girlfriend;
        }), objects.end());
    }
    const StageObject::Kind markerKind = app.characterUseStagePosition && app.characterBackdrop >= 0
        ? (app.characterStageRole == 0 ? StageObject::Kind::Player :
           app.characterStageRole == 2 ? StageObject::Kind::Girlfriend : StageObject::Kind::Opponent)
        : StageObject::Kind::Opponent;
    app.characterMarkerIndex = -1;
    for (size_t i = 0; i < app.previewStage.objects.size(); ++i)
        if (app.previewStage.objects[i].kind == markerKind) {
            app.characterMarkerIndex = static_cast<int>(i);
            break;
        }
    if (app.characterMarkerIndex < 0) {
        StageObject marker;
        marker.kind = markerKind;
        marker.name = markerKind == StageObject::Kind::Player ? "player" :
                      markerKind == StageObject::Kind::Girlfriend ? "girlfriend" : "opponent";
        marker.position = {640.0f, 360.0f};
        marker.implicit = true;
        app.characterMarkerIndex = static_cast<int>(app.previewStage.objects.size());
        app.previewStage.objects.push_back(marker);
    }
    if (app.characterUseStagePosition && app.characterBackdrop >= 0 &&
        !stageRoleDeclared(*app.mods[static_cast<size_t>(app.selectedMod)],
                           assets[static_cast<size_t>(app.characterBackdrop)], markerKind))
        app.previewStage.objects[static_cast<size_t>(app.characterMarkerIndex)].implicit = true;
    app.secondaryMarkerIndex = -1;
    if (app.secondaryAssetIndex >= 0 &&
        app.secondaryAssetIndex < static_cast<int>(assets.size()) &&
        assets[static_cast<size_t>(app.secondaryAssetIndex)].kind == ModExplorerAsset::Kind::Character &&
        assets[static_cast<size_t>(app.secondaryAssetIndex)].valid) {
        const StageObject::Kind secondKind = markerKind == StageObject::Kind::Player
            ? StageObject::Kind::Opponent : StageObject::Kind::Player;
        for (size_t i = 0; i < app.previewStage.objects.size(); ++i)
            if (app.previewStage.objects[i].kind == secondKind) {
                app.secondaryMarkerIndex = static_cast<int>(i);
                break;
            }
        if (app.secondaryMarkerIndex < 0) {
            StageObject marker;
            marker.kind = secondKind;
            marker.name = secondKind == StageObject::Kind::Player ? "player" : "opponent";
            marker.position = {app.previewStage.objects[static_cast<size_t>(app.characterMarkerIndex)].position.x + 310.0f,
                               app.previewStage.objects[static_cast<size_t>(app.characterMarkerIndex)].position.y};
            marker.implicit = true;
            app.secondaryMarkerIndex = static_cast<int>(app.previewStage.objects.size());
            app.previewStage.objects.push_back(std::move(marker));
        }
    }
    app.songLab.viewDirty = true;
}

bool inGallery(const AtlasApp& app, int modIndex, const ModExplorerAsset& asset) {
    const std::string key = identity(*app.mods[static_cast<size_t>(modIndex)], asset);
    return std::binary_search(app.gallery.begin(), app.gallery.end(), key);
}

void toggleGalleryAsset(AtlasApp& app, int modIndex, const ModExplorerAsset& asset) {
    const LoadedMod& mod = *app.mods[static_cast<size_t>(modIndex)];
    const std::string key = identity(mod, asset);
    if (inGallery(app, modIndex, asset)) {
        app.gallery.erase(std::remove(app.gallery.begin(), app.gallery.end(), key), app.gallery.end());
        app.galleryRoots.erase(key);
        app.engineOverrides.erase(key);
    } else {
        app.gallery.push_back(key);
        std::sort(app.gallery.begin(), app.gallery.end());
        app.galleryRoots[key] = mod.catalog.root().u8string();
    }
    saveGallery(app);
}

bool openSourceFolder(AtlasApp& app, int modIndex, const std::string& virtualPath) {
    if (modIndex < 0 || modIndex >= static_cast<int>(app.mods.size())) return false;
    const LoadedMod& mod = *app.mods[static_cast<size_t>(modIndex)];
    const fs::path root = mod.catalog.root();
    std::error_code ec;
    fs::path folder;
    bool archive = false;
    if (fs::is_directory(root, ec)) {
        const auto resolved = virtualPath.empty() ? std::optional<fs::path>{}
                                                  : mod.catalog.vfs().resolve(virtualPath);
        folder = resolved && fs::is_regular_file(*resolved, ec) ? resolved->parent_path() : root;
    } else if (fs::is_regular_file(root, ec)) {
        folder = root.parent_path();
        archive = true;
    }
    if (folder.empty() || !fs::is_directory(folder, ec)) {
        setStatus(app, "La carpeta de origen ya no está disponible.",
                       "The source folder is no longer available.");
        return false;
    }
#ifdef _WIN32
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", folder.c_str(),
                                                 nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        setStatus(app, "No se pudo abrir la carpeta de origen.",
                       "Could not open the source folder.");
        return false;
    }
    setStatus(app, archive ? "Recurso dentro de ZIP; se abrió la carpeta del archivo."
                           : "Carpeta de origen: " + folder.u8string(),
                   archive ? "Resource inside ZIP; opened the archive folder."
                           : "Source folder: " + folder.u8string());
    return true;
#else
    setStatus(app, "Abrir carpetas solo está disponible en Windows.",
                   "Opening source folders is only available on Windows.");
    return false;
#endif
}

bool visible(const AtlasApp& app, int modIndex, const ModExplorerAsset& asset) {
    if (app.tab == 0 && asset.kind != ModExplorerAsset::Kind::Character) return false;
    if (app.tab == 1 && asset.kind != ModExplorerAsset::Kind::Stage) return false;
    if (app.galleryOnly && !inGallery(app, modIndex, asset)) return false;
    const std::string query = app.filter.data();
    return query.empty() || lower(asset.id).find(lower(query)) != std::string::npos ||
           lower(asset.sourcePath).find(lower(query)) != std::string::npos;
}

void refreshPreviewCharacter(AtlasApp& app);
void loadSavedPose(AtlasApp& app, int index);
void songLabSetActors(AtlasApp& app, const SongLabEntry& song);

void ensureCharacterAtlas(AtlasApp& app) {
    if (!app.previewCharacter.resolvedAtlas.empty() ||
        app.previewCharacter.resolvedImage.empty() || !app.rendererReady) return;
    const auto image = app.renderer.previewImage(app.previewCharacter.resolvedImage);
    if (!image.ok || image.width <= 0 || image.height <= 0) return;
    SparrowAtlas atlas;
    AtlasFrame frame;
    frame.name = "raw0000";
    frame.w = frame.frameW = image.width;
    frame.h = frame.frameH = image.height;
    atlas.frames.push_back(std::move(frame));
    const std::string key = "atlas-raw:" + app.previewCharacter.resolvedImage;
    app.atlases.putSparrow(key, std::move(atlas));
    app.previewCharacter.resolvedAtlas = key;
}

void configurePreview(AtlasApp& app) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || !asset->valid) return;
    app.songLab.viewDirty = true;
    app.animationIndex = 0;
    app.animateSourceIndex = 0;
    app.secondaryAnimateSourceIndex = 0;
    app.manualPreviewOverride = false;
    app.secondaryManualPreviewOverride = false;
    app.animationFilter.fill(0);
    app.previewCharacter = UniversalCharacter{};
    app.previewSecondaryCharacter = UniversalCharacter{};
    app.secondaryAssetIndex = -1;
    app.secondaryMarkerIndex = -1;
    app.secondaryAnimationIndex = 0;
    app.sheetViewMode = 0;
    app.liveSheetViews = {};
    app.shareStrumline = false;
    app.secondarySequence.clear();
    app.secondaryCustomFrames.clear();
    app.secondaryManualOrder.clear();
    app.secondaryTracedFrames.clear();
    app.sequence.clear();
    app.customFrames.clear();
    app.manualOrder.clear();
    app.tracedFrames.clear();
    app.savedPoseIndex = -1;
    app.selectedPoseIndex = -1;
    app.poseName.fill(0);
    app.traceAreas = app.tracing = false;
    app.sequenceClockMs = 0.0;
    app.sequenceToken = -1;
    app.scrubFrame = -1;
    app.flipX = app.flipY = app.pixelPerfect = false;
    app.previewZoom = 1.0f;
    app.previewPanX = 0.0f;
    app.previewPanY = 0.0f;
    app.characterFrameValid = false;
    app.previewPanning = false;
    app.previewBoundsValid = false;
    app.previewWheel = 0.0f;
    app.sheetZoom = 1.0f;
    app.sheetPanning = false;
    app.sheetBoundsValid = false;
    app.sheetWheel = 0.0f;
    app.autoFrame = false;
    app.showStageCharacters = false;
    app.stageActors[0] = app.stageActors[1] = app.stageActors[2] = -1;
    for (bool& pinned : app.stageActorPinned) pinned = false;
    app.selectedStageObject = 0;
    app.stageAnimationIndex = 0;
    app.stagePose = {};
    app.stagePoseKey.clear();
    app.stagePoseFrame = -1;
    app.stagePoseClock = 0.0;
    if (asset->kind == ModExplorerAsset::Kind::Stage) {
        app.previewStage = std::get<UniversalStage>(asset->parsed);
        prepareStageInspection(app.previewStage);
        chooseDefaultStageActors(app);
    } else {
        app.previewCharacter = std::get<UniversalCharacter>(asset->parsed);
        if (!asset->previewAnimations.empty()) app.previewCharacter.anims = asset->previewAnimations;
        ensureCharacterAtlas(app);
        for (AnimationDef& animation : app.previewCharacter.anims)
            animation.loop = animation.loop || app.loopPreview;
        if (app.previewCharacter.anims.empty()) {
            AnimationDef raw; raw.name = "raw sheet"; raw.allAtlasFrames = true; raw.loop = true;
            app.previewCharacter.anims.push_back(raw);
        }
        buildCharacterStage(app);
        app.frameGeometry = true;
        app.pickFrames = !AtlasStore::isAnimatePath(app.previewCharacter.resolvedAtlas);
        if (app.pickFrames && app.rendererReady && !app.previewCharacter.resolvedImage.empty()) {
            const auto sheet = app.renderer.previewImage(app.previewCharacter.resolvedImage);
            if (sheet.ok && sheet.width > 0 && sheet.height > 0) {
                const float fit = std::min(800.0f / sheet.width, 295.0f / sheet.height);
                app.sheetZoom = std::clamp(0.5f / std::max(0.001f, fit), 1.0f, 32.0f);
            }
        }
    }
    app.animator.reset(app.previewStage);
    app.animator.setPlaying(app.playing);
    if (asset->kind == ModExplorerAsset::Kind::Character && !app.previewCharacter.anims.empty())
        app.animator.play(static_cast<size_t>(app.characterMarkerIndex), 0);
    if (asset->kind == ModExplorerAsset::Kind::Character) {
        const auto saved = app.savedAnimations.find(identity(*app.mods[static_cast<size_t>(app.selectedMod)], *asset));
        if (saved != app.savedAnimations.end() && !saved->second.empty()) loadSavedPose(app, 0);
    }
    if (asset->kind == ModExplorerAsset::Kind::Stage && app.songLab.chartLoaded &&
        app.selectedMod >= 0 &&
        app.songLab.activeRoot == app.mods[static_cast<size_t>(app.selectedMod)]->catalog.root().u8string())
        songLabSetActors(app, app.songLab.activeSong);
}

void loadSavedPose(AtlasApp& app, int index) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character) return;
    const std::string key = identity(*app.mods[static_cast<size_t>(app.selectedMod)], *asset);
    const auto saved = app.savedAnimations.find(key);
    app.savedPoseIndex = index;
    app.selectedPoseIndex = -1;
    app.manualOrder.clear();
    app.tracedFrames.clear();
    app.customFrames.clear();
    app.customFps = 24;
    app.animateSourceIndex = 0;
    std::string name = "Custom animation";
    if (saved != app.savedAnimations.end() && index >= 0 && index < static_cast<int>(saved->second.size())) {
        const SavedPoseAnimation& animation = saved->second[static_cast<size_t>(index)];
        app.manualOrder = animation.order;
        app.tracedFrames = animation.tracedFrames;
        app.customFps = animation.fps;
        name = animation.name;
        const UniversalCharacter& original = std::get<UniversalCharacter>(asset->parsed);
        const auto& authored = asset->previewAnimations.empty() ? original.anims : asset->previewAnimations;
        for (size_t i = 0; i < authored.size(); ++i)
            if (authored[i].name == animation.sourceAnimation) {
                app.animateSourceIndex = static_cast<int>(i);
                break;
            }
    } else {
        app.savedPoseIndex = -1;
    }
    for (const CustomPose& pose : app.manualOrder)
        if (!pose.traced) app.customFrames.push_back(pose.index);
    std::snprintf(app.poseName.data(), app.poseName.size(), "%s", name.c_str());
    refreshPreviewCharacter(app);
    if (!app.manualOrder.empty()) {
        app.animationIndex = static_cast<int>(app.previewCharacter.anims.size()) - 1;
        app.manualPreviewOverride = true;
        app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
    }
}

bool saveCurrentPose(AtlasApp& app, bool saveAsNew) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character || app.manualOrder.empty()) {
        setStatus(app, "Añade al menos un cuadro antes de guardar.", "Add at least one frame before saving.");
        return false;
    }
    std::string name = app.poseName.data();
    const size_t start = name.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) name = "Custom animation";
    else {
        const size_t end = name.find_last_not_of(" \t\r\n");
        name = name.substr(start, end - start + 1);
    }
    const std::string key = identity(*app.mods[static_cast<size_t>(app.selectedMod)], *asset);
    auto& animations = app.savedAnimations[key];
    SavedPoseAnimation saved;
    saved.name = name;
    if (AtlasStore::isAnimatePath(app.previewCharacter.resolvedAtlas)) {
        const UniversalCharacter& original = std::get<UniversalCharacter>(asset->parsed);
        const auto& authored = asset->previewAnimations.empty() ? original.anims : asset->previewAnimations;
        if (!authored.empty())
            saved.sourceAnimation = authored[static_cast<size_t>(std::clamp(app.animateSourceIndex,
                0, static_cast<int>(authored.size()) - 1))].name;
    }
    saved.fps = std::clamp(app.customFps, 1, 100);
    saved.order = app.manualOrder;
    saved.tracedFrames = app.tracedFrames;
    if (saveAsNew || app.savedPoseIndex < 0 || app.savedPoseIndex >= static_cast<int>(animations.size())) {
        if (animations.size() >= 32) {
            setStatus(app, "Límite de 32 animaciones por personaje.", "Limit of 32 animations per character.");
            return false;
        }
        animations.push_back(std::move(saved));
        app.savedPoseIndex = static_cast<int>(animations.size()) - 1;
    } else {
        animations[static_cast<size_t>(app.savedPoseIndex)] = std::move(saved);
    }
    if (!saveGallery(app)) {
        setStatus(app, "No se pudo guardar la biblioteca de animaciones.", "Could not save the animation library.");
        return false;
    }
    setStatus(app, "Animación guardada: " + name, "Animation saved: " + name);
    return true;
}

void deleteSavedPose(AtlasApp& app) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character) return;
    const std::string key = identity(*app.mods[static_cast<size_t>(app.selectedMod)], *asset);
    const auto found = app.savedAnimations.find(key);
    if (found == app.savedAnimations.end() || app.savedPoseIndex < 0 ||
        app.savedPoseIndex >= static_cast<int>(found->second.size())) return;
    found->second.erase(found->second.begin() + app.savedPoseIndex);
    if (found->second.empty()) app.savedAnimations.erase(found);
    app.savedPoseIndex = -1;
    if (!saveGallery(app)) {
        setStatus(app, "No se pudo guardar el borrado; revisa la biblioteca.",
                  "Could not save the deletion; check the library.");
        return;
    }
    loadSavedPose(app, -1);
    setStatus(app, "Animación eliminada; se limpió también la selección actual.",
               "Animation deleted; the current selection was cleared too.");
}

bool activateRenderer(AtlasApp& app, int modIndex) {
    if (app.renderMod == modIndex && app.rendererReady) return true;
    if (app.rendererReady) app.renderer.shutdown();
    app.rendererReady = false;
    app.renderMod = -1;
    app.atlases = AtlasStore{};
    if (modIndex < 0 || modIndex >= static_cast<int>(app.mods.size())) return false;
    app.atlases.readText = [&app, modIndex](const std::string& path) {
        const auto result = app.mods[static_cast<size_t>(modIndex)]->catalog.vfs().readText(path);
        return result ? *result : std::string();
    };
    std::string error;
    app.rendererReady = app.renderer.init([&app, modIndex](const std::string& path) {
        const auto found = app.mods[static_cast<size_t>(modIndex)]->catalog.vfs().resolve(path);
        return found ? found->u8string() : std::string();
    }, &error);
    if (!app.rendererReady) {
        setStatus(app, "Error al preparar la vista: " + error, "Preview setup failed: " + error);
        return false;
    }
    app.renderMod = modIndex;
    return true;
}

void selectAsset(AtlasApp& app, int modIndex, int assetIndex) {
    const ModExplorerAsset* previous = selectedAsset(app);
    const bool previousStage = previous && previous->kind == ModExplorerAsset::Kind::Stage;
    std::array<std::string, 3> actorNames;
    std::array<bool, 3> pinned{};
    if (previousStage && app.selectedMod >= 0) {
        const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
        for (int role = 0; role < 3; ++role) {
            const int actor = app.stageActors[role];
            if (actor >= 0 && actor < static_cast<int>(assets.size()))
                actorNames[static_cast<size_t>(role)] = assets[static_cast<size_t>(actor)].id;
            pinned[static_cast<size_t>(role)] = app.stageActorPinned[role];
        }
    }
    if (app.selectedMod != modIndex) app.characterBackdrop = -1;
    app.selectedMod = modIndex;
    app.selectedAsset = assetIndex;
    activateRenderer(app, modIndex);
    configurePreview(app);
    const ModExplorerAsset* current = selectedAsset(app);
    if (!previousStage || !current || current->kind != ModExplorerAsset::Kind::Stage) return;
    const auto& assets = app.mods[static_cast<size_t>(modIndex)]->catalog.assets();
    for (int role = 0; role < 3; ++role) {
        const std::string& name = actorNames[static_cast<size_t>(role)];
        if (name.empty()) {
            if (pinned[static_cast<size_t>(role)]) {
                app.stageActors[role] = -1;
                app.stageActorPinned[role] = true;
            }
            continue;
        }
        for (size_t i = 0; i < assets.size(); ++i)
            if (assets[i].valid && assets[i].kind == ModExplorerAsset::Kind::Character &&
                lower(assets[i].id) == lower(name)) {
                app.stageActors[role] = static_cast<int>(i);
                app.stageActorPinned[role] = pinned[static_cast<size_t>(role)];
                break;
            }
    }
    app.animator.reset(app.previewStage);
}

void switchActiveCharacter(AtlasApp& app) {
    if (app.secondaryAssetIndex < 0 || app.secondaryMarkerIndex < 0) return;
    std::swap(app.selectedAsset, app.secondaryAssetIndex);
    std::swap(app.previewCharacter, app.previewSecondaryCharacter);
    std::swap(app.characterMarkerIndex, app.secondaryMarkerIndex);
    std::swap(app.animationIndex, app.secondaryAnimationIndex);
    std::swap(app.animateSourceIndex, app.secondaryAnimateSourceIndex);
    std::swap(app.manualPreviewOverride, app.secondaryManualPreviewOverride);
    std::swap(app.resolvedAnimateSheet, app.secondaryResolvedAnimateSheet);
    std::swap(app.sequence, app.secondarySequence);
    std::swap(app.customFrames, app.secondaryCustomFrames);
    std::swap(app.manualOrder, app.secondaryManualOrder);
    std::swap(app.tracedFrames, app.secondaryTracedFrames);
    std::swap(app.poseName, app.secondaryPoseName);
    std::swap(app.savedPoseIndex, app.secondarySavedPoseIndex);
    std::swap(app.selectedPoseIndex, app.secondarySelectedPoseIndex);
    std::swap(app.customFps, app.secondaryCustomFps);
    std::swap(app.scrubFrame, app.secondaryScrubFrame);
    std::swap(app.loopPreview, app.secondaryLoopPreview);
    std::swap(app.showSheet, app.secondaryShowSheet);
    std::swap(app.frameGeometry, app.secondaryFrameGeometry);
    std::swap(app.pickFrames, app.secondaryPickFrames);
    std::swap(app.flipX, app.secondaryFlipX);
    std::swap(app.flipY, app.secondaryFlipY);
    std::swap(app.pixelPerfect, app.secondaryPixelPerfect);
    std::swap(app.sheetZoom, app.secondarySheetZoom);
    std::swap(app.liveSheetViews[0], app.liveSheetViews[1]);
    std::swap(app.sequenceClockMs, app.secondarySequenceClockMs);
    std::swap(app.sequenceToken, app.secondarySequenceToken);
    std::swap(app.gridSize, app.secondaryGridSize);
    std::swap(app.traceAreas, app.secondaryTraceAreas);
    std::swap(app.tracing, app.secondaryTracing);
    std::swap(app.traceStart, app.secondaryTraceStart);
    app.animationFilter.fill(0);
    app.songLab.viewDirty = true;
}

void setSecondaryCharacter(AtlasApp& app, int index) {
    if (app.selectedMod < 0 || !selectedAsset(app) ||
        selectedAsset(app)->kind != ModExplorerAsset::Kind::Character) return;
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    if (index == app.selectedAsset || index < -1 || index >= static_cast<int>(assets.size()) ||
        (index >= 0 && (!assets[static_cast<size_t>(index)].valid ||
         assets[static_cast<size_t>(index)].kind != ModExplorerAsset::Kind::Character))) return;
    const Vec2 currentPosition = app.characterMarkerIndex >= 0 &&
        app.characterMarkerIndex < static_cast<int>(app.previewStage.objects.size())
        ? app.previewStage.objects[static_cast<size_t>(app.characterMarkerIndex)].position
        : Vec2{640.0f, 360.0f};
    app.secondaryAssetIndex = index;
    app.previewSecondaryCharacter = UniversalCharacter{};
    app.secondaryAnimationIndex = 0;
    app.secondaryAnimateSourceIndex = 0;
    app.secondaryManualPreviewOverride = false;
    app.secondarySequence.clear();
    app.secondaryCustomFrames.clear();
    app.secondaryManualOrder.clear();
    app.secondaryTracedFrames.clear();
    app.secondaryPoseName.fill(0);
    app.secondarySavedPoseIndex = -1;
    app.secondarySelectedPoseIndex = -1;
    app.secondaryScrubFrame = -1;
    app.shareStrumline = false;
    app.sheetViewMode = 0;
    if (index >= 0) {
        const ModExplorerAsset& second = assets[static_cast<size_t>(index)];
        app.previewSecondaryCharacter = std::get<UniversalCharacter>(second.parsed);
        if (!second.previewAnimations.empty())
            app.previewSecondaryCharacter.anims = second.previewAnimations;
        for (AnimationDef& animation : app.previewSecondaryCharacter.anims)
            animation.loop = animation.loop || app.secondaryLoopPreview;
        if (app.previewSecondaryCharacter.anims.empty()) {
            AnimationDef raw;
            raw.name = "raw sheet";
            raw.allAtlasFrames = true;
            raw.loop = true;
            app.previewSecondaryCharacter.anims.push_back(raw);
        }
        std::swap(app.previewCharacter, app.previewSecondaryCharacter);
        ensureCharacterAtlas(app);
        std::swap(app.previewCharacter, app.previewSecondaryCharacter);
    }
    buildCharacterStage(app);
    if (app.characterMarkerIndex >= 0 &&
        app.characterMarkerIndex < static_cast<int>(app.previewStage.objects.size()))
        app.previewStage.objects[static_cast<size_t>(app.characterMarkerIndex)].position = currentPosition;
    app.animator.reset(app.previewStage);
    app.animator.play(static_cast<size_t>(app.characterMarkerIndex), app.animationIndex);
    if (app.secondaryMarkerIndex >= 0)
        app.animator.play(static_cast<size_t>(app.secondaryMarkerIndex), app.secondaryAnimationIndex);
    app.songLab.viewDirty = true;
}

void ensureSelection(AtlasApp& app) {
    const ModExplorerAsset* current = selectedAsset(app);
    if (current && visible(app, app.selectedMod, *current)) return;
    for (int pass = 0; pass < 3; ++pass) {
        for (size_t offset = 0; offset < app.mods.size(); ++offset) {
            const size_t mod = app.selectedMod >= 0 && app.selectedMod < static_cast<int>(app.mods.size())
                ? (static_cast<size_t>(app.selectedMod) + offset) % app.mods.size() : offset;
            const auto& assets = app.mods[mod]->catalog.assets();
            for (size_t index = 0; index < assets.size(); ++index) {
                const auto& candidate = assets[index];
                if (!visible(app, static_cast<int>(mod), candidate)) continue;
                if (pass == 0 && (!candidate.valid || candidate.previewImage.empty())) continue;
                if (pass == 1 && !candidate.valid) continue;
                selectAsset(app, static_cast<int>(mod), static_cast<int>(index));
                return;
            }
        }
    }
    app.selectedMod = app.selectedAsset = -1;
}

void rememberTabState(AtlasApp& app) {
    if (app.tab < 0 || app.tab > 1 || app.selectedMod < 0) return;
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset) return;
    AtlasTabState& saved = app.tabStates[app.tab];
    saved.valid = true;
    saved.root = normalizedRoot(app.mods[static_cast<size_t>(app.selectedMod)]->catalog.root());
    saved.assetKey = asset->key;
    saved.stage = app.previewStage;
    saved.character = app.previewCharacter;
    saved.secondaryCharacter = app.previewSecondaryCharacter;
    saved.sequence = app.sequence;
    saved.customFrames = app.customFrames;
    saved.manualOrder = app.manualOrder;
    saved.tracedFrames = app.tracedFrames;
    saved.secondarySequence = app.secondarySequence;
    saved.secondaryCustomFrames = app.secondaryCustomFrames;
    saved.secondaryManualOrder = app.secondaryManualOrder;
    saved.secondaryTracedFrames = app.secondaryTracedFrames;
    saved.poseName = app.poseName;
    saved.secondaryPoseName = app.secondaryPoseName;
    saved.animationIndex = app.animationIndex;
    saved.animateSourceIndex = app.animateSourceIndex;
    saved.secondaryAnimateSourceIndex = app.secondaryAnimateSourceIndex;
    saved.manualPreviewOverride = app.manualPreviewOverride;
    saved.secondaryManualPreviewOverride = app.secondaryManualPreviewOverride;
    saved.selectedStageObject = app.selectedStageObject;
    saved.stageAnimationIndex = app.stageAnimationIndex;
    for (int role = 0; role < 3; ++role) {
        saved.stageActors[role] = app.stageActors[role];
        saved.stageActorPinned[role] = app.stageActorPinned[role];
    }
    saved.characterBackdrop = app.characterBackdrop;
    saved.characterStageRole = app.characterStageRole;
    saved.characterMarkerIndex = app.characterMarkerIndex;
    saved.secondaryMarkerIndex = app.secondaryMarkerIndex;
    saved.secondaryAssetIndex = app.secondaryAssetIndex;
    saved.secondaryAnimationIndex = app.secondaryAnimationIndex;
    saved.secondarySavedPoseIndex = app.secondarySavedPoseIndex;
    saved.secondarySelectedPoseIndex = app.secondarySelectedPoseIndex;
    saved.secondaryCustomFps = app.secondaryCustomFps;
    saved.secondaryScrubFrame = app.secondaryScrubFrame;
    saved.secondarySequenceClockMs = app.secondarySequenceClockMs;
    saved.secondarySequenceToken = app.secondarySequenceToken;
    saved.sheetViewMode = app.sheetViewMode;
    saved.showLiveSheet = app.showLiveSheet;
    saved.shareStrumline = app.shareStrumline;
    saved.sharedStrumlineLine = app.sharedStrumlineLine;
    saved.savedPoseIndex = app.savedPoseIndex;
    saved.selectedPoseIndex = app.selectedPoseIndex;
    saved.customFps = app.customFps;
    saved.previewZoom = app.previewZoom;
    saved.previewPanX = app.previewPanX;
    saved.previewPanY = app.previewPanY;
    saved.characterFrameValid = app.characterFrameValid;
    saved.characterFrame = app.characterFrame;
    saved.characterFrameWidth = app.characterFrameWidth;
    saved.characterFrameHeight = app.characterFrameHeight;
    saved.previewSpeed = app.previewSpeed;
    saved.previewBpm = app.previewBpm;
    saved.sheetZoom = app.sheetZoom;
    saved.characterUseStagePosition = app.characterUseStagePosition;
    saved.showStageCharacters = app.showStageCharacters;
    saved.autoFrame = app.autoFrame;
    saved.showSheet = app.showSheet;
    saved.frameGeometry = app.frameGeometry;
    saved.pickFrames = app.pickFrames;
    saved.flipX = app.flipX;
    saved.flipY = app.flipY;
    saved.pixelPerfect = app.pixelPerfect;
    saved.loopPreview = app.loopPreview;
    saved.secondaryLoopPreview = app.secondaryLoopPreview;
    saved.secondaryShowSheet = app.secondaryShowSheet;
    saved.secondaryFrameGeometry = app.secondaryFrameGeometry;
    saved.secondaryPickFrames = app.secondaryPickFrames;
    saved.secondaryFlipX = app.secondaryFlipX;
    saved.secondaryFlipY = app.secondaryFlipY;
    saved.secondaryPixelPerfect = app.secondaryPixelPerfect;
    saved.resolvedAnimateSheet = app.resolvedAnimateSheet;
    saved.secondaryResolvedAnimateSheet = app.secondaryResolvedAnimateSheet;
    saved.secondarySheetZoom = app.secondarySheetZoom;
    saved.liveSheetViews = app.liveSheetViews;
    saved.secondaryGridSize = app.secondaryGridSize;
    saved.secondaryTraceAreas = app.secondaryTraceAreas;
    saved.secondaryTracing = app.secondaryTracing;
    saved.secondaryTraceStart = app.secondaryTraceStart;
}

bool restoreTabState(AtlasApp& app, int tab) {
    if (tab < 0 || tab > 1) return false;
    const AtlasTabState& saved = app.tabStates[tab];
    if (!saved.valid) return false;
    for (size_t mod = 0; mod < app.mods.size(); ++mod) {
        if (normalizedRoot(app.mods[mod]->catalog.root()) != saved.root) continue;
        const auto& assets = app.mods[mod]->catalog.assets();
        for (size_t index = 0; index < assets.size(); ++index) {
            if (assets[index].key != saved.assetKey) continue;
            if ((tab == 0) != (assets[index].kind == ModExplorerAsset::Kind::Character)) return false;
            selectAsset(app, static_cast<int>(mod), static_cast<int>(index));
            app.previewStage = saved.stage;
            app.previewCharacter = saved.character;
            app.previewSecondaryCharacter = saved.secondaryCharacter;
            app.sequence = saved.sequence;
            app.customFrames = saved.customFrames;
            app.manualOrder = saved.manualOrder;
            app.tracedFrames = saved.tracedFrames;
            app.secondarySequence = saved.secondarySequence;
            app.secondaryCustomFrames = saved.secondaryCustomFrames;
            app.secondaryManualOrder = saved.secondaryManualOrder;
            app.secondaryTracedFrames = saved.secondaryTracedFrames;
            app.poseName = saved.poseName;
            app.secondaryPoseName = saved.secondaryPoseName;
            app.animationIndex = saved.animationIndex;
            app.animateSourceIndex = saved.animateSourceIndex;
            app.secondaryAnimateSourceIndex = saved.secondaryAnimateSourceIndex;
            app.manualPreviewOverride = saved.manualPreviewOverride;
            app.secondaryManualPreviewOverride = saved.secondaryManualPreviewOverride;
            app.selectedStageObject = saved.selectedStageObject;
            app.stageAnimationIndex = saved.stageAnimationIndex;
            for (int role = 0; role < 3; ++role) {
                app.stageActors[role] = saved.stageActors[role];
                app.stageActorPinned[role] = saved.stageActorPinned[role];
            }
            app.characterBackdrop = saved.characterBackdrop;
            app.characterStageRole = saved.characterStageRole;
            app.characterMarkerIndex = saved.characterMarkerIndex;
            app.secondaryMarkerIndex = saved.secondaryMarkerIndex;
            app.secondaryAssetIndex = saved.secondaryAssetIndex;
            app.secondaryAnimationIndex = saved.secondaryAnimationIndex;
            app.secondarySavedPoseIndex = saved.secondarySavedPoseIndex;
            app.secondarySelectedPoseIndex = saved.secondarySelectedPoseIndex;
            app.secondaryCustomFps = saved.secondaryCustomFps;
            app.secondaryScrubFrame = saved.secondaryScrubFrame;
            app.secondarySequenceClockMs = saved.secondarySequenceClockMs;
            app.secondarySequenceToken = saved.secondarySequenceToken;
            app.sheetViewMode = saved.sheetViewMode;
            app.showLiveSheet = saved.showLiveSheet;
            app.shareStrumline = saved.shareStrumline;
            app.sharedStrumlineLine = saved.sharedStrumlineLine;
            app.savedPoseIndex = saved.savedPoseIndex;
            app.selectedPoseIndex = saved.selectedPoseIndex;
            app.customFps = saved.customFps;
            app.previewZoom = saved.previewZoom;
            app.previewPanX = saved.previewPanX;
            app.previewPanY = saved.previewPanY;
            app.characterFrameValid = saved.characterFrameValid;
            app.characterFrame = saved.characterFrame;
            app.characterFrameWidth = saved.characterFrameWidth;
            app.characterFrameHeight = saved.characterFrameHeight;
            app.previewSpeed = saved.previewSpeed;
            app.previewBpm = saved.previewBpm;
            app.sheetZoom = saved.sheetZoom;
            app.characterUseStagePosition = saved.characterUseStagePosition;
            app.showStageCharacters = saved.showStageCharacters;
            app.autoFrame = saved.autoFrame;
            app.showSheet = saved.showSheet;
            app.frameGeometry = saved.frameGeometry;
            app.pickFrames = saved.pickFrames;
            app.flipX = saved.flipX;
            app.flipY = saved.flipY;
            app.pixelPerfect = saved.pixelPerfect;
            app.loopPreview = saved.loopPreview;
            app.secondaryLoopPreview = saved.secondaryLoopPreview;
            app.secondaryShowSheet = saved.secondaryShowSheet;
            app.secondaryFrameGeometry = saved.secondaryFrameGeometry;
            app.secondaryPickFrames = saved.secondaryPickFrames;
            app.secondaryFlipX = saved.secondaryFlipX;
            app.secondaryFlipY = saved.secondaryFlipY;
            app.secondaryPixelPerfect = saved.secondaryPixelPerfect;
            app.resolvedAnimateSheet = saved.resolvedAnimateSheet;
            app.secondaryResolvedAnimateSheet = saved.secondaryResolvedAnimateSheet;
            app.secondarySheetZoom = saved.secondarySheetZoom;
            app.liveSheetViews = saved.liveSheetViews;
            app.secondaryGridSize = saved.secondaryGridSize;
            app.secondaryTraceAreas = saved.secondaryTraceAreas;
            app.secondaryTracing = saved.secondaryTracing;
            app.secondaryTraceStart = saved.secondaryTraceStart;
            app.animator.reset(app.previewStage);
            if (tab == 0 && !app.previewCharacter.anims.empty())
                app.animator.play(static_cast<size_t>(app.characterMarkerIndex),
                    std::clamp(app.animationIndex, 0, static_cast<int>(app.previewCharacter.anims.size()) - 1));
            app.songLab.viewDirty = true;
            return true;
        }
    }
    return false;
}

bool addMod(AtlasApp& app, const fs::path& root) {
    const std::string canonical = normalizedRoot(root);
    for (size_t i = 0; i < app.mods.size(); ++i) {
        if (app.mods[i]->singleFile.empty() &&
            normalizedRoot(app.mods[i]->catalog.root()) == canonical) {
            setStatus(app, "Ese mod ya está cargado.", "That mod is already loaded.");
            return true;
        }
    }
    if (app.mods.size() >= 3) {
        setStatus(app, "Límite de tres mods: quita uno antes de añadir otro.", "Three-mod limit: remove one before adding another.");
        return false;
    }
    auto mod = std::make_unique<LoadedMod>();
    if (!mod->catalog.scan(root)) {
        setStatus(app, mod->catalog.error() == "The selected path is not a readable folder or ZIP archive." ? "La ruta no corresponde a una carpeta o ZIP legible." : mod->catalog.error(), mod->catalog.error());
        return false;
    }
    mod->label = root.filename().u8string();
    if (mod->label.empty()) mod->label = root.u8string();
    const size_t count = mod->catalog.assets().size();
    app.mods.push_back(std::move(mod));
    app.stageScriptCache.clear();
    setStatus(app, std::to_string(count) + " recursos encontrados en " + app.mods.back()->label, std::to_string(count) + " resources found in " + app.mods.back()->label);
    ensureSelection(app);
    return true;
}

bool addSingleResource(AtlasApp& app, const fs::path& file) {
    const std::string canonical = normalizedRoot(file);
    for (size_t i = 0; i < app.mods.size(); ++i)
        if (!app.mods[i]->singleFile.empty() &&
            app.mods[i]->singleFile == canonical) {
            selectAsset(app, static_cast<int>(i), 0);
            app.tab = app.mods[i]->catalog.assets()[0].kind == ModExplorerAsset::Kind::Character ? 0 : 1;
            app.selectTabOnNextFrame = true;
            return true;
        }
    if (app.mods.size() >= 3) {
        setStatus(app, "Límite de tres fuentes cargadas; quita una antes de añadir otra.",
                       "Three loaded sources maximum; remove one before adding another.");
        return false;
    }
    auto mod = std::make_unique<LoadedMod>();
    if (!mod->catalog.scanSingle(file)) {
        setStatus(app, "No se pudo abrir ese recurso: " + mod->catalog.error(),
                       "Could not open that resource: " + mod->catalog.error());
        return false;
    }
    mod->singleFile = canonical;
    mod->label = file.stem().u8string() + " · " +
        (app.spanish ? "archivo" : "file");
    app.mods.push_back(std::move(mod));
    app.stageScriptCache.clear();
    const int index = static_cast<int>(app.mods.size()) - 1;
    const ModExplorerAsset& asset = app.mods.back()->catalog.assets().front();
    app.tab = asset.kind == ModExplorerAsset::Kind::Character ? 0 : 1;
    app.selectTabOnNextFrame = true;
    app.galleryOnly = false;
    selectAsset(app, index, 0);
    setStatus(app, "Recurso individual cargado: " + asset.id,
                   "Single resource loaded: " + asset.id);
    return true;
}

bool addManualResource(AtlasApp& app) {
    const fs::path definition = fs::u8path(app.manualDefinition.data());
    const fs::path image = fs::u8path(app.manualImage.data());
    const fs::path atlas = fs::u8path(app.manualAtlas.data());
    const fs::path spritemap = fs::u8path(app.manualSpritemap.data());
    const fs::path icon = fs::u8path(app.manualIcon.data());
    const fs::path primary = definition.empty() ? image : definition;
    if (primary.empty()) {
        setStatus(app, "Elige una definición o una imagen PNG.",
                       "Choose a definition or a PNG image.");
        return false;
    }
    if (app.mods.size() >= 3) {
        setStatus(app, "Límite de tres fuentes cargadas.", "Three loaded sources maximum.");
        return false;
    }
    auto mod = std::make_unique<LoadedMod>();
    if (!mod->catalog.scanSingle(primary, !image.empty() || !atlas.empty())) {
        setStatus(app, "No se pudo cargar el recurso manual: " + mod->catalog.error(),
                       "Could not load manual resource: " + mod->catalog.error());
        return false;
    }
    std::string error;
    if (mod->catalog.assets().front().kind == ModExplorerAsset::Kind::Character &&
        !mod->catalog.configureManualCharacter(image, atlas, spritemap, icon, error)) {
        setStatus(app, "No se pudieron enlazar los archivos: " + error,
                       "Could not link manual files: " + error);
        return false;
    }
    mod->singleFile = normalizedRoot(primary);
    mod->label = primary.stem().u8string() + " · " +
        (app.spanish ? "manual" : "manual");
    app.mods.push_back(std::move(mod));
    app.stageScriptCache.clear();
    const int index = static_cast<int>(app.mods.size()) - 1;
    const ModExplorerAsset& asset = app.mods.back()->catalog.assets().front();
    if (app.manualEngine > 0)
        app.engineOverrides[identity(*app.mods.back(), asset)] = app.manualEngine;
    app.tab = asset.kind == ModExplorerAsset::Kind::Character ? 0 : 1;
    app.selectTabOnNextFrame = true;
    app.galleryOnly = false;
    selectAsset(app, index, 0);
    setStatus(app, "Recurso manual cargado: " + asset.id,
                   "Manual resource loaded: " + asset.id);
    saveGallery(app);
    return true;
}

void removeMod(AtlasApp& app, int index) {
    if (index < 0 || index >= static_cast<int>(app.mods.size())) return;
    if (app.quickRenderMod >= 0) app.quickRenderer.shutdown();
    app.quickRenderMod = app.quickMod = app.quickAsset = -1;
    app.quickConfiguredMod = app.quickConfiguredAsset = -1;
    app.songLab.songs.clear();
    app.songLab.indexed = false;
    app.songLab.indexedSignature.clear();
    app.songLab.selected = -1;
    if (app.rendererReady) app.renderer.shutdown();
    app.rendererReady = false;
    app.renderMod = -1;
    app.mods.erase(app.mods.begin() + index);
    app.stageScriptCache.clear();
    app.selectedMod = app.selectedAsset = -1;
    ensureSelection(app);
}

std::string firstMissingGalleryKey(const AtlasApp& app) {
    for (const std::string& key : app.gallery) {
        const auto found = app.galleryRoots.find(key);
        std::error_code ec;
        if (found == app.galleryRoots.end() ||
            !fs::exists(fs::u8path(found->second), ec) || ec) return key;
    }
    return {};
}

void replaceGalleryEntry(AtlasApp& app, int modIndex, const ModExplorerAsset& asset) {
    if (app.pendingRecoveryKey.empty()) return;
    const std::string oldKey = app.pendingRecoveryKey;
    const std::string newKey = identity(*app.mods[static_cast<size_t>(modIndex)], asset);
    for (std::string& key : app.gallery) if (key == oldKey) key = newKey;
    std::sort(app.gallery.begin(), app.gallery.end());
    app.gallery.erase(std::unique(app.gallery.begin(), app.gallery.end()), app.gallery.end());
    const auto engine = app.engineOverrides.find(oldKey);
    if (engine != app.engineOverrides.end()) {
        app.engineOverrides[newKey] = engine->second;
        app.engineOverrides.erase(engine);
    }
    const auto custom = app.savedAnimations.find(oldKey);
    if (custom != app.savedAnimations.end()) {
        app.savedAnimations[newKey] = std::move(custom->second);
        app.savedAnimations.erase(custom);
    }
    app.galleryRoots.erase(oldKey);
    app.galleryRoots[newKey] = app.mods[static_cast<size_t>(modIndex)]->catalog.root().u8string();
    app.pendingRecoveryKey.clear();
    saveGallery(app);
    setStatus(app, "Entrada de galería reconectada.", "Gallery entry relinked.");
}

void SDLCALL dialogSelected(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<AtlasApp*>(userdata);
    std::lock_guard<std::mutex> lock(app->dialogMutex);
    app->dialogPath = filelist && filelist[0] ? filelist[0] : "";
    app->dialogReady = true;
}

std::string safeName(std::string value) {
    for (char& c : value)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '-';
    while (!value.empty() && value.front() == '-') value.erase(value.begin());
    return value.empty() ? "resource" : value;
}

fs::path availablePath(const fs::path& desired, bool directory) {
    std::error_code ec;
    if (!fs::exists(desired, ec)) return desired;
    for (int n = 2; n < 10000; ++n) {
        fs::path candidate = directory
            ? fs::path(desired.u8string() + "-" + std::to_string(n))
            : desired.parent_path() / fs::u8path(desired.stem().u8string() + "-" + std::to_string(n) + desired.extension().u8string());
        if (!fs::exists(candidate, ec)) return candidate;
    }
    return {};
}

bool safeVirtualPath(const std::string& value) {
    const fs::path path = fs::u8path(value);
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}

bool copyVirtual(const Vfs& vfs, const std::string& virtualPath, const fs::path& output) {
    if (!safeVirtualPath(virtualPath)) return false;
    const auto bytes = vfs.readBytes(virtualPath);
    if (!bytes) return false;
    const fs::path destination = output / fs::u8path(virtualPath);
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) return false;
    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    return static_cast<bool>(file);
}

bool exportOriginal(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || !asset->valid) return false;
    const LoadedMod& mod = *app.mods[static_cast<size_t>(app.selectedMod)];
    const fs::path output = availablePath(folder / fs::u8path(safeName(asset->id) + "-source"), true);
    if (output.empty()) return false;
    std::set<std::string> files;
    files.insert(asset->sourcePath);
    fs::path sibling = fs::u8path(asset->sourcePath);
    for (const char* ext : {".lua", ".hx"}) {
        sibling.replace_extension(ext);
        if (mod.catalog.vfs().exists(sibling.u8string())) files.insert(sibling.u8string());
    }
    auto addSprite = [&files, &mod](const std::string& image, const std::string& atlas) {
        if (!image.empty()) files.insert(image);
        if (!atlas.empty()) files.insert(atlas);
        if (!atlas.empty() && AtlasStore::isAnimatePath(atlas)) {
            const std::string parent = fs::u8path(atlas).parent_path().u8string();
            for (const Vfs::Entry& entry : mod.catalog.vfs().allEntries()) {
                if (fs::u8path(entry.virtualPath).parent_path().u8string() != parent) continue;
                const std::string name = lower(fs::u8path(entry.virtualPath).filename().u8string());
                if (name.rfind("spritemap", 0) == 0) files.insert(entry.virtualPath);
            }
        }
    };
    if (asset->kind == ModExplorerAsset::Kind::Character) {
        const auto& character = std::get<UniversalCharacter>(asset->parsed);
        addSprite(character.resolvedImage, character.resolvedAtlas);
    } else {
        const auto& stage = std::get<UniversalStage>(asset->parsed);
        for (const StageObject& object : stage.objects)
            addSprite(object.resolvedImage, object.resolvedAtlas);
        for (const std::string& script : stage.behaviorScripts)
            if (mod.catalog.vfs().exists(script)) files.insert(script);
    }
    std::error_code ec;
    fs::create_directories(output, ec);
    if (ec) return false;
    int copied = 0;
    std::vector<std::string> missing;
    for (const std::string& path : files) {
        if (copyVirtual(mod.catalog.vfs(), path, output)) ++copied;
        else missing.push_back(path);
    }
    std::ofstream report(output / "EXPORT_INFO.txt", std::ios::binary);
    report << "Funkin Atlas source resource package / Paquete de recursos originales\n";
    report << "Source / Origen: " << mod.catalog.root().u8string() << "\n";
    report << "Definition / Definición: " << asset->sourcePath << "\n";
    report << "Files copied / Archivos copiados: " << copied << "\n";
    report << "This package preserves available source files. Runtime scripts and external dependencies may need their original mod.\n";
    report << "Este paquete conserva los archivos originales disponibles. Los scripts y dependencias externas pueden necesitar el mod de origen.\n";
    for (const std::string& path : missing) report << "Missing / Faltante: " << path << "\n";
    setStatus(app, output.u8string() + " · " + std::to_string(copied) + " archivos, faltan " + std::to_string(missing.size()), output.u8string() + " · " + std::to_string(copied) + " files, missing " + std::to_string(missing.size()));
    return copied > 0;
}

std::string imageRelativePath(const std::string& virtualPath) {
    const std::string normalized = Vfs::normalize(virtualPath);
    const std::string lowered = lower(normalized);
    const size_t imageAt = lowered.rfind("images/");
    if (imageAt == std::string::npos) return {};
    const std::string relative = normalized.substr(imageAt + 7);
    return safeVirtualPath(relative) ? relative : std::string();
}

std::string imageKey(const std::string& virtualPath) {
    std::string relative = imageRelativePath(virtualPath);
    if (relative.empty()) return {};
    fs::path path = fs::u8path(relative);
    if (path.has_extension()) path.replace_extension();
    return Vfs::normalize(path.u8string());
}

std::string resolveCharacterIcon(const Vfs& vfs, const std::string& definition,
                                 const std::string& declared) {
    if (declared.empty()) return {};
    std::string key = lower(Vfs::normalize(declared));
    if (key.rfind("images/icons/", 0) == 0) key.erase(0, 13);
    else if (key.rfind("icons/", 0) == 0) key.erase(0, 6);
    if (fs::u8path(key).extension() == ".png") key.resize(key.size() - 4);
    if (key.rfind("icon-", 0) == 0) key.erase(0, 5);
    const std::string source = lower(Vfs::normalize(definition));
    const size_t data = source.find("/data/");
    const std::string prefix = data == std::string::npos ? std::string() : source.substr(0, data);
    int bestScore = -1;
    std::string best;
    for (const Vfs::Entry& entry : vfs.allEntries()) {
        const std::string path = lower(Vfs::normalize(entry.virtualPath));
        if (entry.isDir || fs::u8path(path).extension() != ".png" ||
            path.find("/icons/") == std::string::npos) continue;
        const std::string name = fs::u8path(path).filename().u8string();
        const std::string folder = fs::u8path(path).parent_path().filename().u8string();
        int score = name == key + ".png" ? 50 :
                    name == "icon-" + key + ".png" ? 40 :
                    name == "icon.png" && folder == key ? 30 : -1;
        if (score < 0) continue;
        if (path.rfind("__atlas_manual_", 0) == 0) score += 200;
        if (!prefix.empty() && path.rfind(prefix + "/", 0) == 0) score += 100;
        if (score > bestScore) {
            bestScore = score;
            best = entry.virtualPath;
        }
    }
    return best;
}

bool writeText(const fs::path& path, const std::string& contents) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(output);
}

bool copyToPackage(const Vfs& vfs, const std::string& source, const fs::path& destination) {
    const auto bytes = vfs.readBytes(source, 128u * 1024u * 1024u);
    if (!bytes) return false;
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) return false;
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()));
    return static_cast<bool>(output);
}

void copyVisualAsset(const Vfs& vfs, const std::string& source, const fs::path& package,
                     std::set<std::string>& copied, std::vector<std::string>& missing) {
    if (source.empty() || !copied.insert(source).second) return;
    const std::string relative = imageRelativePath(source);
    if (relative.empty() || !copyToPackage(vfs, source, package / "images" / fs::u8path(relative)))
        missing.push_back(source);
}

void copyAtlasSet(const Vfs& vfs, const std::string& image, const std::string& atlas,
                  const fs::path& package, std::set<std::string>& copied,
                  std::vector<std::string>& missing) {
    copyVisualAsset(vfs, image, package, copied, missing);
    copyVisualAsset(vfs, atlas, package, copied, missing);
    if (!AtlasStore::isAnimatePath(atlas)) return;
    const std::string folder = lower(Vfs::normalize(fs::u8path(atlas).parent_path().u8string()));
    for (const Vfs::Entry& entry : vfs.allEntries()) {
        if (lower(Vfs::normalize(fs::u8path(entry.virtualPath).parent_path().u8string())) != folder) continue;
        const std::string name = lower(fs::u8path(entry.virtualPath).filename().u8string());
        if (name.rfind("spritemap", 0) == 0)
            copyVisualAsset(vfs, entry.virtualPath, package, copied, missing);
    }
}

std::string removePsychImportBanner(std::string value) {
    const std::string banner = "<!-- Importado de Psych Engine por Funkin Mod Lab -->\n";
    const size_t at = value.find(banner);
    if (at != std::string::npos) value.erase(at, banner.size());
    return value;
}

std::string luaQuoted(const std::string& value) {
    std::string result = "'";
    for (char c : value) {
        if (c == '\\' || c == '\'') result.push_back('\\');
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else result.push_back(c);
    }
    result.push_back('\'');
    return result;
}

std::string verifyExportedResource(const fs::path& package,
                                   const ModExplorerAsset& source,
                                   const std::string& exportedId) {
    ModExplorerCatalog checked;
    if (!checked.scan(package)) return checked.error();
    for (const ModExplorerAsset& result : checked.assets()) {
        if (result.kind != source.kind || lower(result.id) != lower(exportedId)) continue;
        if (!result.valid) return result.error.empty() ? "Exported definition did not parse" : result.error;
        if (source.kind == ModExplorerAsset::Kind::Character) {
            const UniversalCharacter& character = std::get<UniversalCharacter>(result.parsed);
            if (character.resolvedImage.empty()) return "Exported character image could not be resolved";
            if (character.anims.empty()) return "Exported character has no playable animations";
        } else {
            const UniversalStage& authored = std::get<UniversalStage>(source.parsed);
            const UniversalStage& stage = std::get<UniversalStage>(result.parsed);
            int sourceSprites = 0, exportedSprites = 0;
            for (const StageObject& object : authored.objects)
                if (object.kind == StageObject::Kind::Sprite && !object.spritePath.empty())
                    ++sourceSprites;
            for (const StageObject& object : stage.objects)
                if (object.kind == StageObject::Kind::Sprite && !object.resolvedImage.empty())
                    ++exportedSprites;
            if (exportedSprites < sourceSprites)
                return std::to_string(sourceSprites - exportedSprites) +
                    " stage sprite(s) are missing after export";
        }
        return {};
    }
    return "Exported definition was not found in the package";
}

bool exportEngine(AtlasApp& app, const fs::path& folder, bool psych) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || !asset->valid || app.selectedMod < 0) return false;
    const LoadedMod& mod = *app.mods[static_cast<size_t>(app.selectedMod)];
    const Vfs& vfs = mod.catalog.vfs();
    const std::string id = safeName(asset->id);
    const std::string modId = safeName(mod.label) + "-atlas";
    const fs::path output = availablePath(folder / fs::u8path(id + (psych ? "-psych" : "-codename")), true);
    if (output.empty()) return false;
    const fs::path package = output / "mods" / fs::u8path(modId);
    std::error_code ec;
    fs::create_directories(package, ec);
    if (ec) {
        setStatus(app, "No se pudo crear el paquete.", "Could not create the package.");
        return false;
    }
    std::set<std::string> copied;
    std::vector<std::string> missing;
    std::vector<std::string> notes;
    bool definitionWritten = false;
    const bool native = psych
        ? asset->format == ModExplorerAsset::Format::PsychJson ||
          asset->format == ModExplorerAsset::Format::PsychLua
        : asset->format == ModExplorerAsset::Format::CodenameXml;
    if (asset->kind == ModExplorerAsset::Kind::Character) {
        const UniversalCharacter& original = std::get<UniversalCharacter>(asset->parsed);
        std::string spriteKey = AtlasStore::isAnimatePath(original.resolvedAtlas)
            ? imageKey(fs::u8path(original.resolvedAtlas).parent_path().u8string())
            : imageKey(original.resolvedImage);
        if (spriteKey.empty()) spriteKey = original.spriteAtlas;
        const fs::path definition = package /
            (psych ? fs::path("characters") : fs::path("data/characters")) /
            fs::u8path(id + (psych ? ".json" : ".xml"));
        if (native && vfs.exists(asset->sourcePath))
            definitionWritten = copyToPackage(vfs, asset->sourcePath, definition);
        else if (psych) {
            json source;
            source["image"] = spriteKey;
            source["scale"] = original.scale;
            source["sing_duration"] = original.holdTime;
            source["healthicon"] = original.icon;
            source["position"] = {original.position.x, original.position.y};
            source["camera_position"] = {original.camOffset.x, original.camOffset.y};
            source["flip_x"] = original.flipX;
            source["no_antialiasing"] = !original.antialiasing;
            source["animations"] = json::array();
            for (const AnimationDef& animation : original.anims)
                source["animations"].push_back({
                    {"anim", animation.name}, {"name", animation.atlasPrefix},
                    {"fps", animation.fps}, {"loop", animation.loop},
                    {"indices", animation.indices},
                    {"offsets", {animation.offset.x, animation.offset.y}}
                });
            if (original.healthColor.size() == 7 && original.healthColor[0] == '#') {
                try {
                    source["healthbar_colors"] = {
                        std::stoi(original.healthColor.substr(1, 2), nullptr, 16),
                        std::stoi(original.healthColor.substr(3, 2), nullptr, 16),
                        std::stoi(original.healthColor.substr(5, 2), nullptr, 16)
                    };
                } catch (...) {}
            }
            definitionWritten = writeText(definition, source.dump(2));
        } else {
            UniversalCharacter character = original;
            character.spriteAtlas = spriteKey;
            definitionWritten = writeText(definition, removePsychImportBanner(
                emitCodenameCharacter(character, {})));
        }
        copyAtlasSet(vfs, original.resolvedImage, original.resolvedAtlas, package, copied, missing);
        if (!original.icon.empty()) {
            const std::string found = resolveCharacterIcon(vfs, asset->sourcePath, original.icon);
            std::string iconId = fs::u8path(original.icon).stem().u8string();
            if (iconId.rfind("icon-", 0) == 0) iconId.erase(0, 5);
            const fs::path target = package / "images" / "icons" /
                fs::u8path((psych ? "icon-" : "") + safeName(iconId) + ".png");
            if (found.empty() || !copyToPackage(vfs, found, target)) {
                notes.push_back("Icon not found: " + original.icon);
                missing.push_back("icon: " + original.icon);
            }
        }
        if (original.resolvedImage.empty()) {
            notes.push_back("Character image was not resolved.");
            missing.push_back("character image: " + original.spriteAtlas);
        }
        if (asset->format == ModExplorerAsset::Format::VSliceJson)
            notes.push_back("V-Slice animations with a secondary assetPath need manual review.");
    } else {
        const UniversalStage& stage = std::get<UniversalStage>(asset->parsed);
        const fs::path definition = package /
            (psych ? fs::path("stages") : fs::path("data/stages")) /
            fs::u8path(id + (psych ? ".json" : ".xml"));
        if (native && lower(fs::u8path(asset->sourcePath).extension().u8string()) != ".lua" &&
            vfs.exists(asset->sourcePath))
            definitionWritten = copyToPackage(vfs, asset->sourcePath, definition);
        else if (psych) {
            json source;
            source["defaultZoom"] = stage.zoom;
            for (const StageObject& object : stage.objects) {
                const char* field = object.kind == StageObject::Kind::Player ? "boyfriend" :
                    object.kind == StageObject::Kind::Opponent ? "opponent" :
                    object.kind == StageObject::Kind::Girlfriend ? "girlfriend" : nullptr;
                if (field) source[field] = {object.position.x, object.position.y};
                if (object.kind == StageObject::Kind::Girlfriend && object.alpha <= 0.01f)
                    source["hide_girlfriend"] = true;
            }
            definitionWritten = writeText(definition, source.dump(2));
        } else {
            PsychStageImport converted;
            converted.stage = stage;
            for (StageObject& object : converted.stage.objects) {
                if (!object.resolvedImage.empty()) {
                    const std::string key = imageKey(object.resolvedImage);
                    if (!key.empty()) object.spritePath = key;
                }
                if (object.kind == StageObject::Kind::Girlfriend && object.alpha <= 0.01f)
                    converted.hidesGirlfriend = true;
            }
            definitionWritten = writeText(definition,
                removePsychImportBanner(emitCodenameStage(converted)));
        }
        for (const StageObject& object : stage.objects) {
            copyAtlasSet(vfs, object.resolvedImage, object.resolvedAtlas, package, copied, missing);
            if (!object.spritePath.empty() && object.resolvedImage.empty()) {
                notes.push_back("Unresolved stage sprite: " + object.spritePath);
                missing.push_back("stage sprite: " + object.spritePath);
            }
        }
        if (native) {
            fs::path sibling = fs::u8path(asset->sourcePath);
            for (const char* extension : {".lua", ".hx"}) {
                sibling.replace_extension(extension);
                if (!vfs.exists(sibling.u8string())) continue;
                const fs::path target = package /
                    (psych ? fs::path("stages") : fs::path("data/stages")) /
                    fs::u8path(id + extension);
                if (!copyToPackage(vfs, sibling.u8string(), target))
                    missing.push_back(sibling.u8string());
            }
        } else if (psych) {
            std::string script = "function onCreate()\n";
            bool front = false;
            for (const StageObject& object : stage.objects) {
                if (object.kind == StageObject::Kind::Player ||
                    object.kind == StageObject::Kind::Opponent ||
                    object.kind == StageObject::Kind::Girlfriend) {
                    front = true;
                    continue;
                }
                if (object.kind != StageObject::Kind::Sprite &&
                    object.kind != StageObject::Kind::Box) continue;
                const std::string tag = object.name.empty() ? "object" : object.name;
                const std::string spriteKey = !object.resolvedImage.empty()
                    ? imageKey(object.resolvedImage) : object.spritePath;
                const bool animated = !object.anims.empty();
                script += "  " + std::string(animated ? "makeAnimatedLuaSprite(" : "makeLuaSprite(") +
                    luaQuoted(tag) + ", " + (spriteKey.empty() ? "nil" : luaQuoted(spriteKey)) +
                    ", " + std::to_string(object.position.x) + ", " +
                    std::to_string(object.position.y) + ")\n";
                if (object.kind == StageObject::Kind::Box)
                    script += "  makeGraphic(" + luaQuoted(tag) + ", " +
                        std::to_string(static_cast<int>(object.width)) + ", " +
                        std::to_string(static_cast<int>(object.height)) + ", " +
                        luaQuoted(object.color.empty() ? "FFFFFF" : object.color) + ")\n";
                for (const AnimationDef& animation : object.anims)
                    script += "  addAnimationByPrefix(" + luaQuoted(tag) + ", " +
                        luaQuoted(animation.name) + ", " + luaQuoted(animation.atlasPrefix) +
                        ", " + std::to_string(animation.fps) + ", " +
                        (animation.loop ? "true" : "false") + ")\n";
                if (object.scale.x != 1.0f || object.scale.y != 1.0f)
                    script += "  scaleObject(" + luaQuoted(tag) + ", " +
                        std::to_string(object.scale.x) + ", " +
                        std::to_string(object.scale.y) + ")\n";
                if (object.scroll.x != 1.0f || object.scroll.y != 1.0f)
                    script += "  setScrollFactor(" + luaQuoted(tag) + ", " +
                        std::to_string(object.scroll.x) + ", " +
                        std::to_string(object.scroll.y) + ")\n";
                if (object.alpha != 1.0f)
                    script += "  setProperty(" + luaQuoted(tag + ".alpha") + ", " +
                        std::to_string(object.alpha) + ")\n";
                script += "  addLuaSprite(" + luaQuoted(tag) + ", " +
                    (front ? "true" : "false") + ")\n";
            }
            script += "end\n";
            bool hasBeat = false;
            for (const StageObject& object : stage.objects)
                if ((object.type == "beat" || object.type == "onbeat") && !object.anims.empty())
                    hasBeat = true;
            if (hasBeat) {
                script += "\nfunction onBeatHit()\n";
                for (const StageObject& object : stage.objects) {
                    if ((object.type != "beat" && object.type != "onbeat") ||
                        object.anims.empty()) continue;
                    const int interval = std::max(1, object.beatInterval);
                    const int remainder = ((-object.beatOffset % interval) + interval) % interval;
                    script += "  if curBeat % " + std::to_string(interval) + " == " +
                        std::to_string(remainder) + " then objectPlayAnimation(" +
                        luaQuoted(object.name) + ", " +
                        luaQuoted(object.anims.front().name) + ", true) end\n";
                    if (object.anims.size() > 1)
                        notes.push_back("Beat animation sequence needs manual review: " + object.name);
                }
                script += "end\n";
            }
            if (!writeText(package / "stages" / fs::u8path(id + ".lua"), script))
                missing.push_back(id + ".lua");
            notes.push_back("Generated static Psych stage script; dynamic behavior needs manual porting.");
        } else {
            notes.push_back("Generated static Codename stage XML; runtime Lua behavior needs manual porting.");
        }
    }
    if (!native) {
        fs::path sibling = fs::u8path(asset->sourcePath);
        for (const char* extension : {".lua", ".hx"}) {
            sibling.replace_extension(extension);
            if (!vfs.exists(sibling.u8string())) continue;
            if (!copyToPackage(vfs, sibling.u8string(),
                               package / "source-scripts" / fs::u8path(id + extension)))
                missing.push_back(sibling.u8string());
        }
    }
    std::string guide = "Funkin Atlas export\nVISUAL/DECORATIVE CONTENT ONLY. Cross-engine scripts, cutscenes and mechanics are not translated.\n"
        "SOLO CONTENIDO VISUAL/DECORATIVO. No se traducen scripts, cinemáticas ni mecánicas entre motores.\nSource: " +
        mod.catalog.root().u8string() +
        "\nEngine: " + (psych ? "Psych Engine" : "Codename Engine") +
        "\n\nInstall: copy the folder inside mods/ into your game's mods/ directory.\n" +
        "Instalación: copia la carpeta dentro de mods/ al directorio mods/ del juego.\n" +
        "No original mod files were changed. Dynamic scripts are not translated automatically.\n" +
        "No se modificaron archivos del mod original. Los scripts dinámicos no se traducen automáticamente.\n";
    for (const std::string& note : notes) guide += "\nNote: " + note;
    for (const std::string& path : missing) guide += "\nMissing: " + path;
    writeText(output / "INSTALL.txt", guide);
    if (!definitionWritten) {
        setStatus(app, "No se pudo escribir la definición; revisa " + output.u8string(),
                  "Could not write the definition; check " + output.u8string());
        return false;
    }
    const std::string verification = verifyExportedResource(output, *asset, id);
    if (!verification.empty()) {
        setStatus(app, "Exportación incompleta (se conserva en disco): " + verification + " · " + output.u8string(),
                       "Incomplete export (kept on disk): " + verification + " · " + output.u8string());
        return false;
    }
    setStatus(app, "Exportado a " + output.u8string() + " · " + std::to_string(missing.size()) + " recursos faltantes",
               "Exported to " + output.u8string() + " · " + std::to_string(missing.size()) + " missing resources");
    return true;
}

bool exportVSlice(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || !asset->valid) return false;
    const LoadedMod& mod = *app.mods[static_cast<size_t>(app.selectedMod)];
    DiagnosticSink sink(DiagnosticScope::WorkspaceInventory);
    Result<VSliceCharacterPackage> built = Result<VSliceCharacterPackage>::fail("unsupported");
    if (asset->kind == ModExplorerAsset::Kind::Character) {
        const auto& character = std::get<UniversalCharacter>(asset->parsed);
        VSliceCharacterSource source;
        source.character = &character;
        source.characterId = safeName(asset->id);
        source.modId = safeName(mod.label) + "-atlas";
        source.imageVirtualPath = character.resolvedImage;
        source.atlasVirtualPath = character.resolvedAtlas;
        source.iconVirtualPath = resolveCharacterIcon(mod.catalog.vfs(), asset->sourcePath,
            character.icon.empty() ? character.id : character.icon);
        built = buildVSliceCharacterPackage(source, sink);
    } else {
        const auto& stage = std::get<UniversalStage>(asset->parsed);
        VSliceStageSource source;
        source.stage = &stage;
        source.stageId = safeName(asset->id);
        source.modId = safeName(mod.label) + "-atlas";
        for (const StageObject& object : stage.objects) {
            source.objectImagePaths.push_back(object.resolvedImage);
            source.objectAtlasPaths.push_back(object.resolvedAtlas);
        }
        built = buildVSliceStagePackage(source, sink);
    }
    if (!built) {
        setStatus(app, "No se pudo preparar la exportación V-Slice: " + built.error(), "Could not prepare V-Slice export: " + built.error());
        return false;
    }
    VSliceExportFile limits;
    limits.path = "EXPORT_LIMITS.txt";
    limits.text = "Funkin Atlas - FML Tool\nVisual/decorative content only. Scripts, cutscenes and mechanics are not translated to V-Slice.\n"
                  "Solo contenido visual/decorativo. No se traducen scripts, cinemáticas ni mecánicas a V-Slice.\n";
    limits.role = VSliceFileRole::Report;
    built.value().files.push_back(std::move(limits));
    const fs::path path = availablePath(folder / fs::u8path(safeName(asset->id) + "-vslice.zip"), false);
    std::string error;
    fs::path written;
    if (path.empty() || !writeVSliceCharacterZip(path, built.value(), mod.catalog.vfs(), error, &written)) {
        setStatus(app, error.empty() ? "Falló la exportación V-Slice." : "Falló la exportación V-Slice: " + error, error.empty() ? "V-Slice export failed." : "V-Slice export failed: " + error);
        return false;
    }
    const std::string verification = verifyExportedResource(written, *asset, safeName(asset->id));
    if (!verification.empty()) {
        setStatus(app, "ZIP incompleto (se conserva en disco): " + verification + " · " + written.u8string(),
                       "Incomplete ZIP (kept on disk): " + verification + " · " + written.u8string());
        return false;
    }
    setStatus(app, written.u8string() + " · " + std::to_string(built.value().warnings.size()) + " avisos, " + std::to_string(built.value().unsupported.size()) + " incompatibles", written.u8string() + " · " + std::to_string(built.value().warnings.size()) + " warnings, " + std::to_string(built.value().unsupported.size()) + " unsupported");
    return true;
}

const char* stageObjectKind(StageObject::Kind kind);
bool exportStageObjectGifFallback(AtlasApp& app, const StageObject& object,
                                  const AnimationDef& animation,
                                  const fs::path& target, const std::string& reason);

GifPrepareResult prepareStageObjectGif(AtlasApp& app, const StageObject& object,
                                      const AnimationDef& animation) {
    GifPrepareResult result;
    if (object.resolvedImage.empty()) {
        result.error = "No resolved stage image";
        return result;
    }
    const Vfs& vfs = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.vfs();
    const auto image = vfs.readBytes(object.resolvedImage, 128u * 1024u * 1024u);
    if (!image) {
        result.error = "Could not read stage image";
        return result;
    }
    const SparrowAtlas* sparrow = nullptr;
    const AnimateAtlas* animate = nullptr;
    if (AtlasStore::isAnimatePath(object.resolvedAtlas))
        animate = app.atlases.getAnimate(object.resolvedAtlas);
    else
        sparrow = app.atlases.get(object.resolvedAtlas);
    return prepareMountedAnimationGif(*image, animation, sparrow, animate,
                                      object.flipX, false, animation.loop);
}

void drawStageAnimationPanel(AtlasApp& app, const LoadedMod& mod,
                             const ModExplorerAsset& asset) {
    if (app.selectedStageObject < 0 ||
        app.selectedStageObject >= static_cast<int>(app.previewStage.objects.size())) return;
    const StageObject& object = app.previewStage.objects[static_cast<size_t>(app.selectedStageObject)];
    if (app.stageAnimationIndex < 0 ||
        app.stageAnimationIndex >= static_cast<int>(object.anims.size())) return;
    const AnimationDef& animation = object.anims[static_cast<size_t>(app.stageAnimationIndex)];
    const std::string key = identity(mod, asset) + "#" +
        std::to_string(app.selectedStageObject) + "/" + std::to_string(app.stageAnimationIndex);
    if (app.stagePoseKey != key) {
        app.stagePose = prepareStageObjectGif(app, object, animation);
        app.stagePoseKey = key;
        app.stagePoseFrame = -1;
        app.stagePoseClock = 0.0;
    }
    ImGui::BeginChild("##stage-animation-preview", ImVec2(0.0f, 225.0f), true);
    if (app.stagePose.ok && !app.stagePose.animation.frames.empty()) {
        const PreparedGifAnimation& poses = app.stagePose.animation;
        if (app.playing) app.stagePoseClock += ImGui::GetIO().DeltaTime;
        const int count = static_cast<int>(poses.frames.size());
        const int frame = app.stageLoopPreview
            ? static_cast<int>(app.stagePoseClock * poses.fps) % count
            : std::min(count - 1, static_cast<int>(app.stagePoseClock * poses.fps));
        if (!app.stagePoseTexture) glGenTextures(1, &app.stagePoseTexture);
        if (frame != app.stagePoseFrame) {
            glBindTexture(GL_TEXTURE_2D, app.stagePoseTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            object.antialiasing ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            object.antialiasing ? GL_LINEAR : GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, poses.width, poses.height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE,
                         poses.frames[static_cast<size_t>(frame)].rgba.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            app.stagePoseFrame = frame;
        }
        const float fit = std::min(240.0f / poses.width, 165.0f / poses.height);
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(app.stagePoseTexture)),
                     ImVec2(poses.width * fit, poses.height * fit));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", animation.name.c_str());
        ImGui::TextDisabled("%d / %d · %d fps", frame + 1, count, poses.fps);
        ImGui::TextDisabled("%s", animation.loop
            ? (app.spanish ? "Repite" : "Looping")
            : (app.spanish ? "Una vez" : "One shot"));
        ImGui::TextWrapped("Atlas: %s", object.resolvedAtlas.c_str());
        ImGui::EndGroup();
    } else {
        ImGui::TextWrapped("%s: %s",
            app.spanish ? "No se pudo montar la animación" : "Could not mount animation",
            app.stagePose.error.c_str());
    }
    ImGui::EndChild();
}

bool exportStageSheet(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Stage ||
        app.selectedStageObject < 0 ||
        app.selectedStageObject >= static_cast<int>(app.previewStage.objects.size())) return false;
    const StageObject& object = app.previewStage.objects[static_cast<size_t>(app.selectedStageObject)];
    if (app.stageAnimationIndex < 0 ||
        app.stageAnimationIndex >= static_cast<int>(object.anims.size())) return false;
    const AnimationDef& animation = object.anims[static_cast<size_t>(app.stageAnimationIndex)];
    const GifPrepareResult prepared = prepareStageObjectGif(app, object, animation);
    MountedSheet sheet;
    std::string error;
    if (!packMountedSheet(prepared, animation, sheet, error)) {
        setStatus(app, "No se pudo montar la hoja: " + error,
                  "Could not build the sheet: " + error);
        return false;
    }
    const fs::path target = availablePath(folder / fs::u8path(
        safeName(asset->id) + "-" + safeName(object.name) + "-" +
        safeName(animation.name) + "-sheet"), true);
    std::error_code ec;
    if (target.empty() || !fs::create_directory(target, ec)) {
        setStatus(app, "No se pudo crear la carpeta de la hoja.",
                  "Could not create the sheet folder.");
        return false;
    }
    const GifWriteResult png = writeMountedPng(target / "sheet.png", sheet.png, 0);
    const bool textOk = writeText(target / "sheet.xml", sheet.xml) &&
                        writeText(target / "animation.txt", sheet.metadata);
    if (!png.ok || !textOk) {
        setStatus(app, "La hoja quedó incompleta; se conservaron los archivos escritos.",
                  "The sheet is incomplete; written files were retained.");
        return false;
    }
    setStatus(app, "Hoja guardada: " + target.u8string(),
              "Sheet saved: " + target.u8string());
    return true;
}

bool exportStageGif(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Stage ||
        app.selectedStageObject < 0 ||
        app.selectedStageObject >= static_cast<int>(app.previewStage.objects.size())) return false;
    const StageObject& object = app.previewStage.objects[static_cast<size_t>(app.selectedStageObject)];
    if (app.stageAnimationIndex < 0 ||
        app.stageAnimationIndex >= static_cast<int>(object.anims.size())) return false;
    const AnimationDef& animation = object.anims[static_cast<size_t>(app.stageAnimationIndex)];
    const GifPrepareResult prepared = prepareStageObjectGif(app, object, animation);
    const fs::path target = availablePath(folder / fs::u8path(
        safeName(asset->id) + "-" + safeName(object.name) + "-" +
        safeName(animation.name) + ".gif"), false);
    if (target.empty()) return false;
    if (!prepared.ok)
        return exportStageObjectGifFallback(app, object, animation, target, prepared.error);
    const GifWriteResult written = writeAnimatedGif(target, prepared.animation);
    if (!written.ok)
        return exportStageObjectGifFallback(app, object, animation, target, written.error);
    setStatus(app, written.ok ? "GIF guardado: " + target.u8string()
                              : "Falló el GIF: " + written.error,
                   written.ok ? "GIF saved: " + target.u8string()
                              : "GIF failed: " + written.error);
    return written.ok;
}

bool exportStageObjectPng(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Stage || app.selectedMod < 0 ||
        app.selectedStageObject < 0 ||
        app.selectedStageObject >= static_cast<int>(app.previewStage.objects.size())) return false;
    const StageObject& object = app.previewStage.objects[static_cast<size_t>(app.selectedStageObject)];
    if (object.resolvedImage.empty()) {
        setStatus(app, "Este objeto no tiene una imagen resuelta.", "This object has no resolved image.");
        return false;
    }
    const fs::path target = availablePath(folder / fs::u8path(
        safeName(asset->id) + "-" + safeName(object.name) + ".png"), false);
    if (target.empty()) return false;
    if (!object.anims.empty()) {
        const int selected = std::clamp(app.stageAnimationIndex, 0,
            static_cast<int>(object.anims.size()) - 1);
        const GifPrepareResult prepared = prepareStageObjectGif(app, object,
            object.anims[static_cast<size_t>(selected)]);
        if (prepared.ok && !prepared.animation.frames.empty()) {
            const size_t frame = static_cast<size_t>(std::clamp(
                app.animator.frameOf(static_cast<size_t>(app.selectedStageObject)), 0,
                static_cast<int>(prepared.animation.frames.size()) - 1));
            const GifWriteResult written = writeMountedPng(target, prepared.animation, frame);
            setStatus(app, written.ok ? "PNG del objeto guardado: " + target.u8string()
                                      : "Falló el PNG: " + written.error,
                           written.ok ? "Object PNG saved: " + target.u8string()
                                      : "PNG failed: " + written.error);
            return written.ok;
        }
    }
    const Vfs& vfs = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.vfs();
    const auto bytes = vfs.readBytes(object.resolvedImage, 128u * 1024u * 1024u);
    if (!bytes || bytes->empty()) {
        setStatus(app, "No se pudo leer el PNG original.", "Could not read the source PNG.");
        return false;
    }
    std::ofstream output(target, std::ios::binary);
    if (output) output.write(reinterpret_cast<const char*>(bytes->data()),
                             static_cast<std::streamsize>(bytes->size()));
    const bool ok = static_cast<bool>(output);
    setStatus(app, ok ? "PNG original guardado: " + target.u8string() : "No se pudo guardar el PNG.",
                   ok ? "Source PNG saved: " + target.u8string() : "Could not save the PNG.");
    return ok;
}

bool exportStageZip(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Stage || app.selectedMod < 0) return false;
    const LoadedMod& mod = *app.mods[static_cast<size_t>(app.selectedMod)];
    const Vfs& vfs = mod.catalog.vfs();
    const UniversalStage& stage = std::get<UniversalStage>(asset->parsed);
    std::map<std::string, std::vector<unsigned char>> entries;
    std::vector<std::string> warnings;
    auto addSource = [&](const std::string& path, const std::string& archivePath) {
        if (path.empty() || !safeVirtualPath(path) || !safeVirtualPath(archivePath)) return;
        const auto bytes = vfs.readBytes(path, 128u * 1024u * 1024u);
        if (bytes) entries[archivePath] = *bytes;
        else warnings.push_back("Missing: " + path);
    };
    auto addVisual = [&](const std::string& path) {
        if (path.empty()) return;
        const std::string relative = imageRelativePath(path);
        if (relative.empty()) {
            warnings.push_back("Image path outside images/: " + path);
            return;
        }
        const std::string archivePath = "images/" + relative;
        if (!entries.count(archivePath)) addSource(path, archivePath);
    };
    json manifest;
    manifest["stage"] = stage.id;
    manifest["source"] = mod.catalog.root().u8string();
    manifest["objects"] = json::array();
    for (size_t index = 0; index < stage.objects.size(); ++index) {
        const StageObject& object = stage.objects[index];
        json entry;
        entry["drawOrder"] = index + 1;
        entry["name"] = object.name;
        entry["kind"] = stageObjectKind(object.kind);
        entry["group"] = object.group;
        entry["declaredSprite"] = object.spritePath;
        entry["image"] = imageRelativePath(object.resolvedImage);
        entry["atlas"] = imageRelativePath(object.resolvedAtlas);
        entry["position"] = {object.position.x, object.position.y};
        entry["animations"] = json::array();
        for (const AnimationDef& animation : object.anims)
            entry["animations"].push_back(animation.name);
        manifest["objects"].push_back(std::move(entry));
        addVisual(object.resolvedImage);
        addVisual(object.resolvedAtlas);
        if (AtlasStore::isAnimatePath(object.resolvedAtlas)) {
            const std::string parent = lower(Vfs::normalize(
                fs::u8path(object.resolvedAtlas).parent_path().u8string()));
            for (const Vfs::Entry& file : vfs.allEntries()) {
                if (lower(Vfs::normalize(fs::u8path(file.virtualPath).parent_path().u8string())) != parent)
                    continue;
                const std::string name = lower(fs::u8path(file.virtualPath).filename().u8string());
                if (name.rfind("spritemap", 0) == 0) addVisual(file.virtualPath);
            }
        }
        if (!object.spritePath.empty() && object.resolvedImage.empty())
            warnings.push_back("Unresolved sprite: " + object.name + " -> " + object.spritePath);
    }
    addSource(asset->sourcePath, "source/" + Vfs::normalize(asset->sourcePath));
    fs::path sibling = fs::u8path(asset->sourcePath);
    for (const char* extension : {".lua", ".hx"}) {
        sibling.replace_extension(extension);
        if (vfs.exists(sibling.u8string()))
            addSource(sibling.u8string(), "source/" + Vfs::normalize(sibling.u8string()));
    }
    for (const std::string& script : stage.behaviorScripts)
        if (vfs.exists(script)) addSource(script, "source/" + Vfs::normalize(script));
    PsychStageImport recovered;
    recovered.stage = stage;
    const std::string xml = removePsychImportBanner(emitCodenameStage(recovered));
    entries["recovered-stage.xml"] = std::vector<unsigned char>(xml.begin(), xml.end());
    if (app.includeStageGifs) {
        int generated = 0;
        for (size_t index = 0; index < stage.objects.size(); ++index) {
            const StageObject& object = stage.objects[index];
            for (const AnimationDef& animation : object.anims) {
                if (generated >= 32) {
                    warnings.push_back("GIF limit reached: 32 animations per ZIP");
                    break;
                }
                const GifPrepareResult prepared = prepareStageObjectGif(app, object, animation);
                if (!prepared.ok) {
                    warnings.push_back("GIF omitted: " + object.name + "/" + animation.name +
                                       " (" + prepared.error + ")");
                    continue;
                }
                const fs::path temporary = availablePath(folder / fs::u8path(
                    ".atlas-" + safeName(asset->id) + "-" + std::to_string(index) +
                    "-" + safeName(animation.name) + ".gif"), false);
                if (temporary.empty()) continue;
                const GifWriteResult written = writeAnimatedGif(temporary, prepared.animation);
                if (!written.ok) {
                    warnings.push_back("GIF omitted: " + object.name + "/" + animation.name +
                                       " (" + written.error + ")");
                    continue;
                }
                std::ifstream input(temporary, std::ios::binary);
                std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                                  std::istreambuf_iterator<char>());
                std::error_code removeError;
                fs::remove(temporary, removeError);
                if (bytes.empty()) {
                    warnings.push_back("GIF omitted after write: " + object.name + "/" + animation.name);
                    continue;
                }
                const std::string name = "animations/" + std::to_string(index + 1) + "-" +
                    safeName(object.name) + "-" + safeName(animation.name) + ".gif";
                entries[name] = std::move(bytes);
                ++generated;
            }
        }
    }
    manifest["warnings"] = warnings;
    const std::string manifestText = manifest.dump(2);
    entries["manifest.json"] = std::vector<unsigned char>(manifestText.begin(), manifestText.end());
    const std::string guide =
        "Funkin Atlas stage resource archive\n" +
        std::string("Source: ") + mod.catalog.root().u8string() +
        "\nContains resolved PNG/spritesheet/atlas files, original definitions and a static recovered XML.\n" +
        "Includes animated GIFs only when the option was selected and preparation succeeded.\n" +
        "Unresolved assets and dynamic behavior are listed in manifest.json.\n";
    entries["README.txt"] = std::vector<unsigned char>(guide.begin(), guide.end());
    const fs::path target = availablePath(folder / fs::u8path(safeName(asset->id) + "-stage-assets.zip"), false);
    if (target.empty()) return false;
    const fs::path temporary = availablePath(fs::path(target.u8string() + ".tmp"), false);
    if (temporary.empty()) return false;
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, temporary.u8string().c_str(), 0)) {
        setStatus(app, "No se pudo abrir el ZIP.", "Could not open ZIP.");
        return false;
    }
    bool ok = true;
    for (const auto& [name, bytes] : entries) {
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            ok = false;
            break;
        }
    }
    if (ok) ok = mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    std::error_code ec;
    if (ok) fs::rename(temporary, target, ec);
    if (!ok || ec) {
        fs::remove(temporary, ec);
        setStatus(app, "No se pudo completar el ZIP.", "Could not finish ZIP.");
        return false;
    }
    setStatus(app, "ZIP guardado: " + target.u8string() +
               " · " + std::to_string(warnings.size()) + " avisos",
               "ZIP saved: " + target.u8string() +
               " · " + std::to_string(warnings.size()) + " warnings");
    return true;
}

#include "CharacterTools.hpp"

bool exportStageScenePng(AtlasApp& app, const fs::path& folder);
bool exportStageSceneGif(AtlasApp& app, const fs::path& folder);
bool exportCharacterPreviewGif(AtlasApp& app, const fs::path& folder);

void processDialog(AtlasApp& app) {
    std::string path;
    DialogAction action;
    {
        std::lock_guard<std::mutex> lock(app.dialogMutex);
        if (!app.dialogReady) return;
        app.dialogReady = false;
        path = std::move(app.dialogPath);
        action = app.dialogAction;
        app.dialogAction = DialogAction::None;
    }
    if (path.empty()) return;
    if (action == DialogAction::PairGifs) {
        if (app.secondaryAssetIndex < 0) return;
        const bool first = exportCharacterAnimation(app, DialogAction::AnimationGif,
                                                    fs::u8path(path));
        switchActiveCharacter(app);
        const bool second = exportCharacterAnimation(app, DialogAction::AnimationGif,
                                                     fs::u8path(path));
        switchActiveCharacter(app);
        setStatus(app, first && second ? "GIF individuales de ambos personajes guardados."
                                       : "No se pudieron guardar ambos GIF individuales.",
                       first && second ? "Separate GIFs for both characters saved."
                                       : "Could not save both separate GIFs.");
        return;
    }
    if (action == DialogAction::PairSceneGif) {
        exportCharacterPreviewGif(app, fs::u8path(path));
        return;
    }
    if (action == DialogAction::AnimationGif || action == DialogAction::AnimationPng ||
        action == DialogAction::MountedSheet) {
        exportCharacterAnimation(app, action, fs::u8path(path));
        return;
    }
    if (action == DialogAction::StageGif || action == DialogAction::StageSheet ||
        action == DialogAction::StageZip || action == DialogAction::StageObjectPng ||
        action == DialogAction::StageScenePng || action == DialogAction::StageSceneGif) {
        if (action == DialogAction::StageGif) exportStageGif(app, fs::u8path(path));
        else if (action == DialogAction::StageSheet) exportStageSheet(app, fs::u8path(path));
        else if (action == DialogAction::StageObjectPng) exportStageObjectPng(app, fs::u8path(path));
        else if (action == DialogAction::StageScenePng) exportStageScenePng(app, fs::u8path(path));
        else if (action == DialogAction::StageSceneGif) exportStageSceneGif(app, fs::u8path(path));
        else exportStageZip(app, fs::u8path(path));
        return;
    }
    if (action == DialogAction::Export) {
        if (app.exportTarget == 0) exportOriginal(app, fs::u8path(path));
        else if (app.exportTarget == 1) exportVSlice(app, fs::u8path(path));
        else exportEngine(app, fs::u8path(path), app.exportTarget == 2);
        return;
    }
    if (action == DialogAction::AddSingle) {
        addSingleResource(app, fs::u8path(path));
        return;
    }
    if (action == DialogAction::ManualDefinition || action == DialogAction::ManualImage ||
        action == DialogAction::ManualAtlas || action == DialogAction::ManualSpritemap ||
        action == DialogAction::ManualIcon) {
        auto& destination = action == DialogAction::ManualDefinition ? app.manualDefinition :
                            action == DialogAction::ManualImage ? app.manualImage :
                            action == DialogAction::ManualAtlas ? app.manualAtlas :
                            action == DialogAction::ManualSpritemap ? app.manualSpritemap : app.manualIcon;
        std::snprintf(destination.data(), destination.size(), "%s", path.c_str());
        return;
    }
    if (!addMod(app, fs::u8path(path))) return;
    if (action != DialogAction::Recover || app.pendingRecoveryKey.empty()) return;
    const std::string oldKey = app.pendingRecoveryKey.substr(app.pendingRecoveryKey.find('|') + 1);
    const auto separator = oldKey.find(':');
    const std::string oldPath = separator == std::string::npos ? oldKey : oldKey.substr(separator + 1);
    const std::string oldId = lower(fs::u8path(oldPath).stem().u8string());
    const ModExplorerAsset::Kind wanted = oldKey.rfind("character:", 0) == 0
        ? ModExplorerAsset::Kind::Character : ModExplorerAsset::Kind::Stage;
    for (size_t mod = 0; mod < app.mods.size(); ++mod) {
        if (normalizedRoot(app.mods[mod]->catalog.root()) != normalizedRoot(fs::u8path(path))) continue;
        const auto& assets = app.mods[mod]->catalog.assets();
        for (size_t index = 0; index < assets.size(); ++index) {
            if (assets[index].kind != wanted || lower(assets[index].id) != oldId) continue;
            selectAsset(app, static_cast<int>(mod), static_cast<int>(index));
            replaceGalleryEntry(app, static_cast<int>(mod), assets[index]);
            return;
        }
    }
    app.tab = wanted == ModExplorerAsset::Kind::Character ? 0 : 1;
    app.selectTabOnNextFrame = true;
    app.galleryOnly = false;
    ensureSelection(app);
    setStatus(app, "Sin coincidencia exacta. Elige el recurso nuevo y pulsa Reemplazar.", "No exact match. Select the new resource and choose Replace.");
}

CharacterBinding previewBinding(AtlasApp& app) {
    CharacterBinding binding;
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || app.selectedMod < 0) return binding;
    if (asset->kind == ModExplorerAsset::Kind::Character) {
        const StageObject::Kind kind = app.characterMarkerIndex >= 0 &&
            app.characterMarkerIndex < static_cast<int>(app.previewStage.objects.size())
            ? app.previewStage.objects[static_cast<size_t>(app.characterMarkerIndex)].kind
            : StageObject::Kind::Opponent;
        if (kind == StageObject::Kind::Player) binding.player = &app.previewCharacter;
        else if (kind == StageObject::Kind::Girlfriend) binding.girlfriend = &app.previewCharacter;
        else binding.opponent = &app.previewCharacter;
        if (app.secondaryAssetIndex >= 0 && app.secondaryMarkerIndex >= 0 &&
            app.secondaryMarkerIndex < static_cast<int>(app.previewStage.objects.size())) {
            const StageObject::Kind secondaryKind =
                app.previewStage.objects[static_cast<size_t>(app.secondaryMarkerIndex)].kind;
            if (secondaryKind == StageObject::Kind::Player) binding.player = &app.previewSecondaryCharacter;
            else if (secondaryKind == StageObject::Kind::Girlfriend)
                binding.girlfriend = &app.previewSecondaryCharacter;
            else binding.opponent = &app.previewSecondaryCharacter;
        }
        return binding;
    }
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    const UniversalCharacter** roles[3] = {&binding.player, &binding.opponent, &binding.girlfriend};
    for (int role = 0; role < 3; ++role) {
        const int index = app.stageActors[role];
        if (index < 0 || index >= static_cast<int>(assets.size())) continue;
        const ModExplorerAsset& actor = assets[static_cast<size_t>(index)];
        if (actor.valid && actor.kind == ModExplorerAsset::Kind::Character)
            *roles[role] = &std::get<UniversalCharacter>(actor.parsed);
    }
    return binding;
}

#include "SongLab.hpp"

ViewportTransform frameScene(const RenderList& list, bool character,
                             int width = 960, int height = 540) {
    ViewportTransform view;
    float left = 0, top = 0, right = 0, bottom = 0;
    bool any = false;
    const float centerX = kGameWidth * 0.5f;
    const float centerY = kGameHeight * 0.5f;
    for (const DrawCmd& cmd : list.cmds) {
        if (!cmd.visible || cmd.alpha <= 0.01f || cmd.w <= 0 || cmd.h <= 0) continue;
        float xs[4], ys[4], ws[4];
        drawCmdCorners(cmd, xs, ys, ws);
        for (int i = 0; i < 4; ++i) {
            float x = (xs[i] - (list.camera.x - centerX) * cmd.scrollX - centerX) * list.camera.zoom + centerX;
            float y = (ys[i] - (list.camera.y - centerY) * cmd.scrollY - centerY) * list.camera.zoom + centerY;
            if (cmd.zoomFactor != 1.0f && list.camera.zoom != 0.0f) {
                const float factor = std::max(1.0f + (list.camera.zoom - 1.0f) * cmd.zoomFactor, 0.0f) / list.camera.zoom;
                x = (x - centerX) * factor + centerX;
                y = (y - centerY) * factor + centerY;
            }
            if (list.camera.angle != 0.0f) {
                const float angle = list.camera.angle * 3.14159265358979323846f / 180.0f;
                const float dx = x - centerX, dy = y - centerY;
                x = dx * std::cos(angle) - dy * std::sin(angle) + centerX;
                y = dx * std::sin(angle) + dy * std::cos(angle) + centerY;
            }
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            if (!any) { left = right = x; top = bottom = y; any = true; }
            else { left = std::min(left, x); right = std::max(right, x);
                   top = std::min(top, y); bottom = std::max(bottom, y); }
        }
    }
    if (!any) return view;
    const float baseFit = std::max(0.001f, std::min(
        width / static_cast<float>(kGameWidth),
        height / static_cast<float>(kGameHeight)));
    view.zoom = std::clamp(std::min(width * 0.9f / baseFit / std::max(1.0f, right - left),
                                    height * 0.9f / baseFit / std::max(1.0f, bottom - top)),
                           0.12f, character ? 4.0f : 1.5f);
    view.panX = (centerX - (left + right) * 0.5f) * baseFit * view.zoom;
    view.panY = (centerY - (top + bottom) * 0.5f) * baseFit * view.zoom;
    return view;
}

ViewportTransform previewView(AtlasApp& app, const RenderList& list,
                              bool character, int width, int height) {
    ViewportTransform framed;
    if (character) {
        if (!app.characterFrameValid || app.characterFrameWidth != width ||
            app.characterFrameHeight != height) {
            app.characterFrame = frameScene(list, true, width, height);
            app.characterFrameWidth = width;
            app.characterFrameHeight = height;
            app.characterFrameValid = true;
        }
        framed = app.characterFrame;
    } else if (app.autoFrame) {
        framed = frameScene(list, false, width, height);
    }
    const float viewportScale = std::max(0.001f, std::min(width / 960.0f, height / 540.0f));
    ViewportTransform view;
    view.zoom = framed.zoom * app.previewZoom;
    view.panX = framed.panX * app.previewZoom + app.previewPanX * viewportScale;
    view.panY = framed.panY * app.previewZoom + app.previewPanY * viewportScale;
    return view;
}

void applyPreviewVisibility(const AtlasApp& app, RenderList& list) {
    const ModExplorerAsset* asset = selectedAsset(app);
    for (DrawCmd& command : list.cmds) {
        if (command.objectIndex < 0 ||
            command.objectIndex >= static_cast<int>(app.previewStage.objects.size())) continue;
        const StageObject& object = app.previewStage.objects[static_cast<size_t>(command.objectIndex)];
        const auto property = object.properties.find("visible");
        if (property != object.properties.end() && !property->second.asBool()) command.visible = false;
        if (asset && asset->kind == ModExplorerAsset::Kind::Stage && !app.showStageCharacters &&
            (object.kind == StageObject::Kind::Player ||
             object.kind == StageObject::Kind::Opponent ||
             object.kind == StageObject::Kind::Girlfriend)) command.visible = false;
    }
}

void applyCharacterVerticalFlip(const AtlasApp& app, RenderList& list) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character) return;
    const std::array<std::pair<int, bool>, 2> actors{{
        {app.characterMarkerIndex, app.flipY},
        {app.secondaryMarkerIndex, app.secondaryFlipY}
    }};
    for (const auto& [index, enabled] : actors) {
        if (index < 0 || !enabled) continue;
        float top = std::numeric_limits<float>::max();
        float bottom = std::numeric_limits<float>::lowest();
        for (const DrawCmd& command : list.cmds) {
            if (command.objectIndex != index || !command.visible) continue;
            float x[4], y[4], w[4];
            drawCmdCorners(command, x, y, w);
            for (float point : y) {
                top = std::min(top, point);
                bottom = std::max(bottom, point);
            }
        }
        if (top > bottom) continue;
        const float center = top + bottom;
        for (DrawCmd& command : list.cmds) {
            if (command.objectIndex != index) continue;
            if (command.quad) {
                for (float& point : command.qy) point = center - point;
            } else {
                command.y = center - command.y;
                command.mb = -command.mb;
                command.md = -command.md;
            }
        }
    }
}

GlRenderer::PreviewImage renderPreview(AtlasApp& app, int width, int height) {
    const ModExplorerAsset* asset = selectedAsset(app);
    app.previewHitValid = false;
    if (!asset || !asset->valid || !app.rendererReady) return {};
    const CharacterBinding binding = previewBinding(app);
    const bool songDriven = songLabUpdate(app, binding);
    const bool activeSong = songLabViewActive(app) && app.songLab.audio.playing();
    if (!activeSong ||
        (asset->kind == ModExplorerAsset::Kind::Character && songLabCharacterLine(app) == -1))
        updateCharacterSequence(app, ImGui::GetIO().DeltaTime * 1000.0f);
    if (asset->kind == ModExplorerAsset::Kind::Character &&
        app.secondaryAssetIndex >= 0 && app.secondaryMarkerIndex >= 0 &&
        (!activeSong || songLabCompanionLine(app) == -1)) {
        std::swap(app.previewCharacter, app.previewSecondaryCharacter);
        std::swap(app.characterMarkerIndex, app.secondaryMarkerIndex);
        std::swap(app.sequence, app.secondarySequence);
        std::swap(app.sequenceClockMs, app.secondarySequenceClockMs);
        std::swap(app.sequenceToken, app.secondarySequenceToken);
        std::swap(app.loopPreview, app.secondaryLoopPreview);
        std::swap(app.manualPreviewOverride, app.secondaryManualPreviewOverride);
        updateCharacterSequence(app, ImGui::GetIO().DeltaTime * 1000.0f);
        std::swap(app.manualPreviewOverride, app.secondaryManualPreviewOverride);
        std::swap(app.loopPreview, app.secondaryLoopPreview);
        std::swap(app.sequenceToken, app.secondarySequenceToken);
        std::swap(app.sequenceClockMs, app.secondarySequenceClockMs);
        std::swap(app.sequence, app.secondarySequence);
        std::swap(app.characterMarkerIndex, app.secondaryMarkerIndex);
        std::swap(app.previewCharacter, app.previewSecondaryCharacter);
    }
    if (!songDriven) {
        app.animator.setPlaying(app.playing);
        app.animator.update(app.previewStage, binding, app.atlases,
                            ImGui::GetIO().DeltaTime * 1000.0f * app.previewSpeed,
                            app.previewBpm);
    }
    RenderList list;
    buildStageRenderList(app.previewStage, binding, app.atlases,
                         app.renderer, list, &app.animator);
    applyPreviewVisibility(app, list);
    applyCharacterVerticalFlip(app, list);
    if (!app.renderer.beginOffscreenFrame(width, height)) return {};
    glClearColor(0.065f, 0.075f, 0.087f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const ViewportTransform view = previewView(app, list,
        asset->kind == ModExplorerAsset::Kind::Character, width, height);
    app.renderer.draw(list, view, width, height, true, false);
    app.previewHitView = view;
    app.previewHitList = std::move(list);
    for (DrawCmd& command : app.previewHitList.cmds) {
        if (command.objectIndex < 0 ||
            command.objectIndex >= static_cast<int>(app.previewStage.objects.size())) {
            command.pickable = false;
            continue;
        }
        const StageObject::Kind kind = app.previewStage.objects[static_cast<size_t>(command.objectIndex)].kind;
        command.pickable = command.pickable &&
            (kind == StageObject::Kind::Player || kind == StageObject::Kind::Opponent ||
             kind == StageObject::Kind::Girlfriend || kind == StageObject::Kind::Character);
    }
    app.previewHitValid = true;
    return app.renderer.finishOffscreenPreview();
}

bool renderStageFrame(AtlasApp& app, StageAnimator& animator,
                      const CharacterBinding& binding, int width, int height,
                      std::vector<unsigned char>& pixels) {
    RenderList list;
    buildStageRenderList(app.previewStage, binding, app.atlases,
                         app.renderer, list, &animator);
    applyPreviewVisibility(app, list);
    applyCharacterVerticalFlip(app, list);
    if (list.cmds.empty() || !app.renderer.beginOffscreenFrame(width, height)) return false;
    glClearColor(0.065f, 0.075f, 0.087f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const ModExplorerAsset* asset = selectedAsset(app);
    const ViewportTransform view = previewView(app, list,
        asset && asset->kind == ModExplorerAsset::Kind::Character, width, height);
    app.renderer.draw(list, view, width, height, true, false);
    return app.renderer.endOffscreenFrame(pixels);
}

bool exportStageObjectGifFallback(AtlasApp& app, const StageObject& object,
                                  const AnimationDef& animation,
                                  const fs::path& target, const std::string& reason) {
    if (!app.rendererReady || app.selectedStageObject < 0) return false;
    PreparedGifAnimation output;
    output.width = 480;
    output.height = 270;
    output.fps = 12;
    output.loop = animation.loop;
    const int frames = std::min(48, std::clamp(app.stageGifSeconds, 1, 8) * output.fps);
    output.frames.reserve(static_cast<size_t>(frames));
    const CharacterBinding binding = previewBinding(app);
    StageAnimator animator;
    animator.reset(app.previewStage);
    animator.play(static_cast<size_t>(app.selectedStageObject), app.stageAnimationIndex);
    ViewportTransform view;
    for (int index = 0; index < frames; ++index) {
        animator.update(app.previewStage, binding, app.atlases,
                        index == 0 ? 0.0f : 1000.0f / output.fps, app.previewBpm);
        RenderList list;
        buildStageRenderList(app.previewStage, binding, app.atlases,
                             app.renderer, list, &animator);
        bool visible = false;
        for (DrawCmd& command : list.cmds) {
            command.visible = command.visible && command.objectIndex == app.selectedStageObject;
            visible = visible || command.visible;
        }
        if (!visible || !app.renderer.beginOffscreenFrame(output.width, output.height)) {
            setStatus(app, "El objeto no tiene cuadros visibles para un GIF.",
                           "The object has no visible frames for a GIF.");
            return false;
        }
        if (index == 0) view = frameScene(list, false, output.width, output.height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        app.renderer.draw(list, view, output.width, output.height, true, false);
        GifRgbaFrame frame;
        if (!app.renderer.endOffscreenFrame(frame.rgba)) return false;
        output.frames.push_back(std::move(frame));
    }
    const GifWriteResult written = writeAnimatedGif(target, output);
    setStatus(app, written.ok
        ? "GIF del objeto guardado con render alternativo: " + target.u8string() + " · " + reason
        : "Falló también el GIF alternativo: " + written.error,
        written.ok
        ? "Object GIF saved with rendered fallback: " + target.u8string() + " · " + reason
        : "Rendered GIF fallback also failed: " + written.error);
    return written.ok;
}

bool exportStageScenePng(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Stage || !app.rendererReady) return false;
    std::vector<unsigned char> pixels;
    StageAnimator animator = app.animator;
    if (!renderStageFrame(app, animator, previewBinding(app), 1280, 720, pixels)) {
        setStatus(app, "No se pudo dibujar el escenario completo.",
                       "Could not render the full stage.");
        return false;
    }
    const fs::path target = availablePath(folder / fs::u8path(
        safeName(asset->id) + "-scene.png"), false);
    if (target.empty()) return false;
    const bool ok = stbi_write_png(target.u8string().c_str(), 1280, 720, 4,
                                   pixels.data(), 1280 * 4) != 0;
    setStatus(app, ok ? "Escenario PNG guardado: " + target.u8string()
                      : "No se pudo guardar el PNG del escenario.",
                   ok ? "Stage PNG saved: " + target.u8string()
                      : "Could not save the stage PNG.");
    return ok;
}

bool exportStageSceneGif(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Stage || !app.rendererReady) return false;
    const int fps = std::clamp(app.stageGifFps, 4, 24);
    const int frames = std::min(72, std::clamp(app.stageGifSeconds, 1, 8) * fps);
    PreparedGifAnimation output;
    output.width = 960;
    output.height = 540;
    output.fps = fps;
    output.loop = true;
    output.frames.reserve(static_cast<size_t>(frames));
    const CharacterBinding binding = previewBinding(app);
    StageAnimator animator;
    animator.reset(app.previewStage);
    animator.setPlaying(true);
    for (int index = 0; index < frames; ++index) {
        animator.update(app.previewStage, binding, app.atlases,
                        index == 0 ? 0.0f : 1000.0f / fps, 100.0f);
        GifRgbaFrame frame;
        if (!renderStageFrame(app, animator, binding, 960, 540, frame.rgba)) {
            setStatus(app, "No se pudo dibujar un cuadro del GIF del escenario.",
                           "Could not render a stage GIF frame.");
            return false;
        }
        output.frames.push_back(std::move(frame));
    }
    const fs::path target = availablePath(folder / fs::u8path(
        safeName(asset->id) + "-scene.gif"), false);
    if (target.empty()) return false;
    const GifWriteResult written = writeAnimatedGif(target, output);
    setStatus(app, written.ok ? "GIF del escenario guardado: " + target.u8string()
                              : "Falló el GIF del escenario: " + written.error,
                   written.ok ? "Stage GIF saved: " + target.u8string()
                              : "Stage GIF failed: " + written.error);
    return written.ok;
}

bool exportCharacterPreviewGif(AtlasApp& app, const fs::path& folder) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character || !app.rendererReady) return false;
    const int width = std::clamp(app.previewGifWidth, 160, 1920);
    const int height = std::clamp(app.previewGifHeight, 90, 1080);
    const int fps = std::clamp(app.previewGifFps, 4, 60);
    const size_t bytesPerFrame = static_cast<size_t>(width) * height * 4u;
    const int memoryLimit = static_cast<int>((384u * 1024u * 1024u) /
        std::max<size_t>(1, bytesPerFrame));
    const int frames = std::min({app.previewGifSeconds * fps,
        app.previewGifMaxFrames, memoryLimit});
    if (frames < 1) {
        setStatus(app, "El tamaño del GIF excede el límite de memoria.",
                       "GIF size exceeds the memory limit.");
        return false;
    }
    PreparedGifAnimation output;
    output.width = width;
    output.height = height;
    output.fps = fps;
    output.loop = true;
    output.frames.reserve(static_cast<size_t>(frames));
    const CharacterBinding binding = previewBinding(app);
    StageAnimator animator;
    animator.reset(app.previewStage);
    animator.setPlaying(true);
    const SongLabState& lab = app.songLab;
    const bool chartActive = lab.chartLoaded && lab.audioLoaded && lab.audio.playing() &&
        (songLabCharacterLine(app) != -1 || songLabCompanionLine(app) != -1);
    const double startMs = chartActive ? lab.audio.positionMs() : 0.0;
    for (int index = 0; index < frames; ++index) {
        if (chartActive) {
            double ms = startMs + static_cast<double>(index) * 1000.0 /
                fps * app.previewSpeed;
            const double duration = lab.audio.durationMs();
            if (duration > 0.0)
                ms = lab.loopSong ? std::fmod(ms, duration) : std::min(ms, duration);
            const double beat = lab.timeMap.beatAt(ms);
            animator.resync(beat, ms);
            animator.updateWithBeat(app.previewStage, binding, app.atlases, ms, beat, true);
            std::map<int, std::pair<const ChartNote*, SongLabSinger>> active;
            for (const ChartNote& note : lab.chart.notes) {
                if (note.timeMs > ms) break;
                if (note.direction < 0 || note.direction > 3 ||
                    lower(note.type) == "no anim note") continue;
                for (const SongLabSinger& singer : {
                    songLabSinger(app, binding, note), songLabCompanionSinger(app, note)}) {
                    if (!singer.character || singer.object < 0) continue;
                    const double endBeat = lab.timeMap.beatAt(note.timeMs) +
                        songLabHoldBeats(app, *singer.character, note);
                    if (endBeat > beat) active[singer.object] = {&note, singer};
                }
            }
            for (const auto& [object, note] : active) {
                const double remaining = lab.timeMap.beatAt(note.first->timeMs) +
                    songLabHoldBeats(app, *note.second.character, *note.first) - beat;
                songLabSingOn(app, animator, note.second, *note.first, remaining,
                              ms - note.first->timeMs);
            }
            animator.updateWithBeat(app.previewStage, binding, app.atlases, ms, beat, true);
        } else {
            animator.update(app.previewStage, binding, app.atlases,
                index == 0 ? 0.0f : 1000.0f / fps * app.previewSpeed,
                app.previewBpm);
        }
        GifRgbaFrame frame;
        if (!renderStageFrame(app, animator, binding, width, height, frame.rgba)) {
            setStatus(app, "No se pudo dibujar un cuadro del GIF de vista.",
                           "Could not render a preview GIF frame.");
            return false;
        }
        output.frames.push_back(std::move(frame));
    }
    std::string filename = safeName(asset->id);
    if (app.secondaryAssetIndex >= 0) {
        const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
        if (app.secondaryAssetIndex < static_cast<int>(assets.size()))
            filename += "-" + safeName(assets[static_cast<size_t>(app.secondaryAssetIndex)].id);
    }
    const fs::path target = availablePath(folder / fs::u8path(filename + "-preview.gif"), false);
    if (target.empty()) return false;
    const GifWriteResult written = writeAnimatedGif(target, output);
    setStatus(app, written.ok ? "GIF de vista guardado: " + target.u8string()
                              : "Falló el GIF de vista: " + written.error,
                   written.ok ? "Preview GIF saved: " + target.u8string()
                              : "Preview GIF failed: " + written.error);
    return written.ok;
}

void drawHeader(AtlasApp& app, SDL_Window* window) {
    const bool es = app.spanish;
    ImGui::TextColored(ImVec4(0.89f, 0.76f, 0.57f, 1.0f), "FUNKIN ATLAS - FML TOOL");
    ImGui::SameLine();
    ImGui::TextDisabled("/ %s", es ? "estudio de personajes y escenarios" : "character and stage studio");
    ImGui::SameLine(ImGui::GetWindowWidth() - 95.0f);
    if (ImGui::SmallButton(es ? "ES  /  EN###language" : "EN  /  ES###language")) { app.spanish = !app.spanish; saveGallery(app); }
    ImGui::Separator();
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 355.0f));
    ImGui::InputTextWithHint("##modpath", es ? "Carpeta, ZIP o archivo" : "Mod folder, ZIP or file", app.root.data(), app.root.size());
    ImGui::SameLine();
    if (ImGui::Button(es ? "Añadir" : "Add")) {
        const fs::path path = fs::u8path(app.root.data());
        std::error_code ec;
        if (fs::is_regular_file(path, ec) && lower(path.extension().u8string()) != ".zip")
            addSingleResource(app, path);
        else addMod(app, path);
    }
    ImGui::SameLine();
    if (ImGui::Button(es ? "Carpeta" : "Folder")) {
        app.dialogAction = DialogAction::Add;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("ZIP")) {
        static const SDL_DialogFileFilter filtersEs[] = {{"Archivo ZIP", "zip"}};
        static const SDL_DialogFileFilter filtersEn[] = {{"ZIP archive", "zip"}};
        app.dialogAction = DialogAction::Add;
        SDL_ShowOpenFileDialog(dialogSelected, &app, window, es ? filtersEs : filtersEn, 1, nullptr, false);
    }
    ImGui::SameLine();
    const char* manualPopup = es ? "Cargar manualmente###manual-import" : "Load manually###manual-import";
    if (ImGui::Button("Manual")) ImGui::OpenPopup(manualPopup);
    ImGui::SetNextWindowSize(ImVec2(650.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(manualPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(es ? "CARGAR RECURSO MANUALMENTE" : "LOAD RESOURCE MANUALLY");
        ImGui::TextWrapped("%s", es
            ? "Elige la definición y, para un personaje, su imagen y atlas si la detección automática falla. Puedes seleccionar archivos de carpetas distintas."
            : "Choose the definition and, for a character, its image and atlas if automatic detection fails. Files may come from different folders.");
        const char* engines[] = {"Auto", "Psych Engine", "Codename Engine", "V-Slice"};
        ImGui::SetNextItemWidth(300.0f);
        ImGui::Combo(es ? "Motor de origen" : "Source engine", &app.manualEngine, engines, 4);
        ImGui::TextWrapped("%s", es ? "Psych: personaje JSON + PNG + atlas XML Sparrow."
                                          : "Psych: character JSON + PNG + Sparrow XML atlas.");
        ImGui::TextWrapped("%s", es ? "Codename: personaje XML + PNG + atlas XML/TXT; también admite Animation.json."
                                          : "Codename: character XML + PNG + XML/TXT atlas; Animation.json is also supported.");
        ImGui::TextWrapped("%s", es ? "V-Slice: personaje JSON + PNG + atlas XML, o Animation.json + spritemap1.json + PNG."
                                          : "V-Slice: character JSON + PNG + XML atlas, or Animation.json + spritemap1.json + PNG.");
        ImGui::TextWrapped("%s", es ? "Para stages: Psych JSON/Lua; Codename XML; V-Slice JSON. Las imágenes se resuelven desde la definición."
                                          : "For stages: Psych JSON/Lua; Codename XML; V-Slice JSON. Images are resolved from the definition.");
        ImGui::TextDisabled("%s", es ? "Definición JSON / XML / Lua, o PNG suelto" : "JSON / XML / Lua definition, or standalone PNG");
        ImGui::SetNextItemWidth(560.0f);
        ImGui::InputTextWithHint("##manual-definition", es ? "Ruta del archivo principal" : "Main file path",
                                 app.manualDefinition.data(), app.manualDefinition.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("...##manual-definition")) {
            static const SDL_DialogFileFilter filters[] = {{"Definition or PNG", "json;xml;lua;png"}};
            app.dialogAction = DialogAction::ManualDefinition;
            SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
        }
        ImGui::TextDisabled("%s", es ? "Imagen PNG del personaje (opcional)" : "Character PNG image (optional)");
        ImGui::SetNextItemWidth(560.0f);
        ImGui::InputTextWithHint("##manual-image", es ? "Ruta de la imagen" : "Image path",
                                 app.manualImage.data(), app.manualImage.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("...##manual-image")) {
            static const SDL_DialogFileFilter filters[] = {{"PNG image", "png"}};
            app.dialogAction = DialogAction::ManualImage;
            SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
        }
        ImGui::TextDisabled("%s", es ? "Atlas XML / TXT / Animation.json (opcional)" : "XML / TXT / Animation.json atlas (optional)");
        ImGui::SetNextItemWidth(560.0f);
        ImGui::InputTextWithHint("##manual-atlas", es ? "Ruta del atlas" : "Atlas path",
                                 app.manualAtlas.data(), app.manualAtlas.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("...##manual-atlas")) {
            static const SDL_DialogFileFilter filters[] = {{"Atlas", "xml;txt;json"}};
            app.dialogAction = DialogAction::ManualAtlas;
            SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
        }
        if (app.manualEngine == 3 ||
            lower(fs::u8path(app.manualAtlas.data()).filename().u8string()) == "animation.json") {
            ImGui::TextDisabled("%s", es ? "V-Slice: spritemap1.json (opcional junto a Animation.json)"
                                          : "V-Slice: spritemap1.json (optional beside Animation.json)");
            ImGui::SetNextItemWidth(560.0f);
            ImGui::InputTextWithHint("##manual-spritemap", es ? "Ruta del spritemap" : "Spritemap path",
                                     app.manualSpritemap.data(), app.manualSpritemap.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("...##manual-spritemap")) {
                static const SDL_DialogFileFilter filters[] = {{"Spritemap JSON", "json"}};
                app.dialogAction = DialogAction::ManualSpritemap;
                SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
            }
        }
        ImGui::TextDisabled("%s", es ? "Icono de vida PNG (opcional)" : "Health icon PNG (optional)");
        ImGui::SetNextItemWidth(560.0f);
        ImGui::InputTextWithHint("##manual-icon", es ? "Ruta del icono" : "Icon path",
                                 app.manualIcon.data(), app.manualIcon.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("...##manual-icon")) {
            static const SDL_DialogFileFilter filters[] = {{"PNG image", "png"}};
            app.dialogAction = DialogAction::ManualIcon;
            SDL_ShowOpenFileDialog(dialogSelected, &app, window, filters, 1, nullptr, false);
        }
        ImGui::TextDisabled("%s", es ? "Los stages toman sus múltiples imágenes de la definición." : "Stages resolve their multiple images from the definition.");
        if (ImGui::Button(es ? "Cargar recurso" : "Load resource") && addManualResource(app))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(es ? "Cancelar" : "Cancel")) ImGui::CloseCurrentPopup();
        if (!statusText(app).empty()) ImGui::TextWrapped("%s", statusText(app).c_str());
        ImGui::EndPopup();
    }
    for (size_t i = 0; i < app.mods.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (i > 0) ImGui::SameLine();
        ImGui::Text("%s", app.mods[i]->label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { removeMod(app, static_cast<int>(i)); ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (app.mods.empty()) ImGui::TextDisabled("%s", es ? "Carga hasta tres mods, ZIP o recursos individuales." : "Load up to three mods, ZIPs or individual resources.");
    if (!statusText(app).empty()) ImGui::TextWrapped("%s", statusText(app).c_str());
}

void drawActorChoice(AtlasApp& app, int role, const char* label) {
    if (app.selectedMod < 0) return;
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    const int selected = app.stageActors[role];
    const char* preview = selected >= 0 && selected < static_cast<int>(assets.size())
        ? assets[static_cast<size_t>(selected)].id.c_str() : (app.spanish ? "Ninguno" : "None");
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable(app.spanish ? "Ninguno" : "None", selected < 0)) {
            app.stageActors[role] = -1;
            app.stageActorPinned[role] = true;
            app.animator.reset(app.previewStage);
            app.songLab.viewDirty = true;
        }
        for (size_t i = 0; i < assets.size(); ++i) {
            if (assets[i].kind != ModExplorerAsset::Kind::Character || !assets[i].valid) continue;
            if (ImGui::Selectable(assets[i].id.c_str(), selected == static_cast<int>(i))) {
                app.stageActors[role] = static_cast<int>(i);
                app.stageActorPinned[role] = true;
                app.animator.reset(app.previewStage);
                app.songLab.viewDirty = true;
            }
        }
        ImGui::EndCombo();
    }
}

const char* stageObjectKind(StageObject::Kind kind) {
    switch (kind) {
        case StageObject::Kind::Sprite: return "Sprite";
        case StageObject::Kind::Box: return "Color box";
        case StageObject::Kind::Player: return "Player";
        case StageObject::Kind::Opponent: return "Opponent";
        case StageObject::Kind::Girlfriend: return "Girlfriend";
        case StageObject::Kind::Character: return "Character";
        case StageObject::Kind::Ratings: return "Ratings";
        case StageObject::Kind::Unknown: return "Unresolved";
    }
    return "Unresolved";
}

fs::path findVsCode() {
#ifdef _WIN32
    std::vector<fs::path> candidates;
    if (const char* local = std::getenv("LOCALAPPDATA"))
        candidates.push_back(fs::u8path(local) / "Programs" / "Microsoft VS Code" / "Code.exe");
    if (const char* programs = std::getenv("ProgramFiles"))
        candidates.push_back(fs::u8path(programs) / "Microsoft VS Code" / "Code.exe");
    wchar_t found[32768] = {};
    if (SearchPathW(nullptr, L"Code.exe", nullptr, 32768, found, nullptr))
        candidates.emplace_back(found);
    for (const fs::path& path : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) return path;
    }
#endif
    return {};
}

const StageScriptInventory& stageScripts(AtlasApp& app, const LoadedMod& mod,
                                         const ModExplorerAsset& asset,
                                         const UniversalStage& stage) {
    const std::string key = identity(mod, asset);
    if (const auto found = app.stageScriptCache.find(key); found != app.stageScriptCache.end())
        return found->second;
    StageScriptInventory scripts;
    const Vfs& vfs = mod.catalog.vfs();
    fs::path contentRoot = fs::u8path(asset.sourcePath).parent_path().parent_path();
    const std::string prefix = Vfs::normalize(contentRoot.u8string());
    const std::string base = prefix.empty() ? "" : lower(prefix) + "/";
    auto isGlobal = [&](const std::string& path) {
        const std::string name = lower(Vfs::normalize(path));
        return name.rfind(base + "scripts/", 0) == 0 ||
               name.rfind(base + "data/scripts/", 0) == 0 ||
               name == base + "data/global.hx";
    };
    auto addScript = [&](const std::string& path) {
        if (path.empty()) return;
        const std::string extension = lower(fs::u8path(path).extension().u8string());
        if (extension != ".lua" && extension != ".hx") return;
        (isGlobal(path) ? scripts.global : scripts.local).insert(Vfs::normalize(path));
    };
    for (const std::string& path : stage.behaviorScripts) addScript(path);
    fs::path sibling = fs::u8path(asset.sourcePath);
    if (lower(sibling.extension().u8string()) == ".lua") addScript(asset.sourcePath);
    for (const char* extension : {".lua", ".hx"}) {
        sibling.replace_extension(extension);
        if (vfs.exists(sibling.u8string())) addScript(sibling.u8string());
    }
    for (const Vfs::Entry& entry : vfs.allEntries())
        if (isGlobal(entry.virtualPath)) addScript(entry.virtualPath);
    return app.stageScriptCache.emplace(key, std::move(scripts)).first->second;
}

void drawStageSidebar(AtlasApp& app, const LoadedMod& mod, const ModExplorerAsset& asset,
                      SDL_Window* window) {
    const bool es = app.spanish;
    UniversalStage& stage = app.previewStage;
    const StageScriptInventory& scripts = stageScripts(app, mod, asset, stage);
    ImGui::TextDisabled(es ? "%zu objetos · Zoom %.2f" : "%zu objects · Zoom %.2f",
                        stage.objects.size(), stage.zoom);
    ImGui::TextDisabled(es ? "Scripts: %zu locales · %zu generales del mod"
                           : "Scripts: %zu local · %zu mod-wide",
                        scripts.local.size(), scripts.global.size());
    if (ImGui::SmallButton(es ? "Abrir carpeta del stage" : "Open stage folder"))
        openSourceFolder(app, app.selectedMod, asset.sourcePath);
    ImGui::TextDisabled("%s", es ? "PERSONAJES Y POSICIONES" : "ACTORS AND POSITIONS");
    ImGui::Checkbox(es ? "Mostrar personajes en el escenario" : "Show characters on stage",
                    &app.showStageCharacters);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", es
        ? "Apagado por defecto, incluso al cargar una canción; solo cambia la vista."
        : "Off by default, including when loading a song; preview only.");
    if (ImGui::Button(es ? "Usar posiciones declaradas en el stage" : "Use positions declared by stage")) {
        const UniversalStage& original = std::get<UniversalStage>(asset.parsed);
        int restored = 0;
        for (size_t i = 0; i < std::min(stage.objects.size(), original.objects.size()); ++i) {
            StageObject& target = stage.objects[i];
            const StageObject& source = original.objects[i];
            if (target.kind != source.kind ||
                (target.kind != StageObject::Kind::Player &&
                 target.kind != StageObject::Kind::Opponent &&
                 target.kind != StageObject::Kind::Girlfriend) ||
                !stageRoleDeclared(mod, asset, source.kind)) continue;
            target.position = source.position;
            ++restored;
        }
        app.animator.reset(stage);
        app.songLab.viewDirty = true;
        setStatus(app, restored > 0
                ? std::to_string(restored) + " posiciones declaradas restauradas desde el stage."
                : "Este stage no declara posiciones de personajes recuperables.",
                       restored > 0
                ? std::to_string(restored) + " authored positions restored from the stage."
                : "This stage has no recoverable authored character positions.");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", es
        ? "Restablece solo la vista previa; no modifica los archivos del mod."
        : "Resets the preview only; it does not modify mod files.");
    const StageObject::Kind roles[] = {
        StageObject::Kind::Player, StageObject::Kind::Opponent, StageObject::Kind::Girlfriend
    };
    const char* labelsEs[] = {"Jugador", "Rival", "GF"};
    const char* labelsEn[] = {"Player", "Opponent", "GF"};
    for (int role = 0; role < 3; ++role) {
        ImGui::PushID(role);
        ImGui::TextUnformatted(es ? labelsEs[role] : labelsEn[role]);
        ImGui::SetNextItemWidth(-1.0f);
        drawActorChoice(app, role, "##actor");
        for (StageObject& object : stage.objects) {
            if (object.kind != roles[role]) continue;
            float position[2] = {object.position.x, object.position.y};
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2(es ? "Posición###role-position" : "Position###role-position", position, 1.0f)) {
                object.position = {position[0], position[1]};
                app.animator.reset(stage);
                app.songLab.viewDirty = true;
            }
            break;
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("%s", es ? "Las posiciones ajustadas aquí son solo de vista previa." : "Position adjustments here affect the preview only.");
    ImGui::Separator();
    if (ImGui::CollapsingHeader(es ? "Jerarquía de la escena###stage-hierarchy" : "Scene hierarchy###stage-hierarchy",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        int inspectionHidden = 0;
        const UniversalStage& originalStage = std::get<UniversalStage>(asset.parsed);
        for (size_t i = 0; i < std::min(stage.objects.size(), originalStage.objects.size()); ++i) {
            const auto before = originalStage.objects[i].properties.find("visible");
            const auto after = stage.objects[i].properties.find("visible");
            if ((before == originalStage.objects[i].properties.end() || before->second.asBool()) &&
                after != stage.objects[i].properties.end() && !after->second.asBool())
                ++inspectionHidden;
        }
        if (inspectionHidden > 0)
            ImGui::TextColored(ImVec4(0.94f, 0.72f, 0.42f, 1.0f),
                es ? "%d capa(s) opaca(s) ocultas para inspeccionar la escena"
                   : "%d opaque overlay(s) hidden to inspect the scene", inspectionHidden);
        if (ImGui::SmallButton(es ? "Restaurar visibilidad declarada" : "Restore authored visibility")) {
            for (size_t i = 0; i < std::min(stage.objects.size(), originalStage.objects.size()); ++i) {
                const auto authored = originalStage.objects[i].properties.find("visible");
                if (authored == originalStage.objects[i].properties.end())
                    stage.objects[i].properties.erase("visible");
                else stage.objects[i].properties["visible"] = authored->second;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", es ? "Solo vista previa; no modifica el mod" : "Preview only; does not modify the mod");
        ImGui::BeginChild("##stage-hierarchy-list", ImVec2(0.0f, 330.0f), true);
        ImGui::TextDisabled("%s", es ? "ESCENA · ORDEN DE DIBUJO" : "SCENE · DRAW ORDER");
        auto item = [&](size_t index) {
            StageObject& object = stage.objects[index];
            const std::string label = std::to_string(index + 1) + "  " +
                (object.name.empty() ? std::string(stageObjectKind(object.kind)) : object.name);
            ImGui::PushID(static_cast<int>(index));
            const auto property = object.properties.find("visible");
            bool shown = property == object.properties.end() || property->second.asBool();
            if (ImGui::Checkbox("##visible", &shown))
                object.properties["visible"] = {PropertyValue::Type::Bool, shown ? "true" : "false"};
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", es ? "Mostrar u ocultar en la vista" : "Show or hide in preview");
            ImGui::SameLine();
            if (ImGui::Selectable(label.c_str(), app.selectedStageObject == static_cast<int>(index))) {
                app.selectedStageObject = static_cast<int>(index);
                app.stageAnimationIndex = 0;
            }
            if (ImGui::BeginPopupContextItem("##object-context")) {
                if (ImGui::MenuItem(es ? "Seleccionar" : "Select")) {
                    app.selectedStageObject = static_cast<int>(index);
                    app.stageAnimationIndex = 0;
                }
                if (ImGui::MenuItem(shown ? (es ? "Ocultar" : "Hide") :
                                            (es ? "Mostrar" : "Show")))
                    object.properties["visible"] = {PropertyValue::Type::Bool, shown ? "false" : "true"};
                if (ImGui::MenuItem(es ? "Abrir carpeta del recurso" : "Open resource folder"))
                    openSourceFolder(app, app.selectedMod,
                        object.resolvedImage.empty() ? asset.sourcePath : object.resolvedImage);
                if (!object.resolvedImage.empty() &&
                    ImGui::MenuItem(es ? "Exportar objeto PNG" : "Export object PNG")) {
                    app.selectedStageObject = static_cast<int>(index);
                    app.dialogAction = DialogAction::StageObjectPng;
                    SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
                }
                if (!object.anims.empty() &&
                    ImGui::MenuItem(es ? "Exportar animación GIF" : "Export animation GIF")) {
                    app.selectedStageObject = static_cast<int>(index);
                    app.stageAnimationIndex = 0;
                    app.dialogAction = DialogAction::StageGif;
                    SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        };
        for (size_t index = 0; index < stage.objects.size(); ++index)
            if (stage.objects[index].group.empty()) item(index);
        std::vector<std::string> groups;
        for (const StageObject& object : stage.objects)
            if (!object.group.empty() && std::find(groups.begin(), groups.end(), object.group) == groups.end())
                groups.push_back(object.group);
        for (const std::string& group : groups) {
            if (!ImGui::TreeNode(group.c_str())) continue;
            for (size_t index = 0; index < stage.objects.size(); ++index)
                if (stage.objects[index].group == group) item(index);
            ImGui::TreePop();
        }
        ImGui::EndChild();
    }
}

void drawStageInspector(AtlasApp& app, const LoadedMod& mod, const ModExplorerAsset& asset,
                        SDL_Window* window) {
    const bool es = app.spanish;
    UniversalStage& stage = app.previewStage;
    const StageScriptInventory& scripts = stageScripts(app, mod, asset, stage);
    ImGui::SeparatorText(es ? "DETALLES DE LA CAPA" : "LAYER DETAILS");
    ImGui::BeginChild("##stage-object", ImVec2(0.0f, 245.0f), true);
        if (app.selectedStageObject >= 0 &&
            app.selectedStageObject < static_cast<int>(stage.objects.size())) {
            const size_t index = static_cast<size_t>(app.selectedStageObject);
            StageObject& object = stage.objects[index];
            ImGui::Text("%s · %s", object.name.empty() ? "—" : object.name.c_str(),
                        stageObjectKind(object.kind));
            const auto visibility = object.properties.find("visible");
            bool shown = visibility == object.properties.end() || visibility->second.asBool();
            if (ImGui::Checkbox(es ? "Visible en preview" : "Visible in preview", &shown))
                object.properties["visible"] = {PropertyValue::Type::Bool, shown ? "true" : "false"};
            ImGui::TextDisabled("%s: %zu", es ? "Orden" : "Draw order", index + 1);
            if (!object.group.empty()) ImGui::TextDisabled("%s: %s", es ? "Grupo" : "Group", object.group.c_str());
            ImGui::Text("%s: %.0f, %.0f", es ? "Posición" : "Position", object.position.x, object.position.y);
            ImGui::Text("%s: %.2f, %.2f · %s: %.2f, %.2f",
                        es ? "Escala" : "Scale", object.scale.x, object.scale.y,
                        es ? "Desplazamiento" : "Scroll", object.scroll.x, object.scroll.y);
            ImGui::Text("%s: %.2f · %s: %.1f°", es ? "Opacidad" : "Opacity", object.alpha,
                        es ? "Ángulo" : "Angle", object.angle);
            ImGui::TextWrapped("%s: %s", es ? "Sprite declarado" : "Declared sprite",
                               object.spritePath.empty() ? "—" : object.spritePath.c_str());
            ImGui::TextWrapped("%s: %s", es ? "Imagen resuelta" : "Resolved image",
                               object.resolvedImage.empty() ? (es ? "No encontrada o no aplica" : "Not found or not applicable")
                                                            : object.resolvedImage.c_str());
            if (!object.resolvedAtlas.empty())
                ImGui::TextWrapped("Atlas: %s", object.resolvedAtlas.c_str());
            const std::string provenance = asset.format == ModExplorerAsset::Format::CodenameXml
                ? asset.sourcePath
                : asset.format == ModExplorerAsset::Format::VSliceJson
                    ? asset.sourcePath
                    : fs::u8path(asset.sourcePath).replace_extension(".lua").u8string();
            ImGui::TextWrapped("%s: %s", es ? "Origen probable" : "Likely origin",
                               mod.catalog.vfs().exists(provenance) ? provenance.c_str() : asset.sourcePath.c_str());
            if (ImGui::SmallButton(es ? "Abrir carpeta de origen" : "Open source folder"))
                openSourceFolder(app, app.selectedMod,
                    object.resolvedImage.empty() ? asset.sourcePath : object.resolvedImage);
            if (!object.resolvedImage.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton(es ? "PNG del objeto" : "Object PNG")) {
                    app.dialogAction = DialogAction::StageObjectPng;
                    SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
                }
                if (!object.anims.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(es ? "GIF del objeto" : "Object GIF")) {
                        app.dialogAction = DialogAction::StageGif;
                        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
                    }
                }
            }
            if (!object.resolvedImage.empty()) {
                const auto sprite = app.renderer.previewImage(object.resolvedImage);
                if (sprite.ok) {
                    const float fit = std::min(210.0f / sprite.width, 100.0f / sprite.height);
                    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(sprite.texture)),
                                 ImVec2(sprite.width * fit, sprite.height * fit));
                }
            }
        } else {
            ImGui::TextDisabled("%s", es ? "Selecciona un objeto." : "Select an object.");
        }
        ImGui::EndChild();
    if (app.selectedStageObject >= 0 &&
        app.selectedStageObject < static_cast<int>(stage.objects.size()) &&
        !stage.objects[static_cast<size_t>(app.selectedStageObject)].anims.empty()) {
        if (ImGui::CollapsingHeader(es ? "Animación del objeto###stage-animation-panel"
                                     : "Object animation###stage-animation-panel",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            const size_t index = static_cast<size_t>(app.selectedStageObject);
            const StageObject& object = stage.objects[index];
            app.stageAnimationIndex = std::clamp(app.stageAnimationIndex, 0,
                static_cast<int>(object.anims.size()) - 1);
            std::vector<const char*> names;
            for (const AnimationDef& animation : object.anims) names.push_back(animation.name.c_str());
            ImGui::SetNextItemWidth(190.0f);
            ImGui::Combo(es ? "Animación" : "Animation", &app.stageAnimationIndex,
                         names.data(), static_cast<int>(names.size()));
            ImGui::SameLine();
            if (ImGui::SmallButton(es ? "Reproducir" : "Play"))
                app.animator.play(index, app.stageAnimationIndex);
            ImGui::SameLine();
            if (ImGui::SmallButton(es ? "Automática" : "Automatic"))
                app.animator.release(index);
            ImGui::SameLine();
            ImGui::TextDisabled("%s: %d", es ? "Cuadro" : "Frame", app.animator.frameOf(index) + 1);
            if (ImGui::Checkbox(es ? "Repetir solo vista" : "Loop preview only",
                                &app.stageLoopPreview))
                app.stagePoseClock = 0.0;
            ImGui::SameLine();
            if (ImGui::SmallButton(es ? "Exportar GIF" : "Export GIF")) {
                app.dialogAction = DialogAction::StageGif;
                SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(es ? "Hoja montada" : "Mounted sheet")) {
                app.dialogAction = DialogAction::StageSheet;
                SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
            }
            drawStageAnimationPanel(app, mod, asset);
        }
    }
    if (!scripts.local.empty() || !scripts.global.empty()) {
        if (ImGui::CollapsingHeader(es ? "Scripts del escenario y del mod###stage-scripts" : "Stage and mod scripts###stage-scripts",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("%s", es ? "Los generales se cargan para el mod; no todos afectan necesariamente a este stage."
                                         : "Mod-wide scripts are available globally; not all necessarily affect this stage.");
            static const fs::path editor = findVsCode();
            std::error_code ec;
            const bool folderSource = fs::is_directory(mod.catalog.root(), ec);
            auto drawScript = [&](const std::string& script) {
                ImGui::PushID(script.c_str());
                ImGui::TextWrapped("%s", script.c_str());
                const auto resolved = folderSource ? mod.catalog.vfs().resolve(script)
                                                   : std::optional<fs::path>{};
                if (folderSource && resolved && !editor.empty()) {
                    if (ImGui::SmallButton(es ? "Abrir en VS Code" : "Open in VS Code")) {
#ifdef _WIN32
                        const std::wstring argument = L"\"" + resolved->wstring() + L"\"";
                        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", editor.c_str(),
                                                                     argument.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
                            setStatus(app, "No se pudo abrir VS Code.", "Could not open VS Code.");
#endif
                    }
                } else {
                    ImGui::TextDisabled("%s", !folderSource
                        ? (es ? "Dentro de ZIP: extrae la fuente para editar." : "Inside ZIP: extract source to edit.")
                        : (es ? "VS Code no disponible o archivo no resuelto." : "VS Code unavailable or file unresolved."));
                }
                ImGui::PopID();
            };
            ImGui::TextDisabled(es ? "Locales (%zu)" : "Local (%zu)", scripts.local.size());
            for (const std::string& script : scripts.local) drawScript(script);
            ImGui::TextDisabled(es ? "Generales (%zu)" : "Mod-wide (%zu)", scripts.global.size());
            for (const std::string& script : scripts.global) drawScript(script);
        }
    }
    ImGui::Separator();
    if (ImGui::Button(es ? "Escenario PNG" : "Stage PNG")) {
        app.dialogAction = DialogAction::StageScenePng;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button(es ? "Escenario GIF" : "Stage GIF")) {
        app.dialogAction = DialogAction::StageSceneGif;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    ImGui::DragInt(es ? "Segundos###stage-gif-seconds" : "Seconds###stage-gif-seconds",
                   &app.stageGifSeconds, 0.2f, 1, 8);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    ImGui::DragInt("FPS###stage-gif-fps", &app.stageGifFps, 0.2f, 4, 24);
    ImGui::TextDisabled("%s", es ? "PNG/GIF usan la vista actual; GIF máximo 72 cuadros."
                                 : "PNG/GIF use the current view; GIF limit: 72 frames.");
    ImGui::Checkbox(es ? "Incluir GIF animados en ZIP" : "Include animated GIFs in ZIP",
                    &app.includeStageGifs);
    ImGui::SameLine();
    if (ImGui::Button(es ? "ZIP de recursos del escenario" : "Stage assets ZIP")) {
        app.dialogAction = DialogAction::StageZip;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
}

void drawLivePanelData(AtlasApp& app) {
    const bool es = app.spanish;
    const ModExplorerAsset* asset = selectedAsset(app);
    if (asset && asset->valid && asset->kind == ModExplorerAsset::Kind::Character)
        drawCharacterAnimationList(app);
    else if (asset && asset->valid && asset->kind == ModExplorerAsset::Kind::Stage)
        drawSongLabActorStatus(app);
    if (app.songLab.chartLoaded && app.songLab.audioLoaded) {
        const double position = app.songLab.audio.positionMs();
        ImGui::SeparatorText(es ? "COMPÁS" : "TIMING");
        ImGui::Text(es ? "Beat %.1f · BPM %.1f" : "Beat %.1f · BPM %.1f",
                    app.songLab.timeMap.beatAt(position), app.songLab.timeMap.bpmAt(position));
        ImGui::TextDisabled(es ? "%zu notas · %zu líneas" : "%zu notes · %zu lines",
                            app.songLab.chart.notes.size(), app.songLab.chart.strumLines.size());
    }
}

void drawDataPanel(AtlasApp& app, SDL_Window* window) {
    const bool es = app.spanish;
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset) {
        ImGui::SeparatorText(es ? "DATOS DEL RECURSO" : "RESOURCE DATA");
        ImGui::TextWrapped("%s", es ? "Selecciona un personaje o escenario de la lista de la derecha."
                                      : "Select a character or stage from the list on the right.");
        return;
    }
    const LoadedMod& mod = *app.mods[static_cast<size_t>(app.selectedMod)];
    const std::string key = identity(mod, *asset);
    ImGui::SeparatorText(es ? "DATOS DEL RECURSO" : "RESOURCE DATA");
    ImGui::TextWrapped("%s", asset->id.c_str());
    ImGui::TextDisabled("%s", mod.label.c_str());
    ImGui::TextDisabled("%s", modExplorerFormatName(asset->format));
    if (asset->valid && asset->kind == ModExplorerAsset::Kind::Character) {
        const auto& character = std::get<UniversalCharacter>(asset->parsed);
        ImGui::TextDisabled(es ? "%zu animaciones" : "%zu animations", character.anims.size());
    } else if (asset->valid && asset->kind == ModExplorerAsset::Kind::Stage) {
        const auto& stage = std::get<UniversalStage>(asset->parsed);
        ImGui::TextDisabled(es ? "%zu objetos · zoom %.2f" : "%zu objects · zoom %.2f",
                            stage.objects.size(), stage.zoom);
    }
    ImGui::Spacing();
    ImGui::SeparatorText(es ? "ORIGEN" : "SOURCE");
    const char* engines[] = {"Auto", "Psych Engine", "Codename Engine", "V-Slice"};
    int engine = asset->suggestedEngine;
    const auto override = app.engineOverrides.find(key);
    if (override != app.engineOverrides.end()) engine = std::clamp(override->second, 0, 3);
    ImGui::TextDisabled("%s", es ? "Motor de origen" : "Source engine");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##source-engine", &engine, engines, 4)) {
        if (engine == asset->suggestedEngine) app.engineOverrides.erase(key);
        else app.engineOverrides[key] = engine;
        saveGallery(app);
    }
    ImGui::TextWrapped("%s", asset->sourcePath.c_str());
    if (ImGui::SmallButton(es ? "Abrir carpeta de origen" : "Open source folder"))
        openSourceFolder(app, app.selectedMod, asset->sourcePath);
    ImGui::Spacing();
    ImGui::SeparatorText(es ? "BIBLIOTECA" : "LIBRARY");
    const bool saved = inGallery(app, app.selectedMod, *asset);
    if (ImGui::Button(saved ? (es ? "Quitar de galería" : "Remove from gallery")
                            : (es ? "Guardar en galería" : "Save to gallery"))) {
        toggleGalleryAsset(app, app.selectedMod, *asset);
    }
    if (!app.pendingRecoveryKey.empty()) {
        if (ImGui::Button(es ? "Reemplazar entrada" : "Replace entry"))
            replaceGalleryEntry(app, app.selectedMod, *asset);
    }
    ImGui::Spacing();
    ImGui::SeparatorText(es ? "EXPORTAR" : "EXPORT");
    const char* targets[] = {es ? "Archivos originales" : "Original files", "V-Slice ZIP",
                             "Psych Engine", "Codename Engine"};
    ImGui::TextDisabled("%s", es ? "Formato de destino" : "Target format");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##export-target", &app.exportTarget, targets, 4);
    if (ImGui::Button(es ? "Elegir carpeta y exportar" : "Choose folder and export")) {
        app.dialogAction = DialogAction::Export;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    ImGui::TextWrapped("%s", app.exportTarget == 0
        ? (es ? "Los originales conservan scripts, pero esta app no los ejecuta ni verifica."
              : "Original files preserve scripts, but this app does not run or verify them.")
        : (es ? "Solo aspecto visual/decorativo. No se traducen scripts, cinemáticas ni mecánicas."
              : "Visual/decorative content only. Scripts, cutscenes and mechanics are not translated."));
    if (!asset->error.empty() || !asset->warnings.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText(es ? "DIAGNÓSTICOS" : "DIAGNOSTICS");
        if (!asset->error.empty())
            ImGui::TextWrapped("%s: %s", es ? "Error técnico" : "Technical error", asset->error.c_str());
        for (const std::string& warning : asset->warnings) {
            const std::string shown = diagnosticText(warning, es);
            ImGui::BulletText("%s", shown.c_str());
        }
    }
}

void drawViewControls(AtlasApp& app) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || !asset->valid) return;
    const bool es = app.spanish;
    ImGui::SeparatorText(es ? "VISTA Y POSICIÓN" : "VIEW AND POSITION");
    if (!app.songLab.audio.playing()) {
        if (ImGui::Button(app.playing ? (es ? "Pausar animación" : "Pause animation")
                                      : (es ? "Animar" : "Animate"))) app.playing = !app.playing;
        if (ImGui::Button(es ? "Reiniciar animación" : "Restart animation")) app.animator.rewind();
    }
    ImGui::TextDisabled("%s", es ? "Zoom de la vista" : "View zoom");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##view-zoom", &app.previewZoom, 0.1f, 8.0f, "%.2fx");
    if (ImGui::SmallButton(es ? "Restablecer vista" : "Reset view")) {
        app.previewZoom = 1.0f;
        app.previewPanX = 0.0f;
        app.previewPanY = 0.0f;
    }
    if (asset->kind == ModExplorerAsset::Kind::Stage) {
        ImGui::Checkbox(es ? "Encuadre automático" : "Auto frame", &app.autoFrame);
        ImGui::Checkbox(es ? "Mostrar personajes" : "Show characters", &app.showStageCharacters);
        if (!songLabViewActive(app)) {
            ImGui::TextDisabled("%s", es ? "BPM del escenario" : "Stage BPM");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##stage-bpm", &app.previewBpm, 30.0f, 400.0f, "%.0f");
        }
    }
    ImGui::TextDisabled("%s", songLabViewActive(app)
        ? (es ? "Velocidad de canción y animación" : "Song and animation speed")
        : (es ? "Velocidad de animación" : "Animation speed"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##preview-speed", &app.previewSpeed, 0.1f, 3.0f, "%.2fx") &&
        app.songLab.audioLoaded) app.songLab.audio.setRate(app.previewSpeed);
    ImGui::TextDisabled("%s", es ? "Clic central: mover cámara · Rueda: zoom"
                                 : "Middle-drag: pan camera · Wheel: zoom");
    if (asset->kind == ModExplorerAsset::Kind::Character) {
        const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
        const char* selected = app.characterBackdrop >= 0 &&
            app.characterBackdrop < static_cast<int>(assets.size())
            ? assets[static_cast<size_t>(app.characterBackdrop)].id.c_str()
            : (es ? "Sin escenario" : "No stage");
        ImGui::TextDisabled("%s", es ? "Escenario de fondo" : "Stage backdrop");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##stage-backdrop", selected)) {
            if (ImGui::Selectable(es ? "Sin escenario" : "No stage", app.characterBackdrop < 0)) {
                app.characterBackdrop = -1;
                buildCharacterStage(app);
                refreshPreviewCharacter(app);
            }
            for (size_t i = 0; i < assets.size(); ++i) {
                if (!assets[i].valid || assets[i].kind != ModExplorerAsset::Kind::Stage) continue;
                if (ImGui::Selectable(assets[i].id.c_str(), app.characterBackdrop == static_cast<int>(i))) {
                    app.characterBackdrop = static_cast<int>(i);
                    buildCharacterStage(app);
                    refreshPreviewCharacter(app);
                }
            }
            ImGui::EndCombo();
        }
        if (app.characterMarkerIndex >= 0 &&
            app.characterMarkerIndex < static_cast<int>(app.previewStage.objects.size())) {
            StageObject& marker = app.previewStage.objects[static_cast<size_t>(app.characterMarkerIndex)];
            float coordinates[2] = {marker.position.x, marker.position.y};
            ImGui::TextDisabled("%s", es ? "Posición X / Y · arrastra también en la vista"
                                          : "Position X / Y · or drag in preview");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("##character-position", coordinates, 1.0f)) {
                marker.position.x = coordinates[0];
                marker.position.y = coordinates[1];
            }
        }
    } else if (app.selectedStageObject >= 0 &&
               app.selectedStageObject < static_cast<int>(app.previewStage.objects.size())) {
        StageObject& object = app.previewStage.objects[static_cast<size_t>(app.selectedStageObject)];
        if (object.kind == StageObject::Kind::Player || object.kind == StageObject::Kind::Opponent ||
            object.kind == StageObject::Kind::Girlfriend || object.kind == StageObject::Kind::Character) {
            float coordinates[2] = {object.position.x, object.position.y};
            ImGui::TextDisabled("%s: %s", es ? "Personaje seleccionado" : "Selected character",
                                object.name.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("##stage-character-position", coordinates, 1.0f)) {
                object.position.x = coordinates[0];
                object.position.y = coordinates[1];
            }
        }
    }
}

void drawCharacterPairControls(AtlasApp& app, SDL_Window* window) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || !asset->valid || asset->kind != ModExplorerAsset::Kind::Character ||
        app.selectedMod < 0) return;
    const bool es = app.spanish;
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    ImGui::SeparatorText(es ? "PERSONAJES EN VISTA" : "PREVIEW CHARACTERS");
    ImGui::TextWrapped("%s: %s", es ? "Editando" : "Editing", asset->id.c_str());
    ImGui::Checkbox(es ? "Mostrar spritesheet en vivo" : "Show live spritesheet",
                    &app.showLiveSheet);
    const std::string companion = app.secondaryAssetIndex >= 0 &&
        app.secondaryAssetIndex < static_cast<int>(assets.size())
        ? assets[static_cast<size_t>(app.secondaryAssetIndex)].id
        : (es ? "Ninguno" : "None");
    ImGui::TextDisabled("%s", es ? "Segundo personaje" : "Second character");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##second-character", companion.c_str())) {
        if (ImGui::Selectable(es ? "Ninguno" : "None", app.secondaryAssetIndex < 0))
            setSecondaryCharacter(app, -1);
        for (size_t i = 0; i < assets.size(); ++i) {
            if (static_cast<int>(i) == app.selectedAsset || !assets[i].valid ||
                assets[i].kind != ModExplorerAsset::Kind::Character) continue;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(assets[i].id.c_str(), app.secondaryAssetIndex == static_cast<int>(i)))
                setSecondaryCharacter(app, static_cast<int>(i));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", assets[i].sourcePath.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (app.secondaryAssetIndex >= 0) {
        if (ImGui::Button(es ? "Editar el otro personaje" : "Edit other character"))
            switchActiveCharacter(app);
        const char* modesEs[] = {"Editado", "Segundo", "Ambos"};
        const char* modesEn[] = {"Editing", "Second", "Both"};
        ImGui::TextDisabled("%s", es ? "Spritesheet lateral" : "Side spritesheet");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##sheet-view-mode", &app.sheetViewMode,
                     es ? modesEs : modesEn, 3);
    }
    constexpr bool showPreviewGifControls = false;
    if (showPreviewGifControls) {
    ImGui::SeparatorText(es ? "GIF DE VISTA" : "PREVIEW GIF");
    ImGui::TextDisabled("%s", es ? "Tamaño (ancho / alto)" : "Size (width / height)");
    int dimensions[2] = {app.previewGifWidth, app.previewGifHeight};
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragInt2("##preview-gif-size", dimensions, 1.0f, 160, 1920)) {
        app.previewGifWidth = std::clamp(dimensions[0], 160, 1920);
        app.previewGifHeight = std::clamp(dimensions[1], 90, 1080);
    }
    ImGui::TextDisabled("%s", es ? "Duración / FPS / máximo de cuadros" : "Seconds / FPS / frame limit");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##preview-gif-seconds", &app.previewGifSeconds, 1, 30,
                     es ? "%d segundos" : "%d seconds");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##preview-gif-fps", &app.previewGifFps, 4, 60, "%d FPS");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##preview-gif-limit", &app.previewGifMaxFrames, 12, 600,
                     es ? "%d cuadros máx." : "%d frames max");
    const int count = std::min(app.previewGifSeconds * app.previewGifFps,
                               app.previewGifMaxFrames);
    ImGui::TextDisabled(es ? "Salida: %d cuadros. Si suena una canción, empieza en el tiempo actual."
                           : "Output: %d frames. If a song plays, starts at the current time.", count);
    if (ImGui::Button(es ? "GIF de la vista" : "Preview GIF")) {
        app.dialogAction = DialogAction::PairSceneGif;
        SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
    }
    if (app.secondaryAssetIndex >= 0) {
        if (ImGui::Button(es ? "GIF individuales de ambos" : "Both as separate GIFs")) {
            app.dialogAction = DialogAction::PairGifs;
            SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
        }
    }
    }
}

void drawDetail(AtlasApp& app, SDL_Window* window) {
    const bool es = app.spanish;
    for (LiveSheetViewState& sheet : app.liveSheetViews) sheet.boundsValid = false;
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset) {
        app.previewBoundsValid = false;
        app.previewWheel = 0.0f;
        ImGui::Text("%s", es ? "Selecciona un recurso de la lista." : "Select a resource from the list.");
        return;
    }
    const LoadedMod& mod = *app.mods[static_cast<size_t>(app.selectedMod)];
    ImGui::SeparatorText(asset->id.c_str());
    const float available = ImGui::GetContentRegionAvail().x;
    const bool character = asset->valid && asset->kind == ModExplorerAsset::Kind::Character;
    const float previewH = std::clamp(available * 0.54f, 210.0f, 460.0f);
    const float previewW = character && app.showLiveSheet && available >= 560.0f
        ? std::max(260.0f, available * 0.57f) : 0.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##canvas", ImVec2(previewW, previewH), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const int previewWidth = std::max(1, static_cast<int>(std::lround(size.x)));
    const int previewHeight = std::max(1, static_cast<int>(std::lround(size.y)));
    const GlRenderer::PreviewImage preview = renderPreview(app, previewWidth, previewHeight);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) app.previewPanning = false;
    if (preview.ok) {
        const float scaleX = size.x / std::max(1, preview.width);
        const float scaleY = size.y / std::max(1, preview.height);
        const float viewportScale = std::max(0.001f, std::min(
            preview.width / 960.0f, preview.height / 540.0f));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.texture)), size,
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        const bool hovered = ImGui::IsItemHovered();
        const bool canvasHovered = ImGui::IsWindowHovered();
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) app.draggingActorObject = -1;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && app.previewHitValid) {
            const int command = app.renderer.pick(app.previewHitList, app.previewHitView,
                preview.width, preview.height,
                (io.MousePos.x - imageMin.x) / scaleX,
                (io.MousePos.y - imageMin.y) / scaleY);
            if (command >= 0 && command < static_cast<int>(app.previewHitList.cmds.size())) {
                app.draggingActorObject = app.previewHitList.cmds[static_cast<size_t>(command)].objectIndex;
                if (asset->kind == ModExplorerAsset::Kind::Stage)
                    app.selectedStageObject = app.draggingActorObject;
            }
        }
        if (app.draggingActorObject >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            app.draggingActorObject < static_cast<int>(app.previewStage.objects.size())) {
            for (const DrawCmd& command : app.previewHitList.cmds) {
                if (command.objectIndex != app.draggingActorObject || !command.pickable) continue;
                const float scale = app.renderer.worldToScreenScale(command,
                    app.previewHitList.camera, app.previewHitView,
                    preview.width, preview.height) * scaleX;
                if (scale > 0.001f) {
                    const float angle = app.previewHitList.camera.angle *
                        3.14159265358979323846f / 180.0f;
                    const float dx = io.MouseDelta.x / scale;
                    const float dy = io.MouseDelta.y / scale;
                    StageObject& actor = app.previewStage.objects[static_cast<size_t>(app.draggingActorObject)];
                    actor.position.x += dx * std::cos(angle) + dy * std::sin(angle);
                    actor.position.y += dy * std::cos(angle) - dx * std::sin(angle);
                    app.characterFrameValid = false;
                }
                break;
            }
        }
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            app.previewPanning = true;
        if (app.previewPanning) {
            app.previewPanX += io.MouseDelta.x / (scaleX * viewportScale);
            app.previewPanY += io.MouseDelta.y / (scaleY * viewportScale);
        }
        if (app.previewWheel != 0.0f) {
            const float before = app.previewZoom;
            app.previewZoom = std::clamp(before * std::pow(1.15f, app.previewWheel), 0.1f, 8.0f);
            const float ratio = app.previewZoom / before;
            const bool overImage = app.previewWheelPos.x >= imageMin.x &&
                app.previewWheelPos.x <= imageMin.x + size.x &&
                app.previewWheelPos.y >= imageMin.y &&
                app.previewWheelPos.y <= imageMin.y + size.y;
            const float cursorX = overImage ?
                (app.previewWheelPos.x - imageMin.x - size.x * 0.5f) /
                    (scaleX * viewportScale) : 0.0f;
            const float cursorY = overImage ?
                (app.previewWheelPos.y - imageMin.y - size.y * 0.5f) /
                    (scaleY * viewportScale) : 0.0f;
            app.previewPanX = ratio * app.previewPanX + (1.0f - ratio) * cursorX;
            app.previewPanY = ratio * app.previewPanY + (1.0f - ratio) * cursorY;
        }
        if (canvasHovered || app.previewPanning || app.draggingActorObject >= 0)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else {
        ImGui::TextWrapped("%s", es ? "No hay vista compuesta disponible. Revisa los diagnósticos y archivos de sprites." : "No composed preview available. Check diagnostics and sprite files.");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    app.previewBoundsMin = ImGui::GetItemRectMin();
    app.previewBoundsMax = ImGui::GetItemRectMax();
    app.previewBoundsValid = true;
    app.previewWheel = 0.0f;
    if (character) {
        if (!app.showLiveSheet) for (LiveSheetViewState& sheet : app.liveSheetViews)
            sheet.wheel = 0.0f;
        if (previewW > 0.0f) {
            ImGui::SameLine();
            if (app.secondaryAssetIndex >= 0 && app.sheetViewMode == 2) {
                ImGui::BeginChild("##pair-live-sheets", ImVec2(0.0f, previewH), false);
                const float half = std::max(100.0f, (previewH - ImGui::GetStyle().ItemSpacing.y) * 0.5f);
                drawActiveSpriteSheet(app, app.previewCharacter, app.characterMarkerIndex,
                                      half, "##live-sheet-primary");
                drawActiveSpriteSheet(app, app.previewSecondaryCharacter, app.secondaryMarkerIndex,
                                      half, "##live-sheet-secondary");
                ImGui::EndChild();
            } else if (app.secondaryAssetIndex >= 0 && app.sheetViewMode == 1) {
                drawActiveSpriteSheet(app, app.previewSecondaryCharacter, app.secondaryMarkerIndex,
                                      previewH, "##live-sheet-secondary");
            } else {
                drawActiveSpriteSheet(app, app.previewCharacter, app.characterMarkerIndex,
                                      previewH, "##live-sheet-primary");
            }
        }
        drawCharacterTools(app, window);
    }
    if (asset->valid && asset->kind == ModExplorerAsset::Kind::Stage) {
        drawStageInspector(app, mod, *asset, window);
    }
}

void drawResourceList(AtlasApp& app) {
    const bool es = app.spanish;
    if (app.tab == 0) {
        if (ImGui::CollapsingHeader(es ? "Mis animaciones###saved-library" : "My animations###saved-library",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            int count = 0;
            for (const auto& [key, animations] : app.savedAnimations) {
                const size_t divider = key.find('|');
                const std::string root = divider == std::string::npos ? "" : key.substr(0, divider);
                const std::string resource = divider == std::string::npos ? key : key.substr(divider + 1);
                const size_t colon = resource.find(':');
                const std::string character = fs::u8path(colon == std::string::npos ? resource
                    : resource.substr(colon + 1)).stem().u8string();
                const std::string modName = fs::u8path(root).filename().u8string();
                for (size_t i = 0; i < animations.size(); ++i) {
                    ++count;
                    int foundMod = -1, foundAsset = -1;
                    for (size_t mod = 0; mod < app.mods.size() && foundMod < 0; ++mod) {
                        const auto& assets = app.mods[mod]->catalog.assets();
                        for (size_t assetIndex = 0; assetIndex < assets.size(); ++assetIndex) {
                            if (identity(*app.mods[mod], assets[assetIndex]) != key) continue;
                            foundMod = static_cast<int>(mod);
                            foundAsset = static_cast<int>(assetIndex);
                            break;
                        }
                    }
                    ImGui::PushID(count);
                    const std::string label = animations[i].name + "  ·  " + character;
                    if (ImGui::Selectable(label.c_str(), foundMod == app.selectedMod &&
                        foundAsset == app.selectedAsset && app.savedPoseIndex == static_cast<int>(i))) {
                        if (foundMod >= 0) {
                            selectAsset(app, foundMod, foundAsset);
                            loadSavedPose(app, static_cast<int>(i));
                        } else {
                            setStatus(app, "Carga el mod de origen para abrir " + animations[i].name,
                                      "Load the source mod to open " + animations[i].name);
                        }
                    }
                    ImGui::TextDisabled("%s%s", modName.c_str(),
                                        foundMod < 0 ? (es ? " · fuente no cargada" : " · source not loaded") : "");
                    ImGui::PopID();
                }
            }
            if (!count) ImGui::TextDisabled("%s", es ? "Aún no hay animaciones guardadas." : "No saved animations yet.");
        }
        ImGui::Separator();
    }
    ImGui::TextDisabled("%s", app.tab == 0 ? (es ? "PERSONAJES CARGADOS" : "LOADED CHARACTERS")
                                                : (es ? "ESCENARIOS CARGADOS" : "LOADED STAGES"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##asset-filter", es ? "Buscar recurso" : "Search resources", app.filter.data(), app.filter.size());
    ImGui::Checkbox(es ? "Solo galería" : "Gallery only", &app.galleryOnly);
    ImGui::Separator();
    int total = 0;
    for (size_t mod = 0; mod < app.mods.size(); ++mod) {
        const auto& assets = app.mods[mod]->catalog.assets();
        int count = 0;
        for (const auto& asset : assets)
            if (visible(app, static_cast<int>(mod), asset)) ++count;
        if (!count) continue;
        const std::string group = app.mods[mod]->label + "  ·  " + std::to_string(count) +
            "###mod-group-" + std::to_string(mod);
        if (!ImGui::CollapsingHeader(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
        for (size_t index = 0; index < assets.size(); ++index) {
            const ModExplorerAsset& asset = assets[index];
            if (!visible(app, static_cast<int>(mod), asset)) continue;
            ++total;
            ImGui::PushID(static_cast<int>(mod * 100000 + index));
            if (ImGui::Selectable(asset.id.c_str(), app.selectedMod == static_cast<int>(mod) && app.selectedAsset == static_cast<int>(index)))
                selectAsset(app, static_cast<int>(mod), static_cast<int>(index));
            if (ImGui::BeginPopupContextItem("##asset-context")) {
                if (ImGui::MenuItem(es ? "Ver recurso" : "View resource"))
                    selectAsset(app, static_cast<int>(mod), static_cast<int>(index));
                if (ImGui::MenuItem(es ? "Vista rápida" : "Quick preview")) {
                    app.quickMod = static_cast<int>(mod);
                    app.quickAsset = static_cast<int>(index);
                }
                if (ImGui::MenuItem(es ? "Abrir carpeta de origen" : "Open source folder"))
                    openSourceFolder(app, static_cast<int>(mod), asset.sourcePath);
                const bool saved = inGallery(app, static_cast<int>(mod), asset);
                if (ImGui::MenuItem(saved ? (es ? "Quitar de galería" : "Remove from gallery")
                                          : (es ? "Guardar en galería" : "Save to gallery")))
                    toggleGalleryAsset(app, static_cast<int>(mod), asset);
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(asset.id.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted(asset.sourcePath.c_str());
                ImGui::EndTooltip();
            }
            ImGui::TextDisabled("%s%s", modExplorerFormatName(asset.format), asset.valid && !asset.previewImage.empty() ? "" : "  ·  !");
            ImGui::PopID();
        }
        ImGui::Spacing();
    }
    if (!total) ImGui::TextWrapped("%s", es ? "No hay recursos en esta pestaña." : "No resources in this tab.");
}

void drawQuickPreview(AtlasApp& app) {
    if (app.quickMod < 0 || app.quickMod >= static_cast<int>(app.mods.size())) return;
    const LoadedMod& mod = *app.mods[static_cast<size_t>(app.quickMod)];
    if (app.quickAsset < 0 || app.quickAsset >= static_cast<int>(mod.catalog.assets().size())) return;
    const ModExplorerAsset& asset = mod.catalog.assets()[static_cast<size_t>(app.quickAsset)];
    bool open = true;
    const std::string title = (app.spanish ? "Vista rápida: " : "Quick preview: ") + asset.id + "###quick-preview";
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s · %s", asset.id.c_str(), modExplorerFormatName(asset.format));
        ImGui::TextDisabled("%s", mod.label.c_str());
        if (asset.kind == ModExplorerAsset::Kind::Stage && asset.valid) {
            if (app.quickRenderMod != app.quickMod) {
                if (app.quickRenderMod >= 0) app.quickRenderer.shutdown();
                app.quickRenderMod = -1;
                app.quickConfiguredMod = app.quickConfiguredAsset = -1;
                std::string error;
                if (app.quickRenderer.init([&app, index = app.quickMod](const std::string& path) {
                    const auto found = app.mods[static_cast<size_t>(index)]->catalog.vfs().resolve(path);
                    return found ? found->u8string() : std::string();
                }, &error)) app.quickRenderMod = app.quickMod;
                else ImGui::TextWrapped("%s", error.c_str());
            }
            if (app.quickRenderMod == app.quickMod) {
                if (app.quickConfiguredMod != app.quickMod || app.quickConfiguredAsset != app.quickAsset) {
                    app.quickAtlases = AtlasStore{};
                    app.quickAtlases.readText = [&app, index = app.quickMod](const std::string& path) {
                        const auto found = app.mods[static_cast<size_t>(index)]->catalog.vfs().readText(path);
                        return found ? *found : std::string();
                    };
                    app.quickStage = std::get<UniversalStage>(asset.parsed);
                    prepareStageInspection(app.quickStage);
                    app.quickAnimator.reset(app.quickStage);
                    app.quickConfiguredMod = app.quickMod;
                    app.quickConfiguredAsset = app.quickAsset;
                }
                CharacterBinding binding;
                app.quickAnimator.update(app.quickStage, binding, app.quickAtlases,
                                         ImGui::GetIO().DeltaTime * 1000.0f, app.previewBpm);
                RenderList list;
                buildStageRenderList(app.quickStage, binding, app.quickAtlases,
                                     app.quickRenderer, list, &app.quickAnimator);
                for (DrawCmd& command : list.cmds) {
                    if (command.objectIndex < 0 ||
                        command.objectIndex >= static_cast<int>(app.quickStage.objects.size())) continue;
                    const StageObject& object = app.quickStage.objects[static_cast<size_t>(command.objectIndex)];
                    const auto property = object.properties.find("visible");
                    if (property != object.properties.end() && !property->second.asBool()) command.visible = false;
                    if (object.kind == StageObject::Kind::Player ||
                        object.kind == StageObject::Kind::Opponent ||
                        object.kind == StageObject::Kind::Girlfriend) command.visible = false;
                }
                if (app.quickRenderer.beginOffscreenFrame(960, 540)) {
                    glClearColor(0.065f, 0.075f, 0.087f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    app.quickRenderer.draw(list, frameScene(list, false), 960, 540, true, false);
                    const auto image = app.quickRenderer.finishOffscreenPreview();
                    if (image.ok) ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image.texture)),
                                                ImVec2(440.0f, 247.5f),
                                                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                }
            }
        } else if (!asset.previewImage.empty()) {
            if (app.quickRenderMod != app.quickMod) {
                if (app.quickRenderMod >= 0) app.quickRenderer.shutdown();
                std::string error;
                if (app.quickRenderer.init([&app, index = app.quickMod](const std::string& path) {
                    const auto found = app.mods[static_cast<size_t>(index)]->catalog.vfs().resolve(path);
                    return found ? found->u8string() : std::string();
                }, &error)) app.quickRenderMod = app.quickMod;
                else ImGui::TextWrapped("%s", error.c_str());
            }
            if (app.quickRenderMod == app.quickMod) {
                const auto image = app.quickRenderer.previewImage(asset.previewImage);
                if (image.ok) {
                    ImVec2 uvMin(0.0f, 0.0f), uvMax(1.0f, 1.0f);
                    float sourceWidth = static_cast<float>(image.width);
                    float sourceHeight = static_cast<float>(image.height);
                    if (asset.kind == ModExplorerAsset::Kind::Character) {
                        const UniversalCharacter& character = std::get<UniversalCharacter>(asset.parsed);
                        if (!character.resolvedAtlas.empty() &&
                            !AtlasStore::isAnimatePath(character.resolvedAtlas)) {
                            const auto atlasText = mod.catalog.vfs().readText(character.resolvedAtlas);
                            if (atlasText) {
                                DiagnosticSink sink(DiagnosticScope::WorkspaceInventory);
                                auto atlas = parseSparrowAtlas(*atlasText, character.resolvedAtlas, sink);
                                if (atlas && !atlas.value().frames.empty()) {
                                    const AtlasFrame* frame = &atlas.value().frames.front();
                                    for (const AnimationDef& animation : character.anims)
                                        if (lower(animation.name) == "idle") {
                                            const auto frames = atlas.value().framesFor(animation.atlasPrefix);
                                            if (!frames.empty()) frame = &atlas.value().frames[frames.front()];
                                            break;
                                        }
                                    if (frame->w > 0 && frame->h > 0) {
                                        uvMin = ImVec2(static_cast<float>(frame->x) / image.width,
                                                       static_cast<float>(frame->y) / image.height);
                                        uvMax = ImVec2(static_cast<float>(frame->x + frame->w) / image.width,
                                                       static_cast<float>(frame->y + frame->h) / image.height);
                                        sourceWidth = static_cast<float>(frame->w);
                                        sourceHeight = static_cast<float>(frame->h);
                                    }
                                }
                            }
                        }
                    }
                    const float fit = std::min(440.0f / std::max(1.0f, sourceWidth),
                                               310.0f / std::max(1.0f, sourceHeight));
                    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image.texture)),
                                 ImVec2(sourceWidth * fit, sourceHeight * fit), uvMin, uvMax);
                }
            }
        } else ImGui::TextDisabled("%s", app.spanish ? "Imagen no resuelta" : "Image not resolved");
        if (asset.kind == ModExplorerAsset::Kind::Stage)
            ImGui::TextDisabled("%s", app.spanish ? "Escena compuesta; personajes ocultos."
                                               : "Composed stage; characters hidden.");
        ImGui::TextWrapped("%s", asset.sourcePath.c_str());
        if (ImGui::Button(app.spanish ? "Abrir recurso" : "Open resource")) {
            selectAsset(app, app.quickMod, app.quickAsset);
            app.tab = asset.kind == ModExplorerAsset::Kind::Character ? 0 : 1;
            app.selectTabOnNextFrame = true;
        }
    }
    ImGui::End();
    if (!open) app.quickMod = app.quickAsset = -1;
}

void draw(AtlasApp& app, SDL_Window* window) {
    processDialog(app);
    songLabPollAudio(app);
    app.sheetBoundsValid = false;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("##funkin-atlas", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    drawHeader(app, window);
    if (ImGui::BeginTabBar("##sections", ImGuiTabBarFlags_None)) {
        const bool requested = app.selectTabOnNextFrame;
        const bool characterOpen = ImGui::BeginTabItem(app.spanish ? "Personajes###characters" : "Characters###characters", nullptr,
            requested && app.tab == 0 ? ImGuiTabItemFlags_SetSelected : 0);
        if (characterOpen) ImGui::EndTabItem();
        const bool stageOpen = ImGui::BeginTabItem(app.spanish ? "Escenarios###stages" : "Stages###stages", nullptr,
            requested && app.tab == 1 ? ImGuiTabItemFlags_SetSelected : 0);
        if (stageOpen) ImGui::EndTabItem();
        ImGui::EndTabBar();
        if (requested) app.selectTabOnNextFrame = false;
        else {
            const int activeTab = stageOpen ? 1 : characterOpen ? 0 : app.tab;
            if (app.tab != activeTab) {
                rememberTabState(app);
                app.tab = activeTab;
                if (!restoreTabState(app, activeTab)) ensureSelection(app);
            }
        }
    }
    const std::string missing = firstMissingGalleryKey(app);
    if (!missing.empty()) {
        ImGui::TextColored(ImVec4(0.96f, 0.69f, 0.42f, 1.0f), "%s", app.spanish ? "Falta una fuente guardada en la galería" : "A saved gallery source is missing");
        ImGui::SameLine();
        if (ImGui::SmallButton(app.spanish ? "Buscar carpeta nueva" : "Locate new folder")) {
            app.pendingRecoveryKey = missing;
            app.dialogAction = DialogAction::Recover;
            SDL_ShowOpenFolderDialog(dialogSelected, &app, window, nullptr, false);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(app.spanish ? "Quitar entrada" : "Remove entry")) {
            app.gallery.erase(std::remove(app.gallery.begin(), app.gallery.end(), missing), app.gallery.end());
            app.galleryRoots.erase(missing);
            app.engineOverrides.erase(missing);
            saveGallery(app);
        }
    }
    const float width = ImGui::GetContentRegionAvail().x;
    const float left = std::clamp(width * 0.25f, 290.0f, 355.0f);
    const float right = std::clamp(width * 0.23f, 285.0f, 335.0f);
    ImGui::BeginChild("##data", ImVec2(left, 0.0f), true);
    if (ImGui::BeginTabBar("##left-panels")) {
        if (ImGui::BeginTabItem(app.spanish ? "Vista###left-view" : "View###left-view")) {
            drawCharacterPairControls(app, window);
            drawLivePanelData(app);
            drawViewControls(app);
            const ModExplorerAsset* asset = selectedAsset(app);
            if (asset && asset->valid && asset->kind == ModExplorerAsset::Kind::Stage)
                drawStageSidebar(app, *app.mods[static_cast<size_t>(app.selectedMod)], *asset, window);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(app.spanish ? "Canciones###left-songs" : "Songs###left-songs")) {
            drawSongLab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(app.spanish ? "Recurso###left-resource" : "Resource###left-resource")) {
            drawDataPanel(app, window);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##workspace", ImVec2(-right - 10.0f, 0.0f), true);
    drawDetail(app, window);
    if (!app.capturePath.empty() && app.captureDetailScroll > 0)
        ImGui::SetScrollY(static_cast<float>(app.captureDetailScroll));
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##resources", ImVec2(0.0f, 0.0f), true);
    drawResourceList(app);
    ImGui::EndChild();
    if (!app.sheetBoundsValid) app.sheetWheel = 0.0f;
    ImGui::End();
}

}

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef FUNKIN_ATLAS_SONGLAB_EXPERIMENTAL
    SDL_Window* window = SDL_CreateWindow("Funkin Atlas - FML Tool — Song Lab (experimental)", 1360, 860,
#else
    SDL_Window* window = SDL_CreateWindow("Funkin Atlas - FML Tool", 1360, 860,
#endif
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { SDL_Quit(); return 2; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 3; }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f))
        ImGui::GetIO().Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(9.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.065f, 0.071f, 0.080f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.091f, 0.099f, 0.110f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.23f, 0.25f, 0.27f, 0.9f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.14f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.19f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.17f, 0.18f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.29f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.27f, 0.23f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.29f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.32f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.91f, 0.74f, 0.48f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.31f, 0.27f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.27f, 0.23f, 0.18f, 1.0f);
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 330");

    AtlasApp app;
    loadGallery(app);
    std::vector<std::string> roots;
    std::string requestedAsset;
    std::string requestedQuick;
    std::string requestedObject;
    std::string requestedExport;
    int captureFrames = 20;
    std::string requestedSong;
    std::string requestedBackdrop;
    std::string requestedSecond;
    int requestedSongLine = 0;
    int requestedSecondLine = 0;
    bool requestedShareLine = false;
    bool requestedStagePosition = false;
    bool requestedFlipY = false;
    bool requestedHideLiveSheet = false;
    std::array<std::string, 4> requestedSongActions;
    std::array<bool, 4> requestedSongActionSet{};
    std::string requestedSongIdle;
    bool requestedSongIdleSet = false;
    std::string requestedSaveActionPreset;
    std::string requestedLoadActionPreset;
    fs::path requestedExportDir;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument.rfind("--capture=", 0) == 0) app.capturePath = argument.substr(10);
        else if (argument.rfind("--capture-frames=", 0) == 0)
            captureFrames = std::clamp(std::atoi(argument.c_str() + 17), 20, 600);
        else if (argument.rfind("--song-lab-select=", 0) == 0)
            requestedSong = lower(argument.substr(18));
        else if (argument.rfind("--song-lab-line=", 0) == 0)
            requestedSongLine = std::max(0, std::atoi(argument.c_str() + 16));
        else if (argument.rfind("--song-lab-second-line=", 0) == 0)
            requestedSecondLine = std::max(0, std::atoi(argument.c_str() + 23));
        else if (argument == "--song-lab-share-line") requestedShareLine = true;
        else if (argument == "--song-lab-play") app.songLab.autoPlay = true;
        else if (argument.rfind("--backdrop=", 0) == 0)
            requestedBackdrop = lower(argument.substr(11));
        else if (argument == "--use-stage-position") requestedStagePosition = true;
        else if (argument == "--flip-y") requestedFlipY = true;
        else if (argument == "--hide-live-sheet") requestedHideLiveSheet = true;
        else if (argument.rfind("--song-action-left=", 0) == 0) {
            requestedSongActions[0] = argument.substr(19); requestedSongActionSet[0] = true;
        } else if (argument.rfind("--song-action-down=", 0) == 0) {
            requestedSongActions[1] = argument.substr(19); requestedSongActionSet[1] = true;
        } else if (argument.rfind("--song-action-up=", 0) == 0) {
            requestedSongActions[2] = argument.substr(17); requestedSongActionSet[2] = true;
        } else if (argument.rfind("--song-action-right=", 0) == 0) {
            requestedSongActions[3] = argument.substr(20); requestedSongActionSet[3] = true;
        } else if (argument.rfind("--song-action-idle=", 0) == 0) {
            requestedSongIdle = argument.substr(19); requestedSongIdleSet = true;
        } else if (argument.rfind("--song-preset-save=", 0) == 0)
            requestedSaveActionPreset = argument.substr(19);
        else if (argument.rfind("--song-preset-load=", 0) == 0)
            requestedLoadActionPreset = argument.substr(19);
        else if (argument.rfind("--song-lab-seek=", 0) == 0)
            app.songLab.initialSeekMs = std::max(0.0, std::atof(argument.c_str() + 16) * 1000.0);
        else if (argument.rfind("--capture-scroll=", 0) == 0)
            app.captureDetailScroll = std::max(0, std::atoi(argument.c_str() + 17));
        else if (argument.rfind("--root=", 0) == 0) roots.push_back(argument.substr(7));
        else if (argument.rfind("--manual-definition=", 0) == 0)
            std::snprintf(app.manualDefinition.data(), app.manualDefinition.size(), "%s", argument.c_str() + 20);
        else if (argument.rfind("--manual-image=", 0) == 0)
            std::snprintf(app.manualImage.data(), app.manualImage.size(), "%s", argument.c_str() + 15);
        else if (argument.rfind("--manual-atlas=", 0) == 0)
            std::snprintf(app.manualAtlas.data(), app.manualAtlas.size(), "%s", argument.c_str() + 15);
        else if (argument.rfind("--manual-spritemap=", 0) == 0)
            std::snprintf(app.manualSpritemap.data(), app.manualSpritemap.size(), "%s", argument.c_str() + 19);
        else if (argument.rfind("--manual-icon=", 0) == 0)
            std::snprintf(app.manualIcon.data(), app.manualIcon.size(), "%s", argument.c_str() + 14);
        else if (argument.rfind("--manual-engine=", 0) == 0) {
            const std::string engine = lower(argument.substr(16));
            app.manualEngine = engine == "psych" ? 1 : engine == "codename" ? 2 :
                               engine == "vslice" ? 3 : 0;
        }
        else if (argument == "--tab=stage") app.tab = 1;
        else if (argument == "--tab=character") app.tab = 0;
        else if (argument == "--lang=en") app.spanish = false;
        else if (argument == "--lang=es") app.spanish = true;
        else if (argument.rfind("--select=", 0) == 0) requestedAsset = lower(argument.substr(9));
        else if (argument.rfind("--second=", 0) == 0) requestedSecond = lower(argument.substr(9));
        else if (argument.rfind("--quick=", 0) == 0) requestedQuick = lower(argument.substr(8));
        else if (argument.rfind("--object=", 0) == 0) requestedObject = lower(argument.substr(9));
        else if (argument.rfind("--export-target=", 0) == 0) requestedExport = lower(argument.substr(16));
        else if (argument.rfind("--export-dir=", 0) == 0) requestedExportDir = fs::u8path(argument.substr(13));
        else if (argument.rfind("--gif-seconds=", 0) == 0)
            app.stageGifSeconds = std::clamp(std::atoi(argument.c_str() + 14), 1, 8);
        else if (argument.rfind("--gif-fps=", 0) == 0)
            app.stageGifFps = std::clamp(std::atoi(argument.c_str() + 10), 4, 24);
        else if (argument.rfind("--preview-gif-width=", 0) == 0)
            app.previewGifWidth = std::clamp(std::atoi(argument.c_str() + 20), 160, 1920);
        else if (argument.rfind("--preview-gif-height=", 0) == 0)
            app.previewGifHeight = std::clamp(std::atoi(argument.c_str() + 21), 90, 1080);
        else if (argument.rfind("--preview-gif-seconds=", 0) == 0)
            app.previewGifSeconds = std::clamp(std::atoi(argument.c_str() + 22), 1, 30);
        else if (argument.rfind("--preview-gif-fps=", 0) == 0)
            app.previewGifFps = std::clamp(std::atoi(argument.c_str() + 18), 4, 60);
        else if (argument.rfind("--preview-gif-max=", 0) == 0)
            app.previewGifMaxFrames = std::clamp(std::atoi(argument.c_str() + 18), 12, 600);
        else if (argument == "--include-stage-gifs") app.includeStageGifs = true;
        else if (argument.rfind("--", 0) != 0) roots.push_back(argument);
    }
    for (const std::string& root : roots) {
        const fs::path path = fs::u8path(root);
        std::error_code ec;
        if (fs::is_regular_file(path, ec) && lower(path.extension().u8string()) != ".zip")
            addSingleResource(app, path);
        else addMod(app, path);
    }
    if (app.manualDefinition[0] || app.manualImage[0]) addManualResource(app);
    if (!requestedAsset.empty()) {
        for (size_t mod = 0; mod < app.mods.size(); ++mod) {
            const auto& assets = app.mods[mod]->catalog.assets();
            for (size_t index = 0; index < assets.size(); ++index) {
                if (visible(app, static_cast<int>(mod), assets[index]) &&
                    lower(assets[index].id) == requestedAsset) {
                    selectAsset(app, static_cast<int>(mod), static_cast<int>(index));
                    mod = app.mods.size();
                    break;
                }
            }
        }
    }
    if (!requestedObject.empty() && selectedAsset(app) &&
        selectedAsset(app)->kind == ModExplorerAsset::Kind::Stage) {
        for (size_t i = 0; i < app.previewStage.objects.size(); ++i)
            if (lower(app.previewStage.objects[i].name) == requestedObject) {
                app.selectedStageObject = static_cast<int>(i);
                app.stageAnimationIndex = 0;
                break;
            }
    }
    if (!requestedSecond.empty() && selectedAsset(app) &&
        selectedAsset(app)->kind == ModExplorerAsset::Kind::Character) {
        const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
        for (size_t i = 0; i < assets.size(); ++i)
            if (assets[i].valid && assets[i].kind == ModExplorerAsset::Kind::Character &&
                lower(assets[i].id) == requestedSecond) {
                setSecondaryCharacter(app, static_cast<int>(i));
                break;
            }
    }
    if (!requestedQuick.empty()) {
        const bool stageOnly = requestedQuick.rfind("stage:", 0) == 0;
        const bool characterOnly = requestedQuick.rfind("character:", 0) == 0;
        const std::string quickId = stageOnly ? requestedQuick.substr(6) :
            characterOnly ? requestedQuick.substr(10) : requestedQuick;
        for (size_t mod = 0; mod < app.mods.size(); ++mod) {
            const auto& assets = app.mods[mod]->catalog.assets();
            for (size_t index = 0; index < assets.size(); ++index) {
                if (lower(assets[index].id) == quickId &&
                    (!stageOnly || assets[index].kind == ModExplorerAsset::Kind::Stage) &&
                    (!characterOnly || assets[index].kind == ModExplorerAsset::Kind::Character)) {
                    app.quickMod = static_cast<int>(mod);
                    app.quickAsset = static_cast<int>(index);
                    mod = app.mods.size();
                    break;
                }
            }
        }
    }
    if (!requestedSong.empty() && selectedAsset(app)) {
        songLabScan(app);
        for (size_t i = 0; i < app.songLab.songs.size(); ++i) {
            const SongLabEntry& entry = app.songLab.songs[i];
            const std::string folder = fs::u8path(entry.path).parent_path().filename().u8string();
            if (lower(entry.id + ":" + entry.difficulty) == requestedSong ||
                lower(folder + ":" + entry.difficulty) == requestedSong ||
                lower(fs::u8path(entry.path).stem().u8string()) == requestedSong) {
                songLabLoad(app, static_cast<int>(i));
                break;
            }
        }
        if (app.songLab.selected < 0)
            std::fprintf(stderr, "Song Lab chart not found: %s\n", requestedSong.c_str());
        else
            std::fprintf(stdout, "Song Lab chart: %s, %zu notes, %s\n",
                app.songLab.chart.songId.c_str(), app.songLab.chart.notes.size(),
                app.songLab.status.c_str());
        for (const std::string& path : app.songLab.audioPaths)
            std::fprintf(stdout, "Song Lab audio: %s\n", path.c_str());
        if (!app.songLab.audioWarning.empty())
            std::fprintf(stdout, "Song Lab warning: %s\n", app.songLab.audioWarning.c_str());
    }
    if (selectedAsset(app) && selectedAsset(app)->kind == ModExplorerAsset::Kind::Character) {
        app.flipY = requestedFlipY;
        if (requestedHideLiveSheet) app.showLiveSheet = false;
        const std::string actionKey = songLabActionKey(app, app.previewCharacter);
        if (!requestedLoadActionPreset.empty()) {
            const auto found = app.songAnimationPresets.find(actionKey);
            if (found != app.songAnimationPresets.end()) {
                const auto preset = std::find_if(found->second.begin(), found->second.end(),
                    [&](const SongAnimationPreset& candidate) {
                        return candidate.name == requestedLoadActionPreset;
                    });
                if (preset != found->second.end()) {
                    app.songAnimationActions[actionKey] = preset->actions;
                    app.songAnimationIdles[actionKey] = preset->idle;
                    std::fprintf(stdout, "Loaded song animation preset: %s\n", preset->name.c_str());
                } else std::fprintf(stderr, "Song animation preset not found: %s\n",
                                    requestedLoadActionPreset.c_str());
            }
        }
        for (int direction = 0; direction < 4; ++direction) {
            if (!requestedSongActionSet[static_cast<size_t>(direction)]) continue;
            std::string action = requestedSongActions[static_cast<size_t>(direction)];
            if (action == "default") action.clear();
            else if (action == "none") action = "__none__";
            if (!action.empty() && action != "__none__" &&
                std::none_of(app.previewCharacter.anims.begin(), app.previewCharacter.anims.end(),
                    [&](const AnimationDef& animation) { return animation.name == action; })) {
                std::fprintf(stderr, "Song animation not found: %s\n", action.c_str());
                continue;
            }
            app.songAnimationActions[actionKey][static_cast<size_t>(direction)] = std::move(action);
        }
        if (requestedSongIdleSet) {
            if (requestedSongIdle == "default") requestedSongIdle.clear();
            if (requestedSongIdle.empty() ||
                std::any_of(app.previewCharacter.anims.begin(), app.previewCharacter.anims.end(),
                    [&](const AnimationDef& animation) { return animation.name == requestedSongIdle; }))
                app.songAnimationIdles[actionKey] = requestedSongIdle;
            else std::fprintf(stderr, "Song idle animation not found: %s\n", requestedSongIdle.c_str());
        }
        if (!requestedSaveActionPreset.empty()) {
            auto& presets = app.songAnimationPresets[actionKey];
            auto existing = std::find_if(presets.begin(), presets.end(),
                [&](const SongAnimationPreset& candidate) {
                    return candidate.name == requestedSaveActionPreset;
                });
            bool stored = false;
            if (existing != presets.end()) {
                existing->actions = app.songAnimationActions[actionKey];
                existing->idle = app.songAnimationIdles[actionKey];
                stored = true;
            } else if (presets.size() < 32) {
                presets.push_back({requestedSaveActionPreset, app.songAnimationActions[actionKey],
                                   app.songAnimationIdles[actionKey]});
                stored = true;
            }
            if (stored && saveGallery(app))
                std::fprintf(stdout, "Saved song animation preset: %s\n",
                             requestedSaveActionPreset.c_str());
            else std::fprintf(stderr, "Could not save song animation preset: %s\n",
                              requestedSaveActionPreset.c_str());
        }
        app.shareStrumline = requestedShareLine;
        if (!requestedBackdrop.empty()) {
            const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
            for (size_t i = 0; i < assets.size(); ++i)
                if (assets[i].valid && assets[i].kind == ModExplorerAsset::Kind::Stage &&
                    lower(assets[i].id) == requestedBackdrop) {
                    app.characterBackdrop = static_cast<int>(i);
                    break;
                }
        }
        if (requestedSongLine > 0 && app.songLab.chartLoaded &&
            requestedSongLine <= static_cast<int>(app.songLab.chart.strumLines.size())) {
            app.characterSongLines[songLabCharacterLineKey(app)] = requestedSongLine - 1;
            app.characterStageRole = songLabStageRole(
                app.songLab.chart.strumLines[static_cast<size_t>(requestedSongLine - 1)]);
        }
        if (requestedSecondLine > 0 && app.secondaryAssetIndex >= 0 &&
            requestedSecondLine <= static_cast<int>(app.songLab.chart.strumLines.size()))
            app.characterSongLines[songLabCompanionLineKey(app)] = requestedSecondLine - 1;
        if (app.shareStrumline && requestedSongLine > 0)
            app.sharedStrumlineLine = requestedSongLine - 1;
        if (requestedStagePosition && app.characterBackdrop >= 0) app.characterUseStagePosition = true;
        if (app.characterBackdrop >= 0 || requestedStagePosition || requestedSongLine > 0) {
            buildCharacterStage(app);
            refreshPreviewCharacter(app);
        }
    }
    bool running = true;
    int exitCode = 0;
    if (!requestedExport.empty()) {
        const bool allowed = requestedExport == "psych" || requestedExport == "codename" ||
                             requestedExport == "vslice" ||
                             requestedExport == "previewgif" || requestedExport == "pairgifs" ||
                             requestedExport == "stagezip" || requestedExport == "stagegif" ||
                             requestedExport == "stagesheet" || requestedExport == "objectpng" ||
                             requestedExport == "stagepng" || requestedExport == "stagescenegif";
        const bool ok = !requestedExportDir.empty() && selectedAsset(app) && allowed &&
            (requestedExport == "previewgif" ? exportCharacterPreviewGif(app, requestedExportDir) :
             requestedExport == "pairgifs" ? [&]() {
                 if (app.secondaryAssetIndex < 0) return false;
                 const bool first = exportCharacterAnimation(app, DialogAction::AnimationGif, requestedExportDir);
                 switchActiveCharacter(app);
                 const bool second = exportCharacterAnimation(app, DialogAction::AnimationGif, requestedExportDir);
                 switchActiveCharacter(app);
                 return first && second;
             }() :
             requestedExport == "stagezip" ? exportStageZip(app, requestedExportDir) :
             requestedExport == "stagegif" ? exportStageGif(app, requestedExportDir) :
             requestedExport == "stagesheet" ? exportStageSheet(app, requestedExportDir) :
             requestedExport == "objectpng" ? exportStageObjectPng(app, requestedExportDir) :
             requestedExport == "stagepng" ? exportStageScenePng(app, requestedExportDir) :
             requestedExport == "stagescenegif" ? exportStageSceneGif(app, requestedExportDir) :
             requestedExport == "vslice" ? exportVSlice(app, requestedExportDir) :
             exportEngine(app, requestedExportDir, requestedExport == "psych"));
        std::fprintf(ok ? stdout : stderr, "%s\n", statusText(app).c_str());
        running = false;
        exitCode = ok ? 0 : 4;
    }
    int frameCount = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_MOUSE_WHEEL && app.previewBoundsValid &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
                event.wheel.windowID == SDL_GetWindowID(window) &&
                event.wheel.mouse_x >= app.previewBoundsMin.x &&
                event.wheel.mouse_x < app.previewBoundsMax.x &&
                event.wheel.mouse_y >= app.previewBoundsMin.y &&
                event.wheel.mouse_y < app.previewBoundsMax.y &&
                event.wheel.y != 0.0f) {
                app.previewWheel += event.wheel.y;
                app.previewWheelPos = ImVec2(event.wheel.mouse_x, event.wheel.mouse_y);
                continue;
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL && app.sheetBoundsValid &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
                event.wheel.windowID == SDL_GetWindowID(window) &&
                event.wheel.mouse_x >= app.sheetBoundsMin.x &&
                event.wheel.mouse_x < app.sheetBoundsMax.x &&
                event.wheel.mouse_y >= app.sheetBoundsMin.y &&
                event.wheel.mouse_y < app.sheetBoundsMax.y &&
                event.wheel.y != 0.0f) {
                app.sheetWheel += event.wheel.y;
                app.sheetWheelPos = ImVec2(event.wheel.mouse_x, event.wheel.mouse_y);
                continue;
            }
            bool liveSheetWheel = false;
            for (LiveSheetViewState& sheet : app.liveSheetViews) {
                if (event.type != SDL_EVENT_MOUSE_WHEEL || !sheet.boundsValid ||
                    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
                    event.wheel.windowID != SDL_GetWindowID(window) ||
                    event.wheel.mouse_x < sheet.boundsMin.x ||
                    event.wheel.mouse_x >= sheet.boundsMax.x ||
                    event.wheel.mouse_y < sheet.boundsMin.y ||
                    event.wheel.mouse_y >= sheet.boundsMax.y ||
                    event.wheel.y == 0.0f) continue;
                sheet.wheel += event.wheel.y;
                sheet.wheelPos = ImVec2(event.wheel.mouse_x, event.wheel.mouse_y);
                liveSheetWheel = true;
                break;
            }
            if (liveSheetWheel) continue;
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw(app, window);
        drawQuickPreview(app);
        ImGui::Render();
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.065f, 0.071f, 0.080f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!app.capturePath.empty() && ++frameCount >= captureFrames) {
            std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            stbi_flip_vertically_on_write(1);
            stbi_write_png(app.capturePath.c_str(), width, height, 4, pixels.data(), width * 4);
            stbi_flip_vertically_on_write(0);
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }
    if (app.capturePath.empty() && requestedExport.empty()) saveGallery(app);
    if (app.stagePoseTexture) glDeleteTextures(1, &app.stagePoseTexture);
    for (MountedViewCache& mounted : app.mountedViews)
        if (mounted.texture) glDeleteTextures(1, &mounted.texture);
    if (app.quickRenderMod >= 0) app.quickRenderer.shutdown();
    if (app.rendererReady) app.renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
