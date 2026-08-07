#pragma once
#include <pebble.h>

// --- Geometric Container Shapes ---
typedef enum {
    SHAPE_TASTER,
    SHAPE_CAN,
    SHAPE_TALLBOY,
    SHAPE_PINT,
    SHAPE_BOTTLE,
    SHAPE_GROWLER,
    SHAPE_WINE_GLASS,
    SHAPE_WINE_BOTTLE,
    SHAPE_CUSTOM
} DrinkShape;

// --- App Structures ---
typedef struct {
    const char *name;
    float volume_ml;
    float default_abv;
    DrinkShape shape; // <-- Added shape identifier
} DrinkContainer;

typedef struct {
    uint32_t timestamp; 
    float volume_ml;    
    float abv;          
} Drink;

typedef struct {
    float weight_kg;
    float gender_constant;
} UserProfile;

// --- Core Math Constants ---
#define METABOLISM_RATE_PER_HOUR 0.015f
#define ALCOHOL_DENSITY_G_ML 0.789f

float calculate_current_bac(UserProfile user, Drink *drinks, int num_drinks, time_t current_time);