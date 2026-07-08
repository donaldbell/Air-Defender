# Air Defender

Air Defender is a wireless, interactive air-quality game built for two ESP32-based boards. One board acts as the game console and visual stage, while the other acts as the controller and user interface. Together they turn real-world air pollution data into a fast-paced, educational arcade experience.

The project was originally started under the working title "1D Enviro Game", but the game is now presented as Air Defender.

## Visual preview

![Air Defender Controller](assets/air-defender-controller.jpg)

Controller and LCD interface for Air Defender.

![LED strip demo](assets/air-defender-strip-demo.jpg)

Three LED lanes light up based on pollutant status.

![Installation view](assets/air-defender-installation.jpg)

Wall-mounted installation demo.

![Wall display](assets/air-defender-wall-display.jpg)

Full vertical display with the Air Defender branding.

![Player interaction](assets/air-defender-player.jpg)

Player using the controller to clean the air.

## What the game does

Air Defender turns three major pollutants into three playable lanes:

- PM2.5 (fine particulate matter)
- NO2 (nitrogen dioxide)
- O3 (ground-level ozone)

Each pollutant appears as a glowing LED strip that is populated by pollution enemies. The player uses the controller to fire at the threats and clear the air. The more effectively the player defeats the pollution on each strip, the more the city is cleaned up.

## How it works

The game is built around a simple idea: pollution levels from a selected city are translated into a playable battle. The console board displays the game visually using three LED strips, while the controller board handles input, text, sound, and the overall experience.

### Core gameplay loop

1. The controller presents a city selection and air-quality screen.
2. The player chooses a city and starts the round.
3. Each pollutant strip reveals an educational intro and the WHO guideline reference.
4. The player fires at enemies using the matching controller button.
5. Holding a button charges a stronger cannon shot.
6. If the player clears the pollution from all three strips, the city is marked as rescued.
7. If pollution reaches the hero, the round ends in defeat.

## Unique two-board wireless system

A defining feature of Air Defender is its split hardware architecture.

### Console board

The console board runs the main game simulation and LED visuals. It is responsible for:

- rendering the three pollutant strips
- animating enemies, sparks, and victory effects
- tracking game state and pollution progression
- sending status information back to the controller

### Controller board

The controller board is the player-facing interface. It handles:

- three arcade-style buttons for shooting
- LCD screen output and score/status display
- audio playback and sound cues
- wireless communication with the console
- optional Wi-Fi configuration mode for setup and city data entry

### Wireless communication

The two boards communicate over ESP-NOW, a low-latency wireless protocol suited to fast game feedback. This makes the system feel like a single game while physically using two boards.

The controller can also switch into access-point configuration mode so the player can:

- configure Wi-Fi settings
- adjust language settings
- manage city AQI data
- browse and update city values through a browser-based interface

## Game mechanics

### Pollutant lanes

Each lane represents a different environmental challenge:

- PM2.5: fine dust and particulate pollution
- NO2: combustion-related air pollution
- O3: harmful ground-level ozone smog

Each strip uses a different color and behavior profile, creating a distinct feel for each pollutant.

### Shooting and charging

The controller includes one button per pollutant lane:

- blue button for PM2.5
- red button for NO2
- green button for O3

A short tap fires a standard shot. Holding the button charges a stronger cannon shot that can clear more pollution at once.

### Educational layer

The game is not only a shooter; it also teaches the player about air quality. Before the active battle begins, the game shows short educational slides with:

- pollutant descriptions
- health impacts
- WHO safe guideline references

This turns the experience into a playful educational tool as well as a game.

## Multi-language support

Air Defender includes localized interface text for multiple languages.

Supported languages:

- English
- Spanish
- Catalan

The selected language can be changed through the controller configuration interface and is persisted on the board.

## Hardware summary

### Console hardware

- M5Stack Atom S3 Lite
- Three LED strips for the pollutant lanes
- Onboard status LED

### Controller hardware

- Adafruit QT Py ESP32-S3
- Three arcade buttons
- 20x4 I2C LCD display
- Audio output hardware
- Onboard NeoPixel status light

## Design files and 3D models

Physical designs, enclosures, and signage are available as public Tinkercad projects. See [DESIGN.md](DESIGN.md) for links to the controller panel, wall-mount holders, toppers, and branding sign.

## Build and upload

This project uses PlatformIO.

### Build all environments

```bash
platformio run
```

### Upload the console firmware

```bash
platformio run --target upload --environment console
```

### Upload the controller firmware

```bash
platformio run --target upload --environment controller
```

### Monitor serial output

```bash
platformio device monitor --environment console
```

or

```bash
platformio device monitor --environment controller
```

## Project structure

- src/ contains the firmware for the console and controller
- include/ contains shared configuration, protocol definitions, and strings
- platformio.ini contains the build configuration for both environments

## Notes

The game uses air-quality values as the basis for the encounter design. In practice, this means each city can create a different challenge depending on its pollution profile and the selected pollutant mix.

This project is intended as both a playable game and a demonstration of how environmental data can be transformed into an interactive physical experience.
