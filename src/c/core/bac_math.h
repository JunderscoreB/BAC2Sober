#pragma once
#include <pebble.h>

#define METABOLISM_RATE_PER_HOUR 0.015f
#define ALCOHOL_DENSITY_G_ML 0.789f

typedef enum {
    SHAPE_CAN,
    SHAPE_TALLBOY,
    SHAPE_BOTTLE,
    SHAPE_WINE_BOTTLE,
    SHAPE_GROWLER,
    SHAPE_PINT,
    SHAPE_TASTER,
    SHAPE_WINE_GLASS,
    SHAPE_SHOT,
    SHAPE_CUSTOM
} DrinkShape;

typedef struct {
    float weight_kg;
    float gender_constant;
} UserProfile;

// Legacy structure for migrating existing saves
typedef struct {
    time_t timestamp;
    float volume_ml;
    float abv;
} OldDrink;

// Expanded structure to remember container types
typedef struct {
    time_t timestamp;
    float volume_ml;
    float original_volume_ml;
    float abv;
    DrinkShape shape;
} Drink;

float calculate_current_bac(UserProfile user, Drink *drinks, int num_drinks, time_t current_time);