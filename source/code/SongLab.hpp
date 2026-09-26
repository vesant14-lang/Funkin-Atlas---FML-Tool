#pragma once

std::string songLabSourcesSignature(const AtlasApp& app) {
    std::string signature;
    for (const auto& mod : app.mods) signature += mod->catalog.root().u8string() + "|";
    return signature;
}

void songLabCatalogSource(SongLabEntry& song, const LoadedMod& mod) {
    song.sourceLabel = mod.label;
    const std::string path = Vfs::normalize(song.path);
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin < path.size()) {
        const size_t end = path.find('/', begin);
        if (end > begin) parts.push_back(path.substr(begin, end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (lower(parts[i]) == "mods" && lower(parts[i + 1]) != "_global")
            song.sourceLabel = parts[i + 1];
        if (lower(parts[i]) == "content" && i + 2 < parts.size() &&
            lower(parts[i + 1]) != "songs" && lower(parts[i + 1]) != "data")
            song.collectionLabel = parts[i + 1];
    }
}

std::string songLabSourceName(const SongLabEntry& song) {
    return song.collectionLabel.empty() ? song.sourceLabel
        : song.sourceLabel + " / " + song.collectionLabel;
}

void songLabScan(AtlasApp& app) {
    SongLabState& lab = app.songLab;
    lab.songs.clear();
    lab.selected = -1;
    lab.indexedSignature = songLabSourcesSignature(app);
    lab.indexed = true;
    std::set<std::string> seen;
    for (const auto& mod : app.mods) {
        const std::string rootPath = mod->catalog.root().u8string();
        const Vfs& vfs = mod->catalog.vfs();
        for (const Vfs::Entry& entry : vfs.allEntries()) {
            const std::string path = lower(Vfs::normalize(entry.virtualPath));
            if (path.rfind(".fml/", 0) == 0 || path.find("/.fml/") != std::string::npos ||
                path.rfind(".temp/", 0) == 0 || path.find("/.temp/") != std::string::npos ||
                path.rfind(".git/", 0) == 0 || path.find("/.git/") != std::string::npos)
                continue;
            if (fs::u8path(path).extension() != ".json" ||
                !seen.insert(lower(rootPath) + "|" + path).second) continue;
            const fs::path chartPath = fs::u8path(entry.virtualPath);
            const bool codenamePath = lower(chartPath.parent_path().filename().u8string()) == "charts" &&
                (path.rfind("songs/", 0) == 0 || path.find("/songs/") != std::string::npos);
            const bool vslicePath = path.size() > 11 && path.substr(path.size() - 11) == "-chart.json" &&
                (path.rfind("songs/", 0) == 0 || path.find("/songs/") != std::string::npos);
            const bool psychPath = path.rfind("data/", 0) == 0 ||
                path.find("/data/") != std::string::npos;
            if (!codenamePath && !vslicePath && !psychPath) continue;
            const auto source = vfs.readText(entry);
            if (!source) continue;
            const json root = json::parse(*source, nullptr, false, true);
            if (!root.is_object()) continue;
            if (codenamePath && root.contains("strumLines") && root["strumLines"].is_array()) {
                SongLabEntry candidate;
                candidate.format = SongLabEntry::Format::Codename;
                candidate.sourceRoot = rootPath;
                candidate.sourceLabel = mod->label;
                candidate.id = chartPath.parent_path().parent_path().filename().u8string();
                candidate.difficulty = chartPath.stem().u8string();
                candidate.path = entry.virtualPath;
                songLabCatalogSource(candidate, *mod);
                candidate.metadataPath = Vfs::normalize((chartPath.parent_path().parent_path() / "meta.json").u8string());
                candidate.stage = root.contains("stage") && root["stage"].is_string()
                    ? root["stage"].get<std::string>() : "";
                for (const json& line : root["strumLines"]) {
                    if (!line.is_object() || !line.contains("characters") ||
                        !line["characters"].is_array() || line["characters"].empty() ||
                        !line["characters"][0].is_string()) continue;
                    const std::string character = line["characters"][0].get<std::string>();
                    const std::string position = line.contains("position") && line["position"].is_string()
                        ? lower(line["position"].get<std::string>()) : "";
                    const int type = line.contains("type") && line["type"].is_number_integer()
                        ? line["type"].get<int>() : -1;
                    if (position == "boyfriend" || position == "bf" || type == 1)
                        candidate.player = character;
                    else if (position == "girlfriend" || position == "gf" || type == 2)
                        candidate.girlfriend = character;
                    else if (candidate.opponent.empty()) candidate.opponent = character;
                }
                lab.songs.push_back(std::move(candidate));
                continue;
            }
            if (codenamePath && root.contains("song") && root["song"].is_object() &&
                root["song"].contains("notes") && root["song"]["notes"].is_array()) {
                const json& header = root["song"];
                auto field = [&](const char* key) -> std::string {
                    return header.contains(key) && header[key].is_string()
                        ? header[key].get<std::string>() : "";
                };
                SongLabEntry candidate;
                candidate.format = SongLabEntry::Format::CodenameLegacy;
                candidate.sourceRoot = rootPath;
                candidate.sourceLabel = mod->label;
                candidate.id = chartPath.parent_path().parent_path().filename().u8string();
                candidate.difficulty = chartPath.stem().u8string();
                candidate.path = entry.virtualPath;
                songLabCatalogSource(candidate, *mod);
                candidate.metadataPath = Vfs::normalize(
                    (chartPath.parent_path().parent_path() / "meta.json").u8string());
                candidate.stage = field("stage");
                candidate.player = field("player1");
                candidate.opponent = field("player2");
                candidate.girlfriend = field("gfVersion");
                lab.songs.push_back(std::move(candidate));
                continue;
            }
            if (vslicePath && root.contains("notes") && root["notes"].is_object()) {
                const fs::path metaPath = chartPath.parent_path() /
                    fs::u8path(chartPath.stem().u8string().substr(0, chartPath.stem().u8string().size() - 6) + "-metadata.json");
                const auto metaText = vfs.readText(Vfs::normalize(metaPath.u8string()));
                if (!metaText) continue;
                const json meta = json::parse(*metaText, nullptr, false, true);
                if (!meta.is_object() || !meta.contains("playData") || !meta["playData"].is_object()) continue;
                const json& play = meta["playData"];
                const json characters = play.contains("characters") && play["characters"].is_object()
                    ? play["characters"] : json::object();
                auto stringField = [](const json& object, const char* key) -> std::string {
                    return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : "";
                };
                for (auto notes = root["notes"].begin(); notes != root["notes"].end(); ++notes) {
                    if (!notes.value().is_array()) continue;
                    SongLabEntry candidate;
                    candidate.format = SongLabEntry::Format::VSlice;
                    candidate.sourceRoot = rootPath;
                    candidate.sourceLabel = mod->label;
                    candidate.id = stringField(meta, "songName");
                    if (candidate.id.empty()) candidate.id = chartPath.stem().u8string().substr(0, chartPath.stem().u8string().size() - 6);
                    candidate.difficulty = notes.key();
                    candidate.path = entry.virtualPath;
                    songLabCatalogSource(candidate, *mod);
                    candidate.metadataPath = Vfs::normalize(metaPath.u8string());
                    candidate.stage = stringField(play, "stage");
                    candidate.player = stringField(characters, "player");
                    candidate.opponent = stringField(characters, "opponent");
                    candidate.girlfriend = stringField(characters, "girlfriend");
                    lab.songs.push_back(std::move(candidate));
                }
                continue;
            }
            if (!psychPath || !root.contains("song") || !root["song"].is_object()) continue;
            const json& song = root["song"];
            if (!song.contains("notes") || !song["notes"].is_array()) continue;
            auto value = [&](const char* key) -> std::string {
                return song.contains(key) && song[key].is_string() ? song[key].get<std::string>() : "";
            };
            SongLabEntry candidate;
            candidate.sourceRoot = rootPath;
            candidate.sourceLabel = mod->label;
            candidate.id = value("song");
            if (candidate.id.empty()) candidate.id = fs::u8path(entry.virtualPath).parent_path().filename().u8string();
            candidate.path = entry.virtualPath;
            songLabCatalogSource(candidate, *mod);
            candidate.stage = value("stage");
            candidate.player = value("player1");
            candidate.opponent = value("player2");
            candidate.girlfriend = value("gfVersion");
            if (candidate.girlfriend.empty()) candidate.girlfriend = value("player3");
            const std::string stem = lower(fs::u8path(entry.virtualPath).stem().u8string());
            const std::string folder = lower(fs::u8path(entry.virtualPath).parent_path().filename().u8string());
            candidate.difficulty = stem == folder ? "normal" :
                stem.rfind(folder + "-", 0) == 0 ? stem.substr(folder.size() + 1) : stem;
            lab.songs.push_back(std::move(candidate));
        }
    }
    std::sort(lab.songs.begin(), lab.songs.end(), [](const SongLabEntry& a, const SongLabEntry& b) {
        if (lower(a.id) != lower(b.id)) return lower(a.id) < lower(b.id);
        if (lower(a.difficulty) != lower(b.difficulty)) return lower(a.difficulty) < lower(b.difficulty);
        if (lower(a.sourceLabel) != lower(b.sourceLabel))
            return lower(a.sourceLabel) < lower(b.sourceLabel);
        if (lower(a.collectionLabel) != lower(b.collectionLabel))
            return lower(a.collectionLabel) < lower(b.collectionLabel);
        if (lower(a.sourceRoot) != lower(b.sourceRoot))
            return lower(a.sourceRoot) < lower(b.sourceRoot);
        return lower(a.path) < lower(b.path);
    });
    if (lab.chartLoaded)
        for (size_t i = 0; i < lab.songs.size(); ++i)
            if (lab.songs[i].sourceRoot == lab.activeRoot &&
                lab.songs[i].path == lab.activeSong.path &&
                lab.songs[i].difficulty == lab.activeSong.difficulty) {
                lab.selected = static_cast<int>(i);
                break;
            }
    if (!lab.chartLoaded) lab.status.clear();
}

bool songLabRelated(const ModExplorerAsset& asset, const SongLabEntry& entry) {
    const std::string id = lower(asset.id);
    if (asset.kind == ModExplorerAsset::Kind::Stage) return lower(entry.stage) == id;
    return lower(entry.player) == id || lower(entry.opponent) == id || lower(entry.girlfriend) == id;
}

void songLabSetActors(AtlasApp& app, const SongLabEntry& song) {
    const ModExplorerAsset* selected = selectedAsset(app);
    if (!selected || selected->kind != ModExplorerAsset::Kind::Stage || app.selectedMod < 0 ||
        song.sourceRoot != app.mods[static_cast<size_t>(app.selectedMod)]->catalog.root().u8string()) return;
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    const std::string ids[3] = {song.player, song.opponent, song.girlfriend};
    for (int role = 0; role < 3; ++role) {
        if (ids[role].empty()) continue;
        for (size_t i = 0; i < assets.size(); ++i) {
            if (assets[i].valid && assets[i].kind == ModExplorerAsset::Kind::Character &&
                lower(assets[i].id) == lower(ids[role])) {
                app.stageActors[role] = static_cast<int>(i);
                break;
            }
        }
    }
    app.animator.reset(app.previewStage);
}

std::string songLabPhysical(const Vfs& vfs, const std::string& virtualPath) {
    auto entry = vfs.find(virtualPath);
    if (!entry) for (const Vfs::Entry& candidate : vfs.allEntries())
        if (lower(Vfs::normalize(candidate.virtualPath)) == lower(Vfs::normalize(virtualPath))) {
            entry = candidate;
            break;
        }
    if (!entry || entry->fromArchive) return {};
    const auto path = vfs.resolve(*entry);
    return path ? path->u8string() : std::string();
}

void songLabSetAudioMode(SongLabState& lab) {
    for (size_t i = 0; i < lab.audio.trackCount(); ++i)
        lab.audio.setTrackGain(i, static_cast<int>(i) == lab.instTrack
            ? (lab.audioMode == 1 ? 0.0f : lab.instVolume)
            : (lab.audioMode == 0 ? 0.0f : lab.voicesVolume));
    lab.audio.setMasterGain(lab.masterVolume);
}

std::string songLabAudioFile(const Vfs& vfs, const std::vector<std::string>& folders,
                             const std::vector<std::string>& names) {
    for (const std::string& folder : folders)
        for (const std::string& name : names) {
            const std::string found = songLabPhysical(vfs, folder + "/" + name);
            if (!found.empty()) return found;
        }
    return {};
}

void songLabLoad(AtlasApp& app, int index) {
    SongLabState& lab = app.songLab;
    if (index < 0 || index >= static_cast<int>(lab.songs.size())) return;
    const SongLabEntry& song = lab.songs[static_cast<size_t>(index)];
    const LoadedMod* sourceMod = nullptr;
    for (const auto& mod : app.mods)
        if (mod->catalog.root().u8string() == song.sourceRoot) {
            sourceMod = mod.get();
            break;
        }
    if (!sourceMod) return;
    app.showStageCharacters = false;
    lab.audio.pause();
    lab.audio.clearTracks();
    lab.audioPaths.clear();
    lab.audioWarning.clear();
    lab.audioMode = 2;
    lab.chartLoaded = lab.audioPending = lab.audioLoaded = false;
    lab.activeRoot.clear();
    lab.activeLabel.clear();
    lab.activeSong = {};
    lab.instTrack = -1;
    lab.previousMs = -1.0;
    lab.viewDirty = true;
    lab.wasPlaying = false;
    lab.nextNote = 0;
    lab.selected = index;
    const LoadedMod& mod = *sourceMod;
    const Vfs& vfs = mod.catalog.vfs();
    const auto source = vfs.readText(song.path);
    DiagnosticSink diagnostics;
    if (!source) { lab.status = app.spanish ? "No se pudo leer el chart." : "Could not read the chart."; return; }
    Result<UniversalChart> parsed = Result<UniversalChart>::fail("Unsupported chart format");
    SongMeta codenameMeta;
    if (song.format == SongLabEntry::Format::Codename ||
        song.format == SongLabEntry::Format::CodenameLegacy) {
        codenameMeta = parseSongMeta(vfs.readText(song.metadataPath).value_or(""), song.id,
                                     song.metadataPath, diagnostics);
        if (song.format == SongLabEntry::Format::CodenameLegacy)
            parsed = parseLegacyChart(*source, song.path, diagnostics);
        else {
            CodenameChartDefaults defaults;
            defaults.songId = song.id;
            defaults.displayName = codenameMeta.displayName;
            defaults.bpm = codenameMeta.bpm;
            defaults.beatsPerMeasure = codenameMeta.beatsPerMeasure;
            defaults.stepsPerBeat = codenameMeta.stepsPerBeat;
            defaults.needsVoices = codenameMeta.needsVoices;
            parsed = parseCodenameChart(*source, song.path, defaults, diagnostics);
        }
    } else if (song.format == SongLabEntry::Format::VSlice) {
        const auto metadata = vfs.readText(song.metadataPath);
        if (!metadata) {
            lab.status = app.spanish ? "Faltan metadatos V-Slice." : "V-Slice metadata is missing.";
            return;
        }
        auto bundle = importFunkinBaseChartText(*source, *metadata, song.path, diagnostics);
        if (!bundle) { lab.status = bundle.error(); return; }
        for (auto& candidate : bundle.value().candidates)
            if (candidate.difficulty == song.difficulty) {
                parsed = Result<UniversalChart>::ok(std::move(candidate.chart));
                break;
            }
    } else parsed = parseLegacyChart(*source, song.path, diagnostics);
    if (!parsed) { lab.status = parsed.error(); return; }
    lab.chart = std::move(parsed.value());
    lab.timeMap.build(lab.chart);
    lab.chartLoaded = true;
    lab.activeRoot = mod.catalog.root().u8string();
    lab.activeLabel = song.id + " · " + song.difficulty;
    lab.activeSong = song;
    songLabSetActors(app, song);
    if (!lab.audio.ready()) {
        std::string error;
        if (!lab.audio.init(&error)) { lab.status = error; return; }
    }
    lab.audio.setRate(app.previewSpeed);
    const fs::path chartPath = fs::u8path(song.path);
    const std::string chartStem = chartPath.stem().u8string();
    const std::string folder = chartPath.parent_path().filename().u8string();
    std::vector<std::string> tracks;
    std::string inst, voices;
    if (song.format == SongLabEntry::Format::Psych) {
        const std::vector<fs::path> roots = psych::roots(mod.catalog.root());
        const fs::path realChart = mod.catalog.root() / chartPath;
        const psych::SongAudio resolved = psych::resolveSongAudio(roots, realChart, folder);
        inst = songLabPhysical(vfs, "songs/" + chartStem + "/Inst.ogg");
        if (inst.empty()) inst = resolved.instPath;
        if (inst.empty()) inst = songLabPhysical(vfs, "songs/" + folder + "/Inst.ogg");
        voices = songLabPhysical(vfs, "songs/" + chartStem + "/Voices.ogg");
        if (voices.empty()) voices = resolved.voicesPath;
        if (!inst.empty()) { lab.instTrack = 0; tracks.push_back(inst); }
        if (!voices.empty()) tracks.push_back(voices);
        if (voices.empty()) {
            if (!resolved.playerVoicesPath.empty()) tracks.push_back(resolved.playerVoicesPath);
            if (!resolved.opponentVoicesPath.empty()) tracks.push_back(resolved.opponentVoicesPath);
        }
    } else {
        const bool codenameAudio = song.format == SongLabEntry::Format::Codename ||
            song.format == SongLabEntry::Format::CodenameLegacy;
        const fs::path songDirectory = codenameAudio
            ? chartPath.parent_path().parent_path()
            : chartPath.parent_path();
        std::vector<std::string> audioFolders;
        if (codenameAudio)
            audioFolders.push_back(Vfs::normalize((songDirectory / "song").u8string()));
        const std::string songKey = songDirectory.filename().u8string();
        const std::string path = Vfs::normalize(songDirectory.u8string());
        const std::string lowered = lower(path);
        const size_t dataSongs = lowered.rfind("/data/songs/");
        if (lowered.rfind("data/songs/", 0) == 0)
            audioFolders.push_back("songs/" + songKey);
        else if (dataSongs != std::string::npos)
            audioFolders.push_back(path.substr(0, dataSongs) + "/songs/" + songKey);
        audioFolders.push_back(Vfs::normalize(songDirectory.u8string()));
        std::string suffix = codenameAudio
            ? codenameMeta.instSuffix : "";
        if (suffix.empty() && song.format == SongLabEntry::Format::VSlice) {
            const auto metadata = vfs.readText(song.metadataPath);
            const json meta = json::parse(metadata.value_or("{}"), nullptr, false, true);
            if (meta.is_object() && meta.contains("playData") && meta["playData"].is_object()) {
                const json& play = meta["playData"];
                if (play.contains("characters") && play["characters"].is_object() &&
                    play["characters"].contains("instrumental") &&
                    play["characters"]["instrumental"].is_string())
                    suffix = play["characters"]["instrumental"].get<std::string>();
            }
        }
        const std::string difficulty = song.difficulty;
        std::vector<std::string> instNames;
        if (!suffix.empty()) instNames.push_back("Inst" + suffix + ".ogg");
        if (!suffix.empty()) instNames.push_back("Inst-" + suffix + ".ogg");
        instNames.push_back("Inst-" + difficulty + ".ogg");
        instNames.push_back("Inst" + difficulty + ".ogg");
        instNames.push_back("Inst.ogg");
        inst = songLabAudioFile(vfs, audioFolders, instNames);
        if (codenameAudio) suffix = codenameMeta.vocalsSuffix;
        std::vector<std::string> voicesNames;
        if (!suffix.empty()) voicesNames.push_back("Voices" + suffix + ".ogg");
        if (!suffix.empty()) voicesNames.push_back("Voices-" + suffix + ".ogg");
        voicesNames.push_back("Voices-" + difficulty + ".ogg");
        voicesNames.push_back("Voices" + difficulty + ".ogg");
        voicesNames.push_back("Voices.ogg");
        voices = songLabAudioFile(vfs, audioFolders, voicesNames);
        if (!inst.empty()) { lab.instTrack = 0; tracks.push_back(inst); }
        if (!voices.empty()) tracks.push_back(voices);
        if (voices.empty()) for (const std::string& singer : {song.player, song.opponent}) {
            if (singer.empty()) continue;
            const std::string split = songLabAudioFile(vfs, audioFolders,
                {"Voices-" + singer + ".ogg", "Voices_" + singer + ".ogg"});
            if (!split.empty()) tracks.push_back(split);
        }
        if (!inst.empty() && lower(fs::u8path(inst).stem().u8string()) == "inst" &&
            lower(difficulty) != "normal" && lower(difficulty) != "hard")
            lab.audioWarning = app.spanish
                ? "No se encontró Inst específica; se usa la pista base."
                : "No difficulty-specific Inst was found; using the base track.";
    }
    if (tracks.empty()) {
        lab.status = app.spanish ? "Chart cargado, pero no se encontró audio físico. ZIP aún no soportado en Song Lab."
                                 : "Chart loaded, but no physical audio was found. ZIP is not supported in Song Lab yet.";
        return;
    }
    if (song.format == SongLabEntry::Format::Psych && song.difficulty != "hard" && song.difficulty != "normal" && !inst.empty() &&
        lower(fs::u8path(inst).parent_path().filename().u8string()) != lower(chartStem)) {
        lab.audioWarning = app.spanish
            ? "No hay Inst separada para esta dificultad; se usa la pista base. Comprueba si el mod la comparte."
            : "No separate Inst for this difficulty; using base audio. Check whether the mod intentionally shares it.";
    }
    lab.audioPaths = tracks;
    lab.audio.loadTracksAsync(tracks);
    lab.audioPending = true;
    lab.status = app.spanish ? "Decodificando audio..." : "Decoding audio...";
}

bool songLabViewActive(const AtlasApp& app) {
    return app.songLab.chartLoaded && app.songLab.audioLoaded && selectedAsset(app) != nullptr;
}

std::string songLabCharacterLineKey(const AtlasApp& app) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character || app.selectedMod < 0)
        return {};
    return identity(*app.mods[static_cast<size_t>(app.selectedMod)], *asset) + "|" +
           app.songLab.activeRoot + "|" + app.songLab.activeSong.path;
}

