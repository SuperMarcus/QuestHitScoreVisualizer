#include "../include/main.hpp"
#include "../include/config.hpp"
#include "../include/notification.h"
#include "../include/audio-manager.hpp"
#include "../include/texture-manager.hpp"
#include <ctime>
#include "../extern/beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include <sstream>
#include <strstream>

#define CONFIG_BACKUP_PATH CONFIG_PATH "HitScoreVisualizer_backup.json"

static HSVConfig config;
static AudioManager audioManager;
static TextureManager textureManager;
static NotificationBox notification;
static bool configValid = true;

#define HANDLE_CONFIG_FAILURE(condition) if (!condition) { \
    log(ERROR, "Config failed to load properly! Pushing notification..."); \
    log(ERROR, "Assertion caught in: %s %s.%d", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
    configValid = false; \
    notification.pushNotification("Config failed to load properly! Please ensure your JSON was configured correctly!"); \
}

std::optional<int> getBestJudgment(std::vector<judgment>& judgments, int comparison) {
    static auto length = config.judgments.size();
    if (length == 0) {
        return std::nullopt;
    }
    int i;
    for (i = 0; i < length; i++) {
        if (comparison >= config.judgments[i].threshold) {
            break;
        }
    }
    log(DEBUG, "bestIndex: %d", i);
    return i;
}

std::optional<segment> getBestSegment(std::vector<segment>& segments, int comparison) {
    auto size = segments.size();
    if (size == 0) {
        return std::nullopt;
    }
    auto& best = segments[0];
    for (int i = 1; i < size; i++) {
        if (comparison >= segments[i].threshold) {
            break;
        }
        best = segments[i];
    }
    return best;
}

Il2CppString* parseFormattedText(judgment best, int beforeCut, int afterCut, int cutDistance) {
    std::stringstream ststr;
    log(DEBUG, "%d, %d, %d", beforeCut, afterCut, cutDistance);
    bool isPercent = false;
    // TODO: Can make this EVEN FASTER by getting a list of indices of where the %'s are ONCE
    // And then replacing the corresponding indices with them as we iterate
    for (auto itr = best.text->begin(); itr != best.text->end(); ++itr) {
        auto current = *itr;
        if (isPercent) {
            // If the last character was a %
            std::string buffer;
            std::optional<segment> out;
            switch (current) {
                case 'b':
                    ststr << beforeCut;
                    break;
                case 'c':
                    ststr << cutDistance;
                    break;
                case 'a':
                    ststr << afterCut;
                    break;
                case 'B':
                    out = getBestSegment(config.beforeCutAngleJudgments, beforeCut);
                    break;
                case 'C':
                    out = getBestSegment(config.accuracyJudgments, cutDistance);
                    break;
                case 'A':
                    out = getBestSegment(config.afterCutAngleJudgments, afterCut);
                    break;
                case 's':
                    ststr << (beforeCut + afterCut + cutDistance);
                    break;
                case 'p':
                    ststr.precision(2);
                    ststr << std::fixed << (beforeCut + afterCut + cutDistance) / 115.0f * 100.0;
                    break;
                case 'n':
                    ststr << "\n";
                    break;
                case '%':
                default:
                    ststr << "%%" << current;
            }
            if (out) {
                ststr << *(out->text);
            }
            isPercent = false;
            continue;
        }
        if (current == '%' && !isPercent) {
            isPercent = true;
        } else {
            ststr.put(current);
        }
    }
    return il2cpp_utils::createcsstr(ststr.str().data());
}

std::optional<Color> fadeBetween(judgment from, judgment to, int score, Color initialColor) {
    auto reverseLerp = *RET_NULLOPT_UNLESS(il2cpp_utils::RunMethod<float>("UnityEngine", "Mathf", "InverseLerp", (float)from.threshold, (float)to.threshold, (float)score));
    
    auto fadeToColor = *RET_NULLOPT_UNLESS(to.color);
    return RET_NULLOPT_UNLESS(il2cpp_utils::RunMethod<Color>("UnityEngine", "Color", "Lerp", initialColor, fadeToColor, reverseLerp));
}

bool addColor(Il2CppObject* flyingScoreEffect, int bestIndex, int score) {
    log(DEBUG, "Adding color for bestIndex: %d, score: %d", bestIndex, score);
    std::optional<Color> color = config.judgments[bestIndex].color;
    if (config.judgments[bestIndex].fade.value_or(false) && bestIndex != 0 && color) {
        color = fadeBetween(config.judgments[bestIndex], config.judgments[bestIndex - 1], score, *color);
    }

    if (color) {
        RET_F_UNLESS(il2cpp_utils::SetFieldValue(flyingScoreEffect, "_color", *color));
    }
    return true;
}

