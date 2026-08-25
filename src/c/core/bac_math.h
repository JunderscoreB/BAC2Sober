#pragma once
#include <pebble.h>

// --- Core Constants ---
#define METABOLISM_RATE_PER_HOUR 0.015f
#define ALCOHOL_DENSITY_G_ML 0.789f

// --- Global Enumerations ---
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

// --- Core Data Structures ---
typedef struct {
    float weight_kg;
    float gender_constant;
} UserProfile;

typedef struct {
    time_t timestamp;
    float volume_ml;
    float abv;
} Drink;

// --- Mathematical Prototypes ---
float calculate_current_bac(UserProfile user, Drink *drinks, int num_drinks, time_t current_time);
