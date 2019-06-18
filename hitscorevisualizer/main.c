#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/limits.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

#include "../beatsaber-hook/shared/inline-hook/inlineHook.h"
#include "../beatsaber-hook/shared/utils/utils.h"

typedef struct __attribute__((__packed__)) {
    float r;
    float g;
    float b;
    float a;
} Color;

typedef struct __attribute__((__packed__)) {
    void* fadeAnimationCurve;
    void* maxCutDistanceScoreIndicator;
    void* text;
    Color* color;
    float colorAMultiplier;
    void* noteCutInfo;
    void* saberAfterCutSwingRatingCounter;
    
} FlyingScoreEffect;

float temp_r = 1;
float temp_g = 0;
float temp_b = 0;
float temp_a = 1;

MAKE_HOOK(raw_score_without_multiplier, 0x48C248, void, void* noteCutInfo, void* saberAfterCutSwingRatingCounter, int* beforeCutRawScore, int* afterCutRawScore, int* cutDistanceRawScore) {
    log("Created RawScoreWithoutMultiplier Hook!");
    raw_score_without_multiplier(noteCutInfo, saberAfterCutSwingRatingCounter, beforeCutRawScore, afterCutRawScore, cutDistanceRawScore);
}

MAKE_HOOK(init_and_present, 0x132307C, void, void* noteCut, int multiplier, float duration, void* targetPos, Color* color, void* saberAfterCutSwingRatingCounter) {
    // Placeholder, for now.
    log("Created InitAndPresent Hook!");
    log("Attempting to call standard InitAndPresent...");
    init_and_present(noteCut, multiplier, duration, targetPos, color, saberAfterCutSwingRatingCounter);
    log("Creating score int* pointers!");
    int* beforeCut = malloc(sizeof(int));
    int* afterCut = malloc(sizeof(int));
    int* cutDistance = malloc(sizeof(int));
    log("Attempting to call RawScoreWithoutMultiplier hook...");
    raw_score_without_multiplier(noteCut, saberAfterCutSwingRatingCounter, beforeCut, afterCut, cutDistance);
    log("Completed!");
    log("RawScore: %i", *(*afterCut) + *(*cutDistance));
}

MAKE_HOOK(manual_update, 0x1323314, void, FlyingScoreEffect* self, float t) {
    // Lets just test to make sure this one works without running the actual ones that would change color based on hit score
    log("Attempting to create new pointer to color with self pointer: %i", self);
    self->color = malloc(sizeof(Color));
    log("Completed malloc for Color struct!");
    log("Attemping to set color to: %f, %f, %f", temp_r, temp_g, temp_b);
    self->color->r = temp_r;
    self->color->g = temp_g;
    self->color->b = temp_b;
    self->color->a = temp_a;
    log("Set color to: %f, %f, %f", temp_r, temp_g, temp_b);
    manual_update(self, t);
}

__attribute__((constructor)) void lib_main()
{
    log("Inserted HitScoreVisualizer to only display color: %f, %f, %f", temp_r, temp_g, temp_b);
    INSTALL_HOOK(manual_update);
    log("Installed ManualUpdate Hook!");
    INSTALL_HOOK(init_and_present);
    log("Installed InitAndPresent Hook!");
    INSTALL_HOOK(raw_score_without_multiplier);
    log("Installed RawScoreWithoutMultiplier Hook!");
}