bool addText(Il2CppObject* flyingScoreEffect, judgment best, int beforeCut, int afterCut, int cutDistance) {
    Il2CppObject* text = RET_F_UNLESS(il2cpp_utils::GetFieldValue(flyingScoreEffect, "_text").value_or(nullptr));

    // Runtime invoke set_richText to true
    RET_F_UNLESS(il2cpp_utils::SetPropertyValue(text, "richText", true));

    // Runtime invoke set_enableWordWrapping false
    RET_F_UNLESS(il2cpp_utils::SetPropertyValue(text, "enableWordWrapping", false));

    // Runtime invoke set_overflowMode to OverflowModes.Overflow (0)
    RET_F_UNLESS(il2cpp_utils::SetPropertyValue(text, "overflowMode", 0));

    Il2CppString* judgment_cs = nullptr;
    if (config.displayMode == DISPLAY_MODE_FORMAT) {
        RET_F_UNLESS(best.text);
        judgment_cs = parseFormattedText(best, beforeCut, afterCut, cutDistance);
    } else if (config.displayMode == DISPLAY_MODE_NUMERIC) {
        RET_F_UNLESS(best.text);
        // Numeric display ONLY
        judgment_cs = *RET_F_UNLESS(il2cpp_utils::GetPropertyValue<Il2CppString*>(text, "text"));
    } else if (config.displayMode == DISPLAY_MODE_SCOREONTOP) {
        RET_F_UNLESS(best.text);
        // Score on top
        // Add newline
        judgment_cs = *RET_F_UNLESS(il2cpp_utils::GetPropertyValue<Il2CppString*>(text, "text"));
        // Faster to convert the old text, use c++ strings, convert it back than it is to use C# string.Concat twice (with two created C# strings)
        judgment_cs = il2cpp_utils::createcsstr(to_utf8(csstrtostr(judgment_cs)) + "\n" + *best.text);
    } else if (config.displayMode == DISPLAY_MODE_TEXTONLY) {
        // Will not display images, use formatted display
        RET_F_UNLESS(best.text);
        judgment_cs = parseFormattedText(best, beforeCut, afterCut, cutDistance);
    } else if (config.displayMode == DISPLAY_MODE_TEXTONTOP) {
        // Text on top
        judgment_cs = *RET_F_UNLESS(il2cpp_utils::GetPropertyValue<Il2CppString*>(text, "text"));
        // Faster to convert the old text, use c++ strings, convert it back then it is to use C# string.Concat (although only marginally)
        judgment_cs = il2cpp_utils::createcsstr(*best.text + "\n" + to_utf8(csstrtostr(judgment_cs)));
    }
    else if (config.displayMode == DISPLAY_MODE_IMAGEONLY) {
        // Skip
    } else if (config.displayMode == DISPLAY_MODE_IMAGEANDTEXT) {
        RET_F_UNLESS(best.text);
    }

    // Set text if it is not null
    RET_F_UNLESS(judgment_cs);
    RET_F_UNLESS(il2cpp_utils::SetPropertyValue(text, "text", judgment_cs));
    log(DEBUG, "Using text: %s", best.text->data());
    return true;
}