std::string songLabCompanionLineKey(const AtlasApp& app) {
    if (app.selectedMod < 0 || app.secondaryAssetIndex < 0) return {};
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    if (app.secondaryAssetIndex >= static_cast<int>(assets.size())) return {};
    return identity(*app.mods[static_cast<size_t>(app.selectedMod)],
                    assets[static_cast<size_t>(app.secondaryAssetIndex)]) + "|" +
           app.songLab.activeRoot + "|" + app.songLab.activeSong.path;
}

int songLabStoredLine(const AtlasApp& app, const std::string& key) {
    if (key.empty()) return -1;
    const auto found = app.characterSongLines.find(key);
    return found == app.characterSongLines.end() || found->second < -2 ||
           found->second >= static_cast<int>(app.songLab.chart.strumLines.size())
        ? -1 : found->second;
}

int songLabCharacterLine(const AtlasApp& app) {
    if (app.secondaryAssetIndex >= 0 && app.shareStrumline)
        return app.sharedStrumlineLine;
    return songLabStoredLine(app, songLabCharacterLineKey(app));
}

int songLabCompanionLine(const AtlasApp& app) {
    if (app.secondaryAssetIndex < 0) return -1;
    if (app.shareStrumline) return app.sharedStrumlineLine;
    return songLabStoredLine(app, songLabCompanionLineKey(app));
}

