#include <pebble.h>
#include "bac_math.h"

float calculate_current_bac(UserProfile user, Drink* drinks, int num_drinks, time_t current_time) {
    if (num_drinks == 0) return 0.0f;

    float bac = 0.0f;
    time_t last_time = drinks[0].timestamp;

    for (int i = 0; i < num_drinks; i++) {
        // Metabolize alcohol since the *previous* drink
        float hours_passed = (float)(drinks[i].timestamp - last_time) / 3600.0f;
        bac -= (METABOLISM_RATE_PER_HOUR * hours_passed);
        if (bac < 0.0f) bac = 0.0f;

        // Add the new drink's BAC spike
        float alcohol_grams = drinks[i].volume_ml * drinks[i].abv * ALCOHOL_DENSITY_G_ML;
        float bac_spike = alcohol_grams / (user.weight_kg * user.gender_constant * 10.0f);
        bac += bac_spike;

        last_time = drinks[i].timestamp;
    }

    // Metabolize from the last drink until the *current* time
    float final_hours_passed = (float)(current_time - last_time) / 3600.0f;
    bac -= (METABOLISM_RATE_PER_HOUR * final_hours_passed);
    if (bac < 0.0f) bac = 0.0f;

    return bac;
}