bool addImage(Il2CppObject* flyingScoreEffect, judgment best, int beforeCut, int afterCut, int cutDistance) {
    if (config.displayMode == DISPLAY_MODE_IMAGEONLY) {
        RET_F_UNLESS(best.imagePath);
    } else if (config.displayMode == DISPLAY_MODE_IMAGEANDTEXT) {
        if (!best.imagePath) {
            // Simply use fallback text if no image is provided
            return true;
        }
    } else {
        // Don't display an image
        return true;
    }
    static auto spriteRendererType = il2cpp_utils::GetSystemType("UnityEngine", "SpriteRenderer");
    RET_F_UNLESS(spriteRendererType);
    Il2CppObject* existing = *RET_F_UNLESS(il2cpp_utils::RunMethod(flyingScoreEffect, "GetComponent", spriteRendererType));
    if (existing) {
        // Already displaying an image, no need to fail
        return true;
    }
    // SpriteRenderer spriteRender = flyingScoreEffect.AddComponent(typeof(SpriteRenderer));
    existing = RET_F_UNLESS(il2cpp_utils::RunMethod(flyingScoreEffect, "AddComponent", spriteRendererType).value_or(nullptr));
    // Get texture
    auto texture = textureManager.GetTexture(*best.imagePath).value_or(nullptr);
    if (texture) {
        auto width = *RET_F_UNLESS(il2cpp_utils::GetPropertyValue<int>(texture, "width"));
        auto height = *RET_F_UNLESS(il2cpp_utils::GetPropertyValue<int>(texture, "height"));
        auto rect = (Rect){0.0f, 0.0f, (float)width, (float)height};
        auto pivot = (Vector2){0.5f, 0.5f};
        // Sprite sprite = Sprite.Create(texture, rect, pivot, 1024f, 1u, 0);
        auto sprite = RET_F_UNLESS(il2cpp_utils::RunMethod("UnityEngine", "Sprite", "Create", texture, rect, pivot, 1024.0f, 1u, 0).value_or(nullptr));
        // spriteRenderer.set_sprite(sprite);
        RET_F_UNLESS(il2cpp_utils::SetPropertyValue(existing, "sprite", sprite));
        if (best.color) {
            // spriteRenderer.set_color(color);
            RET_F_UNLESS(il2cpp_utils::SetPropertyValue(existing, "color", *best.color));
        }
    }
    // TODO: Add segment images here
    return true;
}

bool addAudio(Il2CppObject* textObj, judgment toPlay) {
    // Check to see if we have an AudioSource already.
    // If we do, that means we already started our audio, so don't make another
    // If we don't make one and play audio on it.
    if (!toPlay.soundPath) {
        // No need to fail with logs if we didn't even want to add a sound
        return true;
    }
    static auto audioSourceType = il2cpp_utils::GetSystemType("UnityEngine", "AudioSource");
    RET_F_UNLESS(audioSourceType);
    Il2CppObject* existing = *RET_F_UNLESS(il2cpp_utils::RunMethod(textObj, "GetComponent", audioSourceType));
    if (existing) {
        // Already playing a sound, no need to fail
        return true;
    }
    existing = RET_F_UNLESS(il2cpp_utils::RunMethod(textObj, "AddComponent", audioSourceType).value_or(nullptr));
    // Get audio clip
    auto clip = audioManager.GetAudioClip(toPlay.soundPath->data()).value_or(nullptr);
    if (clip) {
        RET_F_UNLESS(il2cpp_utils::RunMethod(existing, "PlayOneShot", clip, toPlay.soundVolume.value_or(1.0f)));
    }
    return true;
}

void checkJudgments(Il2CppObject* flyingScoreEffect, int beforeCut, int afterCut, int cutDistance) {
    log(DEBUG, "Checking judgments!");
    if (!configValid) {
        return;
    }
    auto bestIndex = getBestJudgment(config.judgments, beforeCut + afterCut + cutDistance);
    RET_V_UNLESS(bestIndex);
    log(DEBUG, "Index: %d", *bestIndex);
    auto best = config.judgments[*bestIndex];

    log(DEBUG, "Adding color");
    if (!addColor(flyingScoreEffect, *bestIndex, beforeCut + afterCut + cutDistance)) {
        log(ERROR, "Failed to add color!");
    }

    log(DEBUG, "Adding image");
    if (!addImage(flyingScoreEffect, best, beforeCut, afterCut, cutDistance)) {
        log(ERROR, "Failed to add image!");
    }

    log(DEBUG, "Adding text");
    if (!addText(flyingScoreEffect, best, beforeCut, afterCut, cutDistance)) {
        log(ERROR, "Failed to add text!");
    }

    log(DEBUG, "Adding sound");
    if (!addAudio(flyingScoreEffect, best)) {
        log(ERROR, "Failed to add audio!");
    }
    log(DEBUG, "Complete with judging!");
}
// Checks season, sets config to correct season
void setConfigToCurrentSeason() {
    if (config.useSeasonalThemes) {
        // Check season
        time_t now = std::time(nullptr);
        tm* currentTm = std::localtime(&now);
        log(DEBUG, "Current datetime: (%i/%i)", currentTm->tm_mon, currentTm->tm_mday);
        // Christmas is 1 + 11 month, 23 - 25 day
        if (currentTm->tm_mon == 11 && (currentTm->tm_mday >= 23 && currentTm->tm_mday <= 24)) {
            if (config.backupBeforeSeason) {
                log(DEBUG, "Backing up config before seasonal swap...");
                // Before we backup, we must first ensure we have written out the correct info
                // So we don't have to fail to parse it again in the future
                config.WriteToConfig(Configuration::config);
                ConfigHelper::BackupConfig(Configuration::config, CONFIG_BACKUP_PATH);
            }
            log(DEBUG, "Setting to Christmas config!");
            config.SetToSeason(CONFIG_TYPE_CHRISTMAS);
        } else {
            if (config.type != CONFIG_TYPE_STANDARD) {
                // Otherwise, set to standard - iff config.restoreAfterSeason is set and there is a viable backup
                if (config.restoreAfterSeason && fileexists(CONFIG_BACKUP_PATH)) {
                    log(DEBUG, "Restoring config from: %s", CONFIG_BACKUP_PATH);
                    ConfigHelper::RestoreConfig(CONFIG_BACKUP_PATH);
                    HANDLE_CONFIG_FAILURE(ConfigHelper::LoadConfig(config, Configuration::config));
                    // Delete the old path to ensure we don't load from it again
                    deletefile(CONFIG_BACKUP_PATH);
                } else if (config.restoreAfterSeason) {
                    // If there isn't a viable backup, but we want to restore, set to default
                    log(DEBUG, "Setting config to default from type: %i", config.type);
                    config.SetToDefault();
                }
            }
        }
        log(DEBUG, "Writing Config to JSON file!");
        config.WriteToConfig(Configuration::config);
        log(DEBUG, "Writing Config JSON to file!");
        Configuration::Write();
    }
}

