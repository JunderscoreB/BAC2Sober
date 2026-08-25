# BAC2Sober 🍻⌚

BAC2Sober is a native, open-source C application for PebbleOS smartwatches that tracks blood alcohol content (BAC) and provides a real-time metabolic countdown to sobriety. 

## Features
- **Dynamic Widmark Calculations:** Real-time BAC tracking and a live "Sober By" clock that updates every minute.
- **Vector Container Graphics:** Beautiful, memory-efficient `GPath` vector silhouettes of common drink containers (Cans, Bottles, Wine Glasses, Growlers, Pints, and **Shots**) with dynamic liquid filling.
- **Full Touch Support:** Native touchscreen integration for the Pebble Time 2 (Emery platform). Use kinetic scrolling through your drink logs and intuitive swipe-and-tap gestures to dial in volumes and ABVs. Includes a graceful fallback to physical buttons on legacy, non-touch hardware using the system's touch service detection.
- **Customizable User Profiles:** Supports precise weight entry in both Kilograms (kg) and Pounds (lbs).
- **Theme Engine:** Built-in Light, Dark, and **Auto** (switches based on 6 PM - 6 AM local time) modes with vibrant UI highlights on color displays.
- **Historical Log:** Review, edit (Time, Volume, ABV), or delete previous drinks on the fly. The log resets after 12 hours of 0.00 BAC. 

## Compatibility
Built against the modern [Core Devices PebbleOS SDK](https://github.com/coredevices/PebbleOS). Fully compatible with:
- Pebble Time 2 (PT2)
- Pebble Time / Time Steel
- Pebble 2 HR / SE
- Legacy monochrome hardware (Graceful fallback to B&W themes)

## Building from Source

This project utilizes the standard Pebble `waf` build system.

1. Ensure you have the Pebble SDK installed and configured.
2. Clone this repository:
   ```bash
   git clone [https://github.com/username/BAC2Sober.git](https://github.com/username/BAC2Sober.git)
   cd BAC2Sober
   ```
3. Build the `pbw` binary:
   ```bash
   pebble build
   ```
4. Install to your watch or emulator:
   ```bash
   pebble install --emulator emery
   ```
   *(To install directly to a watch over your local network, use `pebble install --phone <PHONE_IP>`)*

## Liability Disclaimer
**The developers of BAC2Sober assume no responsibility for the misuse of this application or the accuracy of its blood alcohol estimates.** This application is for entertainment and informational purposes only. It is not a medical device, nor a legal breathalyzer. Its calculations must never be relied upon to determine your ability or fitness to operate a motor vehicle, heavy machinery, or make safety-critical decisions. Always exercise personal responsibility and never drink and drive.

## AI Disclosure
The source code, geometric vector generation, and user interface for this application were developed in collaboration with an AI language model. The underlying mathematical implementations and firmware integrations were validated against the official PebbleOS open-source SDK.