int songLabStageRole(const ChartStrumLine& line) {
    const std::string position = lower(line.position);
    if (position == "girlfriend" || position == "gf") return 2;
    if (position == "boyfriend" || position == "bf") return 0;
    if (position == "dad" || position == "opponent") return 1;
    return line.type == 2 ? 2 : line.type == 1 ? 0 : 1;
}

struct SongLabSinger {
    const UniversalCharacter* character = nullptr;
    int object = -1;
};

SongLabSinger songLabSinger(AtlasApp& app, const CharacterBinding& binding, const ChartNote& note) {
    const SongLabState& lab = app.songLab;
    const int line = note.singerStrumLine >= 0 ? note.singerStrumLine : note.strumLine;
    const int role = line >= 0 && line < static_cast<int>(lab.chart.strumLines.size())
        ? songLabStageRole(lab.chart.strumLines[static_cast<size_t>(line)])
        : line == 2 ? 2 : note.isPlayer ? 0 : 1;
    const bool gf = role == 2;
    const bool player = role == 0;
    const UniversalCharacter* character = gf ? binding.girlfriend : player ? binding.player : binding.opponent;
    const StageObject::Kind kind = gf ? StageObject::Kind::Girlfriend :
        player ? StageObject::Kind::Player : StageObject::Kind::Opponent;
    int object = app.animator.objectIndexOfKind(kind);
    const ModExplorerAsset* selected = selectedAsset(app);
    if (selected && selected->kind == ModExplorerAsset::Kind::Character) {
        const int chosenLine = songLabCharacterLine(app);
        if (chosenLine == -1) return {};
        if (chosenLine >= 0) {
            if (line != chosenLine) return {};
        } else {
            bool matches = false;
            if (line >= 0 && line < static_cast<int>(lab.chart.strumLines.size()))
                for (const std::string& id : lab.chart.strumLines[static_cast<size_t>(line)].characters)
                    if (lower(id) == lower(selected->id)) { matches = true; break; }
            if (!matches) {
                const std::string singerId = lower(gf ? lab.chart.gfVersion :
                    player ? lab.chart.player1 : lab.chart.player2);
                matches = lower(selected->id) == singerId;
            }
            if (!matches) return {};
        }
        character = &app.previewCharacter;
        object = app.characterMarkerIndex;
    }
    return {character, object};
}

