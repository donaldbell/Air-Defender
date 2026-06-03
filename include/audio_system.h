#pragma once

#include <Arduino.h>
#include <vector>

// --------------------------------------------------------------------------
// AUDIO DATA STRUCTURES
// --------------------------------------------------------------------------

struct ToneCmd { 
  int freq; 
  int duration; 
};

typedef std::vector<ToneCmd> Melody;

enum SoundEvent { 
  EVT_NONE=0, EVT_START, EVT_WIN, EVT_LOSE, EVT_MISTAKE,      
  EVT_HIT_SUCCESS, EVT_SHOT_BLUE, EVT_SHOT_RED, EVT_SHOT_GREEN, EVT_SHOT_WHITE,
  EVT_FIREWORK_LAUNCH, EVT_FIREWORK_EXPLODE, EVT_ENEMIES_BUILDING, EVT_LEVEL_VICTORY, EVT_SHOT_CANNON,
  // New events
  EVT_CITY_SELECT,        // Cheerful chime when player confirms a city
  EVT_ENEMY_ENCROACH,     // Low warning pulse when enemy is close to hero
  EVT_HERO_DEATH,         // Dramatic descending tones on death (alias for EVT_LOSE visual)
  EVT_THEME,              // Short ambient intro jingle
  EVT_GAME_START,         // Energetic jingle when player exits attract screen
  EVT_COMET_LAUNCH        // High-pitched descending sweep when enemy comet fires
};

// --------------------------------------------------------------------------
// AUDIO SYSTEM DECLARATIONS
// --------------------------------------------------------------------------

// Core audio functions
void initAudio();
void playSound(SoundEvent evt);
void playCannonChargeSound(int stage);
void audioTask(void* parameter);

// Melody and tone functions  
Melody parseSoundString(String data);
void melodyFromStr(Melody& m, String s);
void playToneI2S(int freq, int durationMs);

// Audio system management
void enableAudio();
void disableAudio();
bool isAudioEnabled();
void setAudioVolume(int volume);  // 0-100

// Audio event queue management
bool queueAudioEvent(SoundEvent evt);
void clearAudioQueue();
int getQueuedEventCount();