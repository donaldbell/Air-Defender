#pragma once

#include "game_config.h"

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

// Melody strings for different events
extern const String MELODY_SHOT_BLUE;
extern const String MELODY_SHOT_RED;
extern const String MELODY_SHOT_GREEN;
extern const String MELODY_ENEMY_HIT;
extern const String MELODY_ENEMY_DESTROYED;
extern const String MELODY_PLAYER_HIT;
extern const String MELODY_LEVEL_COMPLETE;
extern const String MELODY_GAME_OVER;
extern const String MELODY_BUTTON_PRESS;
extern const String MELODY_POWER_UP;
extern const String MELODY_VICTORY;
extern const String MELODY_BOSS_DEFEAT;
extern const String MELODY_ENEMIES_BUILDING;
extern const String MELODY_POWER_UP;
extern const String MELODY_VICTORY;
extern const String MELODY_BOSS_DEFEAT;
extern const String MELODY_ENEMIES_BUILDING;