void loadConfig() {
    log(INFO, "Loading Configuration...");
    Configuration::Load();
    HANDLE_CONFIG_FAILURE(ConfigHelper::LoadConfig(config, Configuration::config));
    if (config.VersionLessThanEqual(2, 4, 0) || config.type == CONFIG_TYPE_CHRISTMAS) {
        // Let's just auto fix everyone's configs that are less than or equal to 2.4.0 or are of Christmas type
        log(DEBUG, "Setting to default because version <= 2.4.0! Actual: %i.%i.%i", config.majorVersion, config.minorVersion, config.patchVersion);
        config.SetToDefault();
        config.WriteToConfig(Configuration::config);
        configValid = true;
    }
    log(INFO, "Loaded Configuration! Metadata: type: %i, useSeasonalThemes: %c, restoreAfterSeason: %c", config.type, config.useSeasonalThemes ? 't' : 'f', config.restoreAfterSeason ? 't' : 'f');
    setConfigToCurrentSeason();
    log(INFO, "Set Configuration to current season! Type: %i", config.type);
}

static Il2CppObject* currentEffect;
static std::map<Il2CppObject*, swingRatingCounter_context_t> swingRatingMap;

// Called during: FlyingObjectEffect.didFinishEvent
// FlyingScoreEffect.HandleFlyingScoreEffectDidFinish
void effectDidFinish(Il2CppObject* flyingObjectEffect) {
    if (flyingObjectEffect == currentEffect) {
        currentEffect = nullptr;
    }
}

void judgeNoContext(Il2CppObject* flyingScoreEffect, Il2CppObject* noteCutInfo) {
    int beforeCut = 0;
    int afterCut = 0;
    int cutDistance = 0;
    RET_V_UNLESS(il2cpp_utils::RunMethod("", "ScoreModel", "RawScoreWithoutMultiplier", noteCutInfo, beforeCut, afterCut, cutDistance));
    checkJudgments(flyingScoreEffect, beforeCut, afterCut, cutDistance);
}

void judge(Il2CppObject* counter) {
    // Get FlyingScoreEffect and NoteCutInfo from map
    auto itr = swingRatingMap.find(counter);
    if (itr == swingRatingMap.end()) {
        log(DEBUG, "counter: %p was not in swingRatingMap!", counter);
        return;
    }
    auto context = itr->second;
    int beforeCut = 0;
    int afterCut = 0;
    int cutDistance = 0;

    if (context.noteCutInfo) {
        RET_V_UNLESS(il2cpp_utils::RunMethod("", "ScoreModel", "RawScoreWithoutMultiplier", context.noteCutInfo, beforeCut, afterCut, cutDistance));
        if (context.flyingScoreEffect) {
            checkJudgments(context.flyingScoreEffect, beforeCut, afterCut, cutDistance);
        }
    }
}