SongLabSinger songLabCompanionSinger(AtlasApp& app, const ChartNote& note) {
    if (app.secondaryAssetIndex < 0 || app.secondaryMarkerIndex < 0 ||
        app.selectedMod < 0) return {};
    const int chosenLine = songLabCompanionLine(app);
    if (chosenLine == -1) return {};
    const int line = note.singerStrumLine >= 0 ? note.singerStrumLine : note.strumLine;
    if (chosenLine >= 0 && line != chosenLine) return {};
    if (chosenLine == -2) {
        const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
        if (app.secondaryAssetIndex >= static_cast<int>(assets.size())) return {};
        const std::string id = lower(assets[static_cast<size_t>(app.secondaryAssetIndex)].id);
        bool matches = false;
        if (line >= 0 && line < static_cast<int>(app.songLab.chart.strumLines.size()))
            for (const std::string& character : app.songLab.chart.strumLines[static_cast<size_t>(line)].characters)
                if (lower(character) == id) { matches = true; break; }
        if (!matches) return {};
    }
    return {&app.previewSecondaryCharacter, app.secondaryMarkerIndex};
}

std::string songLabNoteAnimation(const ChartNote& note) {
    if (note.direction < 0 || note.direction > 3) return {};
    const char* names[4] = {"singLEFT", "singDOWN", "singUP", "singRIGHT"};
    return std::string(names[note.direction]) +
        (lower(note.type) == "alt anim note" ? "-alt" : "");
}

std::string songLabActionKey(const AtlasApp& app, const UniversalCharacter& character) {
    if (app.selectedMod < 0 || app.selectedMod >= static_cast<int>(app.mods.size())) return {};
    const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
    int index = -1;
    if (&character == &app.previewCharacter) index = app.selectedAsset;
    else if (&character == &app.previewSecondaryCharacter) index = app.secondaryAssetIndex;
    if (index < 0 || index >= static_cast<int>(assets.size()) ||
        assets[static_cast<size_t>(index)].kind != ModExplorerAsset::Kind::Character) return {};
    return identity(*app.mods[static_cast<size_t>(app.selectedMod)],
                    assets[static_cast<size_t>(index)]);
}

std::string songLabActionName(const AtlasApp& app, const UniversalCharacter& character,
                              const ChartNote& note) {
    const std::string key = songLabActionKey(app, character);
    const auto found = app.songAnimationActions.find(key);
    if (note.direction >= 0 && note.direction < 4 &&
        found != app.songAnimationActions.end() &&
        !found->second[static_cast<size_t>(note.direction)].empty())
        return found->second[static_cast<size_t>(note.direction)];
    return songLabNoteAnimation(note);
}

const AnimationDef* songLabAnimation(const AtlasApp& app, const UniversalCharacter& character,
                                      const ChartNote& note) {
    const std::string requested = songLabActionName(app, character, note);
    if (requested == "__none__") return nullptr;
    const std::string base = requested.substr(0, requested.find('-'));
    for (const AnimationDef& animation : character.anims)
        if (animation.name == requested) return &animation;
    for (const AnimationDef& animation : character.anims)
        if (animation.name == base) return &animation;
    return nullptr;
}

int songLabAnimationFrames(AtlasApp& app, const UniversalCharacter& character,
                           const AnimationDef& animation) {
    if (animation.resolvedFrames > 0) return animation.resolvedFrames;
    int total = 0;
    if (AtlasStore::isAnimatePath(character.resolvedAtlas)) {
        const AnimateAtlas* atlas = app.atlases.getAnimate(character.resolvedAtlas);
        total = atlas ? atlas->frameCount(animation.atlasPrefix) : 0;
    } else {
        const SparrowAtlas* atlas = app.atlases.get(character.resolvedAtlas);
        total = atlas ? (animation.allAtlasFrames
            ? static_cast<int>(atlas->frames.size())
            : static_cast<int>(atlas->framesFor(animation.atlasPrefix).size())) : 0;
    }
    if (animation.indices.empty()) return total;
    int selected = 0;
    for (int index : animation.indices)
        if (index >= 0 && index < total) ++selected;
    return selected;
}

double songLabHoldBeats(AtlasApp& app, const UniversalCharacter& character,
                        const ChartNote& note) {
    const SongLabState& lab = app.songLab;
    double base = character.id == "pico-speakers" ? 0.5 : std::max(0.25,
        static_cast<double>(character.holdTime > 0.0f ? character.holdTime : 4.0f) /
        static_cast<double>(std::max(1, lab.chart.stepsPerBeat)));
    if (const AnimationDef* animation = songLabAnimation(app, character, note)) {
        const int frames = songLabAnimationFrames(app, character, *animation);
        if (frames > 0 && animation->fps > 0) {
            const double durationMs = frames * 1000.0 / animation->fps;
            base = std::max(base, lab.timeMap.beatAt(note.timeMs + durationMs) -
                                  lab.timeMap.beatAt(note.timeMs));
        }
    }
    return base + std::max(0.0, lab.timeMap.beatAt(note.timeMs + note.sustainMs) -
                               lab.timeMap.beatAt(note.timeMs));
}

bool songLabSingOn(AtlasApp& app, StageAnimator& animator, const SongLabSinger& singer,
                   const ChartNote& note, double holdBeats, double elapsedMs = 0.0) {
    if (!singer.character || singer.object < 0 || note.direction < 0 || note.direction > 3 ||
        lower(note.type) == "no anim note") return false;
    const std::string requested = songLabActionName(app, *singer.character, note);
    if (requested == "__none__") return false;
    const std::string base = requested.substr(0, requested.find('-'));
    bool played = animator.sing(static_cast<size_t>(singer.object), *singer.character,
                                requested, holdBeats, elapsedMs);
    if (!played && requested != base)
        played = animator.sing(static_cast<size_t>(singer.object), *singer.character,
                               base, holdBeats, elapsedMs);
    return played;
}

bool songLabSing(AtlasApp& app, const SongLabSinger& singer, const ChartNote& note,
                 double holdBeats, double elapsedMs = 0.0) {
    return songLabSingOn(app, app.animator, singer, note, holdBeats, elapsedMs);
}

void songLabPollAudio(AtlasApp& app) {
    SongLabState& lab = app.songLab;
    if (lab.audioPending) {
        int loaded = 0;
        std::string error;
        if (lab.audio.pollTrackLoad(&loaded, &error)) {
            lab.audioPending = false;
            lab.audioLoaded = loaded > 0;
            lab.audio.seekMs(lab.initialSeekMs);
            songLabSetAudioMode(lab);
            if (lab.loopSong && lab.audioLoaded)
                lab.audio.setLoop(0.0, lab.audio.durationMs());
            if (lab.audioLoaded && lab.autoPlay) lab.audio.play();
            lab.status = loaded > 0 ? (app.spanish ? "Audio listo." : "Audio ready.") : error;
        }
    }
}

bool songLabUpdate(AtlasApp& app, const CharacterBinding& binding) {
    SongLabState& lab = app.songLab;
    if (!songLabViewActive(app)) return false;
    const ModExplorerAsset* selected = selectedAsset(app);
    const bool hasCharacterLine = selected && selected->kind == ModExplorerAsset::Kind::Character &&
        (songLabCharacterLine(app) != -1 || songLabCompanionLine(app) != -1);
    if (!lab.audio.playing() ||
        (selected && selected->kind == ModExplorerAsset::Kind::Character && !hasCharacterLine)) {
        if (lab.wasPlaying) {
            app.animator.reset(app.previewStage);
            if (selected && selected->kind == ModExplorerAsset::Kind::Character) {
                if (!app.previewCharacter.anims.empty())
                    app.animator.play(static_cast<size_t>(app.characterMarkerIndex),
                        std::clamp(app.animationIndex, 0,
                                   static_cast<int>(app.previewCharacter.anims.size()) - 1));
                if (app.secondaryMarkerIndex >= 0 && !app.previewSecondaryCharacter.anims.empty())
                    app.animator.play(static_cast<size_t>(app.secondaryMarkerIndex),
                        std::clamp(app.secondaryAnimationIndex, 0,
                                   static_cast<int>(app.previewSecondaryCharacter.anims.size()) - 1));
            }
            app.sequenceToken = -1;
            app.secondarySequenceToken = -1;
            lab.viewDirty = true;
            lab.previousMs = -1.0;
        }
        lab.wasPlaying = false;
        return false;
    }
    lab.wasPlaying = true;
    const double ms = std::max(0.0, lab.audio.positionMs());
    const double beat = lab.timeMap.beatAt(ms);
    const bool resync = lab.viewDirty || lab.previousMs < 0.0 ||
        ms + 50.0 < lab.previousMs || ms > lab.previousMs + 1000.0;
    if (resync) {
        app.animator.resync(beat, ms);
        lab.nextNote = static_cast<size_t>(std::upper_bound(lab.chart.notes.begin(), lab.chart.notes.end(), ms + 0.01,
            [](double when, const ChartNote& note) { return when < note.timeMs; }) - lab.chart.notes.begin());
    }
    app.animator.updateWithBeat(app.previewStage, binding, app.atlases, ms, beat, lab.audio.playing());
    if (resync) {
        std::map<int, std::pair<const ChartNote*, SongLabSinger>> active;
        for (size_t i = 0; i < lab.nextNote; ++i) {
            const ChartNote& note = lab.chart.notes[i];
            if (note.direction < 0 || note.direction > 3 || lower(note.type) == "no anim note") continue;
            const SongLabSinger singer = songLabSinger(app, binding, note);
            const SongLabSinger companion = selected &&
                selected->kind == ModExplorerAsset::Kind::Character
                ? songLabCompanionSinger(app, note) : SongLabSinger{};
            for (const SongLabSinger& candidate : {singer, companion}) {
                if (!candidate.character || candidate.object < 0) continue;
                const double endBeat = lab.timeMap.beatAt(note.timeMs) +
                    songLabHoldBeats(app, *candidate.character, note);
                if (endBeat > beat) active[candidate.object] = {&note, candidate};
            }
        }
        for (const auto& [object, current] : active) {
            const ChartNote& note = *current.first;
            const double remaining = lab.timeMap.beatAt(note.timeMs) +
                songLabHoldBeats(app, *current.second.character, note) - beat;
            songLabSing(app, current.second, note, remaining, ms - note.timeMs);
        }
        if (!active.empty())
            app.animator.updateWithBeat(app.previewStage, binding, app.atlases, ms, beat, true);
        lab.viewDirty = false;
        lab.previousMs = ms;
        return true;
    }
    while (lab.nextNote < lab.chart.notes.size() && lab.chart.notes[lab.nextNote].timeMs <= ms + 0.01) {
        const ChartNote& note = lab.chart.notes[lab.nextNote++];
        if (note.timeMs < lab.previousMs - 10.0) continue;
        const SongLabSinger singer = songLabSinger(app, binding, note);
        if (singer.character && singer.object >= 0)
            songLabSing(app, singer, note, songLabHoldBeats(app, *singer.character, note));
        if (selected && selected->kind == ModExplorerAsset::Kind::Character) {
            const SongLabSinger companion = songLabCompanionSinger(app, note);
            if (companion.character && companion.object >= 0)
                songLabSing(app, companion, note,
                    songLabHoldBeats(app, *companion.character, note));
        }
    }
    lab.previousMs = ms;
    return true;
}