void HandleSaberSwingRatingCounterChangeEvent(Il2CppObject* self) {
    if (config.doIntermediateUpdates) {
        auto noteCutInfo = RET_V_UNLESS(il2cpp_utils::GetFieldValue(self, "_noteCutInfo").value_or(nullptr));
        judgeNoContext(self, noteCutInfo);
    }
}

void InitAndPresent_Prefix(Il2CppObject* self, Vector3& targetPos, float& duration) {
    if (config.useFixedPos) {
        targetPos.x = config.fixedPosX;
        targetPos.y = config.fixedPosY;
        targetPos.z = config.fixedPosZ;
        auto transform = RET_V_UNLESS(il2cpp_utils::GetPropertyValue(self, "transform").value_or(nullptr));
        RET_V_UNLESS(il2cpp_utils::SetPropertyValue(self, "position", targetPos));

        if (currentEffect) {
            // Disable the current effect
            RET_V_UNLESS(il2cpp_utils::SetFieldValue(currentEffect, "_duration", 0.0f));
        }
        currentEffect = self;
    } else if (!config.doIntermediateUpdates) {
        // If there are no intermediate updates, in order to remove the 'flickering' score, set the duration to 0 and fix it later
        // Duration being set to 0 would be wrong, since we would destroy before doing anything
        duration = 0.01f;
    }
}

void InitAndPresent_Postfix(Il2CppObject* self, Il2CppObject* noteCutInfo) {
    judgeNoContext(self, noteCutInfo);
    if (!config.doIntermediateUpdates) {
        // Set the duration back to the default, which is 0.7 seconds
        RET_V_UNLESS(il2cpp_utils::SetFieldValue(self, "_duration", 0.7f));
    }
    auto counter = RET_V_UNLESS(il2cpp_utils::GetPropertyValue(noteCutInfo, "swingRatingCounter").value_or(nullptr));
    swingRatingMap[counter] = {noteCutInfo, self};
    log(DEBUG, "Complete with InitAndPresent!");
}

void Notification_Init(Il2CppObject* parent) {
    log(DEBUG, "Initialized notificationBox, parent: %p", parent);
    notification.notificationBox.fontSize = 5;
    notification.notificationBox.anchoredPosition = {0.0f, -100.0f};
    notification.notificationBox.parentTransform = parent;
}

bool Notification_Create() {
    return notification.create();
}

void Notification_Update() {
    notification.parallelUpdate();
}

void Notification_Invalid() {
    notification.markInvalid();
}

// Install hooks
extern "C" void load() {
    loadConfig();
    log(INFO, "Installing hooks...");
    INSTALL_HOOK_OFFSETLESS(FlyingScoreEffect_HandleSaberSwingRatingCounterDidChangeEvent, il2cpp_utils::FindMethodUnsafe("", "FlyingScoreEffect", "HandleSaberSwingRatingCounterDidChangeEvent", 2));
    INSTALL_HOOK_OFFSETLESS(FlyingScoreEffect_InitAndPresent, il2cpp_utils::FindMethodUnsafe("", "FlyingScoreEffect", "InitAndPresent", 6));
    INSTALL_HOOK_OFFSETLESS(FlyingScoreSpawner_HandleFlyingScoreEffectDidFinish, il2cpp_utils::FindMethodUnsafe("", "FlyingScoreSpawner", "HandleFlyingScoreEffectDidFinish", 1));
    INSTALL_HOOK_OFFSETLESS(CutScoreHandler_HandleSwingRatingCounterDidFinishEvent, il2cpp_utils::FindMethodUnsafe("", "BeatmapObjectExecutionRatingsRecorder/CutScoreHandler", "HandleSwingRatingCounterDidFinishEvent", 1));
    INSTALL_HOOK_OFFSETLESS(VRUIControls_VRPointer_Process, il2cpp_utils::FindMethodUnsafe("VRUIControls", "VRPointer", "Process", 1));
    INSTALL_HOOK_OFFSETLESS(MainMenuViewController_DidActivate, il2cpp_utils::FindMethodUnsafe("", "MainMenuViewController", "DidActivate", 2));
    INSTALL_HOOK_OFFSETLESS(MainMenuViewController_HandleMenuButton, il2cpp_utils::FindMethodUnsafe("", "MainMenuViewController", "HandleMenuButton", 1));
    log(INFO, "Installed hooks!");
}