void drawSongLabActorStatus(AtlasApp& app) {
    const ModExplorerAsset* asset = selectedAsset(app);
    SongLabState& lab = app.songLab;
    if (!asset || asset->kind != ModExplorerAsset::Kind::Stage || !lab.audioLoaded) return;
    const bool es = app.spanish;
    ImGui::SeparatorText(es ? "ANIMACIONES EN VIVO" : "LIVE ANIMATIONS");
    const CharacterBinding binding = previewBinding(app);
    const UniversalCharacter* characters[3] = {
        binding.player, binding.opponent, binding.girlfriend};
    const StageObject::Kind kinds[3] = {
        StageObject::Kind::Player, StageObject::Kind::Opponent, StageObject::Kind::Girlfriend};
    const char* namesEs[3] = {"Jugador", "Rival", "GF"};
    const char* namesEn[3] = {"Player", "Opponent", "GF"};
    for (int role = 0; role < 3; ++role) {
        const UniversalCharacter* character = characters[role];
        if (!character) continue;
        const int object = app.animator.objectIndexOfKind(kinds[role]);
        if (object < 0) continue;
        const int index = app.animator.animIndexOf(static_cast<size_t>(object));
        if (index < 0 || index >= static_cast<int>(character->anims.size())) continue;
        const AnimationDef& animation = character->anims[static_cast<size_t>(index)];
        const int frames = songLabAnimationFrames(app, *character, animation);
        const int frame = app.animator.frameOf(static_cast<size_t>(object));
        ImGui::TextColored(animation.name.rfind("sing", 0) == 0
                ? ImVec4(0.40f, 0.89f, 0.65f, 1.0f)
                : ImVec4(0.60f, 0.61f, 0.64f, 1.0f),
            "%s · %s: %s  %d/%d", es ? namesEs[role] : namesEn[role],
            character->id.c_str(), animation.name.c_str(),
            frames > 0 ? std::clamp(frame + 1, 1, frames) : 0, frames);
    }
}

void drawSongAnimationActions(AtlasApp& app) {
    const ModExplorerAsset* asset = selectedAsset(app);
    if (!asset || asset->kind != ModExplorerAsset::Kind::Character) return;
    const bool es = app.spanish;
    if (!ImGui::CollapsingHeader(es ? "Acciones al cantar" : "Singing actions",
                               ImGuiTreeNodeFlags_DefaultOpen)) return;
    const char* directionsEs[4] = {"Izquierda", "Abajo", "Arriba", "Derecha"};
    const char* directionsEn[4] = {"Left", "Down", "Up", "Right"};
    const char* defaults[4] = {"singLEFT", "singDOWN", "singUP", "singRIGHT"};
    auto drawCharacter = [&](const UniversalCharacter& character) {
        const std::string key = songLabActionKey(app, character);
        if (key.empty()) return;
        ImGui::PushID(key.c_str());
        ImGui::Text("%s", character.id.c_str());
        auto& actions = app.songAnimationActions[key];
        for (int direction = 0; direction < 4; ++direction) {
            const std::string& action = actions[static_cast<size_t>(direction)];
            const std::string chosen = action.empty()
                ? std::string(es ? "Normal · " : "Default · ") + defaults[direction]
                : action == "__none__" ? (es ? "Sin animación" : "No animation") : action;
            ImGui::TextDisabled("%s", es ? directionsEs[direction] : directionsEn[direction]);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::PushID(direction);
            if (ImGui::BeginCombo("##note-action", chosen.c_str())) {
                bool changed = false;
                if (ImGui::Selectable(es ? "Normal" : "Default", action.empty()))
                    { actions[static_cast<size_t>(direction)].clear(); changed = true; }
                if (ImGui::Selectable(es ? "Sin animación" : "No animation", action == "__none__"))
                    { actions[static_cast<size_t>(direction)] = "__none__"; changed = true; }
                for (const AnimationDef& animation : character.anims)
                    if (ImGui::Selectable(animation.name.c_str(), action == animation.name))
                        { actions[static_cast<size_t>(direction)] = animation.name; changed = true; }
                ImGui::EndCombo();
                if (changed) { app.songLab.viewDirty = true; saveGallery(app); }
            }
            ImGui::PopID();
        }
        auto& presets = app.songAnimationPresets[key];
        ImGui::TextDisabled("%s", es ? "Preset guardado" : "Saved preset");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##load-action-preset", es ? "Elegir preset" : "Choose preset")) {
            for (const SongAnimationPreset& preset : presets)
                if (ImGui::Selectable(preset.name.c_str())) {
                    actions = preset.actions;
                    app.songAnimationDraftNames[key] = preset.name;
                    app.songLab.viewDirty = true;
                    saveGallery(app);
                }
            ImGui::EndCombo();
        }
        std::array<char, 97> name{};
        std::snprintf(name.data(), name.size(), "%s", app.songAnimationDraftNames[key].c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##action-preset-name",
                es ? "Nombre del preset" : "Preset name", name.data(), name.size()))
            app.songAnimationDraftNames[key] = name.data();
        if (ImGui::Button(es ? "Guardar preset" : "Save preset")) {
            const std::string presetName = app.songAnimationDraftNames[key];
            if (presetName.empty()) {
                setStatus(app, "Ponle nombre al preset.", "Give the preset a name.");
            } else {
                auto existing = std::find_if(presets.begin(), presets.end(),
                    [&](const SongAnimationPreset& preset) { return preset.name == presetName; });
                bool stored = false;
                if (existing != presets.end()) { existing->actions = actions; stored = true; }
                else if (presets.size() < 32) { presets.push_back({presetName, actions}); stored = true; }
                else setStatus(app, "Límite de 32 presets por personaje.",
                                   "Limit of 32 presets per character.");
                if (stored) {
                    if (saveGallery(app))
                        setStatus(app, "Preset guardado: " + presetName,
                                       "Preset saved: " + presetName);
                    else setStatus(app, "No se pudo guardar el preset.",
                                        "Could not save the preset.");
                }
            }
        }
        ImGui::PopID();
    };
    drawCharacter(app.previewCharacter);
    if (app.secondaryAssetIndex >= 0) {
        ImGui::Separator();
        drawCharacter(app.previewSecondaryCharacter);
    }
}

void drawSongLab(AtlasApp& app) {
    SongLabState& lab = app.songLab;
    const ModExplorerAsset* asset = selectedAsset(app);
    const bool es = app.spanish;
    const bool hasCatalog = !app.mods.empty();
    const bool hasAsset = asset && app.selectedMod >= 0;
    if (!hasCatalog && !lab.chartLoaded && !lab.audioPending) return;
    const std::string root = hasAsset
        ? app.mods[static_cast<size_t>(app.selectedMod)]->catalog.root().u8string() : std::string();
    if (hasCatalog && (!lab.indexed || lab.indexedSignature != songLabSourcesSignature(app))) songLabScan(app);
    ImGui::SeparatorText(es ? "Canciones cargadas" : "Loaded songs");
    ImGui::TextDisabled(es ? "%zu entradas disponibles" : "%zu available entries", lab.songs.size());
    if (hasCatalog) {
        size_t related = 0;
        if (hasAsset) for (const SongLabEntry& song : lab.songs)
            if (song.sourceRoot == root && songLabRelated(*asset, song)) ++related;
        const bool hasSelection = lab.selected >= 0 && lab.selected < static_cast<int>(lab.songs.size());
        const std::string chosen = hasSelection
            ? lab.songs[static_cast<size_t>(lab.selected)].id + " · " +
              lab.songs[static_cast<size_t>(lab.selected)].difficulty + " · " +
              songLabSourceName(lab.songs[static_cast<size_t>(lab.selected)])
            : lab.chartLoaded ? lab.activeLabel + (lab.activeRoot != root
                ? (es ? " (otro mod)" : " (another mod)") : "")
            : (es ? "Elige una canción para escucharla" : "Choose a song to play it");
        ImGui::TextDisabled("%s", es ? "Canción y dificultad" : "Song and difficulty");
        std::set<std::string> sourceMods;
        for (const SongLabEntry& song : lab.songs) sourceMods.insert(song.sourceLabel);
        ImGui::TextDisabled("%s", es ? "Mod de origen" : "Source mod");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##song-mod-filter", lab.modFilter.empty()
                ? (es ? "Todos los mods" : "All mods") : lab.modFilter.c_str())) {
            if (ImGui::Selectable(es ? "Todos los mods" : "All mods", lab.modFilter.empty()))
                lab.modFilter.clear();
            for (const std::string& source : sourceMods)
                if (ImGui::Selectable(source.c_str(), lab.modFilter == source))
                    lab.modFilter = source;
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##song-lab-selection", chosen.c_str())) {
            ImGui::InputTextWithHint("##song-lab-filter", es ? "Buscar canción" : "Search songs",
                                     lab.filter.data(), lab.filter.size());
            int visibleCount = 0;
            for (size_t i = 0; i < lab.songs.size(); ++i) {
                const SongLabEntry& song = lab.songs[i];
                if (!lab.modFilter.empty() && song.sourceLabel != lab.modFilter) continue;
                if (lab.onlyRelated && (!hasAsset || song.sourceRoot != root ||
                    !songLabRelated(*asset, song))) continue;
                if (lab.filter[0] && lower(song.id + " " + song.difficulty + " " +
                    song.sourceLabel + " " + song.collectionLabel)
                    .find(lower(lab.filter.data())) == std::string::npos)
                    continue;
                ++visibleCount;
                ImGui::PushID(static_cast<int>(i));
                const std::string label = song.id + " · " + song.difficulty + " · " +
                    songLabSourceName(song);
                if (ImGui::Selectable(label.c_str(), lab.selected == static_cast<int>(i))) {
                    lab.autoPlay = true;
                    lab.initialSeekMs = 0.0;
                    songLabLoad(app, static_cast<int>(i));
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", song.path.c_str());
                ImGui::PopID();
            }
            if (!visibleCount) ImGui::TextDisabled("%s", es ? "No hay canciones con este filtro." : "No songs match this filter.");
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox(es ? "Solo relacionadas" : "Related only", &lab.onlyRelated) &&
            lab.onlyRelated && !hasAsset) lab.onlyRelated = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", es
            ? "Desactiva para ver las canciones de todos los mods cargados."
            : "Turn off to see songs from every loaded mod.");
        ImGui::SameLine();
        if (ImGui::SmallButton(es ? "Actualizar" : "Refresh")) songLabScan(app);
        if (lab.onlyRelated) {
            if (!related) ImGui::TextDisabled("%s", es ? "No hay canciones vinculadas; desactiva Solo relacionadas."
                                                   : "No linked songs; turn off Related only.");
        } else if (lab.songs.empty()) {
            ImGui::TextDisabled("%s", es ? "No se encontraron charts Psych, Codename o V-Slice."
                                         : "No Psych, Codename or V-Slice charts were found.");
        }
    } else if (lab.chartLoaded) {
        ImGui::TextDisabled("%s: %s", es ? "Canción activa" : "Active song", lab.activeLabel.c_str());
    }
    if (asset && asset->kind == ModExplorerAsset::Kind::Character && lab.chartLoaded) {
        if (app.secondaryAssetIndex >= 0) {
            if (ImGui::Checkbox(es ? "Compartir strumline entre ambos" : "Share strumline between both",
                                &app.shareStrumline) && app.shareStrumline)
                app.sharedStrumlineLine = songLabStoredLine(app, songLabCharacterLineKey(app));
        }
        const int chosenLine = songLabCharacterLine(app);
        std::string preview = chosenLine == -2
            ? (es ? "Automático (según el chart)" : "Auto (from chart)")
            : (es ? "Sin strumline" : "No strumline");
        if (chosenLine >= 0 && chosenLine < static_cast<int>(lab.chart.strumLines.size())) {
            const ChartStrumLine& line = lab.chart.strumLines[static_cast<size_t>(chosenLine)];
            preview = std::to_string(chosenLine + 1) + " · " +
                (songLabStageRole(line) == 0 ? (es ? "Jugador" : "Player") :
                 songLabStageRole(line) == 2 ? "GF" : (es ? "Rival" : "Opponent"));
        }
        ImGui::TextDisabled("%s", es ? "Strumline del personaje" : "Character strumline");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##character-strumline", preview.c_str())) {
            const std::string key = songLabCharacterLineKey(app);
            auto chooseLine = [&](int line) {
                if (app.secondaryAssetIndex >= 0 && app.shareStrumline)
                    app.sharedStrumlineLine = line;
                else if (line == -1) app.characterSongLines.erase(key);
                else app.characterSongLines[key] = line;
                lab.viewDirty = true;
            };
            if (ImGui::Selectable(es ? "Sin strumline" : "No strumline", chosenLine == -1)) {
                chooseLine(-1);
            }
            if (ImGui::Selectable(es ? "Automático (según el chart)" : "Auto (from chart)", chosenLine == -2)) {
                chooseLine(-2);
            }
            for (size_t i = 0; i < lab.chart.strumLines.size(); ++i) {
                const ChartStrumLine& line = lab.chart.strumLines[i];
                std::string label = std::to_string(i + 1) + " · " +
                    (songLabStageRole(line) == 0 ? (es ? "Jugador" : "Player") :
                     songLabStageRole(line) == 2 ? "GF" : (es ? "Rival" : "Opponent"));
                for (const std::string& id : line.characters) label += " · " + id;
                if (ImGui::Selectable(label.c_str(), chosenLine == static_cast<int>(i))) {
                    chooseLine(static_cast<int>(i));
                    app.characterStageRole = songLabStageRole(line);
                    if (app.characterUseStagePosition && app.characterBackdrop >= 0) {
                        buildCharacterStage(app);
                        refreshPreviewCharacter(app);
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (app.secondaryAssetIndex >= 0 && !app.shareStrumline) {
            const auto& assets = app.mods[static_cast<size_t>(app.selectedMod)]->catalog.assets();
            const std::string key = songLabCompanionLineKey(app);
            const int line = songLabCompanionLine(app);
            std::string shown = line == -2 ? (es ? "Automático" : "Auto")
                : line < 0 ? (es ? "Sin strumline" : "No strumline")
                : std::to_string(line + 1);
            if (line >= 0 && line < static_cast<int>(lab.chart.strumLines.size())) {
                const ChartStrumLine& selectedLine = lab.chart.strumLines[static_cast<size_t>(line)];
                shown += " · ";
                shown += songLabStageRole(selectedLine) == 0 ? (es ? "Jugador" : "Player") :
                    songLabStageRole(selectedLine) == 2 ? "GF" : (es ? "Rival" : "Opponent");
                for (const std::string& id : selectedLine.characters) shown += " · " + id;
            }
            if (app.secondaryAssetIndex < static_cast<int>(assets.size()))
                ImGui::TextDisabled("%s: %s", es ? "Strumline del segundo personaje" : "Second character strumline",
                    assets[static_cast<size_t>(app.secondaryAssetIndex)].id.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##companion-strumline", shown.c_str())) {
                if (ImGui::Selectable(es ? "Sin strumline" : "No strumline", line == -1)) {
                    app.characterSongLines.erase(key); lab.viewDirty = true;
                }
                if (ImGui::Selectable(es ? "Automático (según el chart)" : "Auto (from chart)", line == -2)) {
                    app.characterSongLines[key] = -2; lab.viewDirty = true;
                }
                for (size_t i = 0; i < lab.chart.strumLines.size(); ++i) {
                    const ChartStrumLine& option = lab.chart.strumLines[i];
                    std::string label = std::to_string(i + 1) + " · " +
                        (songLabStageRole(option) == 0 ? (es ? "Jugador" : "Player") :
                         songLabStageRole(option) == 2 ? "GF" : (es ? "Rival" : "Opponent"));
                    for (const std::string& id : option.characters) label += " · " + id;
                    if (ImGui::Selectable(label.c_str(), line == static_cast<int>(i))) {
                        app.characterSongLines[key] = static_cast<int>(i);
                        lab.viewDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", es
            ? "Por defecto no canta. Elige una línea o usa coincidencia automática."
            : "The character does not sing by default. Choose a line or chart-based matching.");
    }
    if (asset && asset->kind == ModExplorerAsset::Kind::Character)
        drawSongAnimationActions(app);
    if (lab.chartLoaded) {
        if (lab.audioLoaded) {
            if (ImGui::Button(lab.audio.playing() ? (es ? "Pausar canción" : "Pause song")
                                                 : (es ? "Reproducir canción" : "Play song"))) {
                if (lab.audio.playing()) lab.audio.pause();
                else {
                    if (lab.audio.positionMs() >= lab.audio.durationMs() - 30.0) lab.audio.seekMs(0.0);
                    lab.audio.play();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(es ? "Desde el inicio" : "From start")) {
                lab.audio.seekMs(0.0);
                lab.previousMs = -1.0;
                lab.audio.play();
            }
            if (lab.instTrack >= 0 && lab.audio.trackCount() > 1) {
                const char* modes[] = {"Inst", "Voices", es ? "Ambos" : "Both"};
                ImGui::TextDisabled("%s", es ? "Pistas" : "Tracks");
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##song-lab-audio-mode", &lab.audioMode, modes, 3)) songLabSetAudioMode(lab);
            } else {
                ImGui::TextDisabled("%s", lab.instTrack >= 0 ? "Inst" : "Voices");
            }
            float position = static_cast<float>(lab.audio.positionMs() / 1000.0);
            const float duration = static_cast<float>(std::max(lab.audio.durationMs(), lab.chart.lastNoteMs()) / 1000.0);
            ImGui::TextDisabled("%s", es ? "Tiempo" : "Time");
            ImGui::SetNextItemWidth(-1.0f);
            if (duration > 0.0f && ImGui::SliderFloat("##song-lab-time", &position, 0.0f, duration, "%.1f s")) {
                lab.audio.seekMs(position * 1000.0);
                lab.previousMs = -1.0;
            }
            ImGui::TextDisabled("%s", es ? "Volumen" : "Volume");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##song-lab-volume", &lab.masterVolume, 0.0f, 1.5f, "%.2fx"))
                songLabSetAudioMode(lab);
            if (ImGui::Checkbox(es ? "Repetir" : "Loop", &lab.loopSong)) {
                if (lab.loopSong) lab.audio.setLoop(0.0, lab.audio.durationMs());
                else lab.audio.clearLoop();
            }
            if (ImGui::TreeNodeEx(es ? "Mezcla de pistas" : "Track mix")) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("Inst", &lab.instVolume, 0.0f, 1.5f, "%.2fx"))
                    songLabSetAudioMode(lab);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("Voices", &lab.voicesVolume, 0.0f, 1.5f, "%.2fx"))
                    songLabSetAudioMode(lab);
                ImGui::TreePop();
            }
        } else if (lab.audioPending) {
            ImGui::TextDisabled("%s", es ? "Preparando audio..." : "Preparing audio...");
        }
        if (ImGui::TreeNodeEx(es ? "Detalles de canción" : "Song details")) {
            ImGui::TextWrapped("%s: %s", es ? "Mod" : "Mod", songLabSourceName(lab.activeSong).c_str());
            ImGui::TextWrapped("%s: %s", es ? "Chart de origen" : "Source chart", lab.activeSong.path.c_str());
            ImGui::Text("%zu %s · %s", lab.chart.notes.size(), es ? "notas" : "notes", lab.chart.stage.c_str());
            for (const std::string& path : lab.audioPaths) {
                const fs::path physical = fs::u8path(path);
                const std::string label = physical.parent_path().filename().u8string() + "/" +
                                          physical.filename().u8string();
                ImGui::TextDisabled("%s", label.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
            }
            ImGui::TextDisabled("%s", es ? "Sin scripts, cinemáticas ni cambios de fase."
                                         : "No scripts, cutscenes, or phase changes.");
            ImGui::TreePop();
        }
    }
    if (!lab.status.empty() && (!lab.audioLoaded || lab.status != (es ? "Audio listo." : "Audio ready.")))
        ImGui::TextWrapped("%s", lab.status.c_str());
    if (!lab.audioWarning.empty()) ImGui::TextWrapped("%s", lab.audioWarning.c_str());
}
