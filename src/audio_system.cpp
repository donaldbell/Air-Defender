// --- Controller build: Provide global audio state variables ---
#include "audio_system.h"
#include "game_config.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// =============================================================================
// DEBUG CONTROL SYSTEM (matches controller_unit.cpp)
// =============================================================================

#define DEBUG_LEVEL 1

#define DEBUG_ERROR   1
#define DEBUG_INFO    2 
#define DEBUG_VERBOSE 3

#define DBG_ERROR(fmt, ...)   if (DEBUG_LEVEL >= DEBUG_ERROR)   Serial.printf("[ERROR] " fmt, ##__VA_ARGS__)
#define DBG_INFO(fmt, ...)    if (DEBUG_LEVEL >= DEBUG_INFO)    Serial.printf("[INFO] " fmt, ##__VA_ARGS__)
#define DBG_VERBOSE(fmt, ...) if (DEBUG_LEVEL >= DEBUG_VERBOSE) Serial.printf("[DEBUG] " fmt, ##__VA_ARGS__)

bool config_sound_on = true;
QueueHandle_t audioQueue = nullptr;
int config_volume_pct = 50; // Default to 50% volume (reduce hot output for line-level amp input)

// Melody objects (must be after Melody type is defined)
Melody melStart;
Melody melShotBlue;
Melody melShotRed;
Melody melShotGreen;
Melody melShotWhite;
Melody melShotCannon;
Melody melMistake;
Melody melHit;
Melody melWin;
Melody melLose;
Melody melFireworkLaunch;
Melody melFireworkExplode;
Melody melEnemiesBuilding;
Melody melLevelVictory;
Melody melCitySelect;
Melody melEnemyEncroach;
Melody melHeroDeath;
Melody melTheme;
Melody melGameStart;
Melody melCometLaunch;

TaskHandle_t audioTaskHandle = nullptr;
bool audioEnabled = false;

// --------------------------------------------------------------------------
// AUDIO CONFIGURATION STRINGS
// --------------------------------------------------------------------------

static const String MELODY_SHOT_BLUE = "784,30;1047,30;1319,30;784,20";  // Using red/NO2 sound for all weapon fire
static const String MELODY_SHOT_RED = "784,30;1047,30;1319,30;784,20";  // Enhanced red shot with tail  
static const String MELODY_SHOT_GREEN = "784,30;1047,30;1319,30;784,20";  // Using red/NO2 sound for all weapon fire
static const String MELODY_ENEMY_HIT = "2093,30";
static const String MELODY_ENEMY_DESTROYED = "1500,20;1200,20;900,20;700,20;500,20;300,20";  // Explosion cascade
static const String MELODY_PLAYER_HIT = "370,100;349,100;330,100;311,400";
static const String MELODY_LEVEL_COMPLETE = "392,120;523,120;659,120;784,120;1047,120;1319,120;1568,120;1319,120;0,40;415,120;523,120;622,120;830,120;1047,120;1319,120;1661,120;1319,120;0,40;466,120;587,120;698,120;932,120;1175,120;1397,120;1865,120;1865,120;1865,120;2093,180;0,200;523,100;659,100;784,100;1047,200;0,100;1319,80;1568,80;2093,300;2349,200;0,300";  // Extended Mario Course Clear with finale
static const String MELODY_GAME_OVER = "370,100;349,100;330,100;311,400";
static const String MELODY_BUTTON_PRESS = "523,80;659,80;784,80;1047,300";
static const String MELODY_POWER_UP = "130,80;110,80;90,100;70,100;50,120";  // Deep bass rumble
static const String MELODY_VICTORY = "523,80;659,80;784,80;1047,300;0,150;1047,60;1319,60;0,100;523,60;659,60;784,60;1047,200";  // Extended win melody
static const String MELODY_BOSS_DEFEAT = "220,30;180,30;150,30;120,30;100,30";  // Doppler bomb drop
static const String MELODY_ENEMIES_BUILDING = "147,140;165,140;185,140;208,140;233,140;262,140;294,150";  // D3 to D4 - extended 990ms bubbling growth

// New sounds — added for 7-feature batch
static const String MELODY_CITY_SELECT   = "523,80;659,60;784,60;1047,120;1319,150";  // Cheerful ascending chime
static const String MELODY_ENEMY_ENCROACH = "110,200;0,100;110,200;0,100;98,250";      // Low ominous pulse (warning)
static const String MELODY_HERO_DEATH     = "440,120;330,120;247,150;185,200;147,300"; // Dramatic descending tones
static const String MELODY_THEME          = "523,100;587,100;659,100;784,100;659,80;523,80;392,120;440,150"; // Ambient intro jingle
static const String MELODY_GAME_START     = "392,75;0,45;392,75;0,45;330,105;392,105;440,105;392,210;0,60;440,105;392,105;330,105;294,210;0,60;262,75;294,75;330,75;392,75;440,75;392,150;330,150;392,450;330,120;392,120;440,240"; // Energetic mid-range jingle — attract exit (octave down, 1.5x tempo)
static const String MELODY_COMET_LAUNCH   = "1568,20;1319,20;1047,20;784,20;523,20";  // G6→C5 descending sweep — enemy comet incoming

// --------------------------------------------------------------------------
// AUDIO PARSING FUNCTIONS  
// --------------------------------------------------------------------------

Melody parseSoundString(String data) {
  Melody result;
  if (data.length() < 3) return result;
  
  int pos = 0;
  while (pos < data.length()) {
    int commaPos = data.indexOf(',', pos);
    int semicolonPos = data.indexOf(';', pos);
    
    if (commaPos != -1 && (semicolonPos == -1 || commaPos < semicolonPos)) {
      ToneCmd t;
      t.freq = data.substring(pos, commaPos).toInt();
      pos = commaPos + 1;
      
      int nextDelim = data.indexOf(';', pos);
      if (nextDelim == -1) nextDelim = data.length();
      t.duration = data.substring(pos, nextDelim).toInt();
      result.push_back(t);
      pos = nextDelim + 1;
    } else {
      break;
    }
  }
  
  if (result.empty()) {
    ToneCmd t;
    t.freq = 440;
    t.duration = 100;
    result.push_back(t);
  }
  
  return result;
}

void melodyFromStr(Melody& m, String s) { 
  m = parseSoundString(s); 
}

// --------------------------------------------------------------------------
// CORE AUDIO FUNCTIONS
// --------------------------------------------------------------------------

void playSound(SoundEvent evt) {
  if (!config_sound_on || audioQueue == nullptr) {
    return; // Silent fail when audio disabled
  }
  
  SoundEvent soundEvt = static_cast<SoundEvent>(evt);
  BaseType_t result = xQueueSend(audioQueue, &soundEvt, 0);
  if (result != pdTRUE) {
    DBG_ERROR("Failed to queue audio event %d\n", evt);
  }
}

void playCannonChargeSound(int stage) {
  if (!config_sound_on) {
    return; // Silent fail when audio disabled
  }
  
  // Different pitches for charge stages (1-3)
  switch(stage) {
    case 1: playSound(EVT_SHOT_BLUE); break;   // Low pitch start
    case 2: playSound(EVT_SHOT_RED); break;    // Medium pitch 
    case 3: playSound(EVT_SHOT_CANNON); break; // Full power
    default: playSound(EVT_SHOT_GREEN); break;
  }
}

void playToneI2S(int freq, int durationMs) {
  static int toneCount = 0;
  toneCount++;
  
  // Handle silence (freq = 0)
  if (freq == 0) {
    int samples = (SAMPLE_RATE * durationMs) / 1000;
    int16_t *buffer = (int16_t *)calloc(samples, sizeof(int16_t));
    if (buffer) {
      size_t bytes_written;
      esp_err_t result = i2s_write(I2S_NUM_0, buffer, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      free(buffer);
    }
    return;
  }
  
  size_t bytes_written;
  int samples = (SAMPLE_RATE * durationMs) / 1000;
  int16_t *buffer = (int16_t *)malloc(samples * sizeof(int16_t));
  if (!buffer) {
    return;
  }
  
  int halfPeriod = SAMPLE_RATE / freq / 2;
  int16_t volume = map(config_volume_pct, 0, 100, 0, 8192); // Reduced max volume
  
  // Generate square wave
  for (int i = 0; i < samples; i++) {
    buffer[i] = ((i / halfPeriod) % 2 == 0) ? volume : -volume;
  }
  
  esp_err_t result = i2s_write(I2S_NUM_0, buffer, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  
  free(buffer);
}

void audioTask(void *parameter) {
  DBG_INFO("Audio task starting on core %d\n", xPortGetCoreID());
  
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE 
  };

  // Verify GPIO pins are available
#ifdef QTPY_S3
  DBG_VERBOSE("GPIO pins - BCLK: %d, LRC: %d, DOUT: %d (Controller)\n", 
                I2S_BCLK, I2S_LRC, I2S_DOUT);
#else
  DBG_VERBOSE("GPIO pins - BCLK: %d, LRC: %d, DOUT: %d\n", 
                I2S_BCLK, I2S_LRC, I2S_DOUT);
#endif
  
  DBG_VERBOSE("Installing I2S driver...\n");
  esp_err_t result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (result == ESP_OK) {
    DBG_INFO("I2S driver installed\n");
  } else {
    DBG_ERROR("I2S driver install failed: %d\n", result);
    vTaskDelete(NULL);
    return;
  }
  
  DBG_VERBOSE("Setting I2S pins...\n");
  result = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (result == ESP_OK) {
    DBG_VERBOSE("I2S pins configured\n");
  } else {
    Serial.printf("✗ I2S pin config failed: %d\\n", result);
    i2s_driver_uninstall(I2S_NUM_0);
    vTaskDelete(NULL);
    return;
  }
  
  // Start I2S driver
  DBG_VERBOSE("Starting I2S driver...\n");
  result = i2s_start(I2S_NUM_0);
  if (result == ESP_OK) {
    DBG_VERBOSE("I2S driver started\n");
  } else {
    Serial.printf("✗ I2S driver start failed: %d\\n", result);
  }
  
  i2s_zero_dma_buffer(I2S_NUM_0);
  
  // Audio system ready
  DBG_INFO("Audio task initialized and ready\n");

  const Melody* currentMelody = nullptr;
  int noteIndex = 0;
  unsigned long debugTimer = 0;
  int soundsPlayed = 0;
  
  while(true) {
    // Periodic I2S status check (reduced frequency)
    if (millis() - debugTimer > 60000) { // Every 60 seconds
      debugTimer = millis();
      DBG_VERBOSE("I2S Status: %d sounds played, %d bytes free\n", 
                    soundsPlayed, ESP.getFreeHeap());
    }
    
    SoundEvent newEvent;
    if (xQueueReceive(audioQueue, &newEvent, 10) == pdTRUE) {
      // Process audio event silently - removed verbose logging
      
      bool play = true;
      if ((currentMelody == &melWin || currentMelody == &melLose) && newEvent != EVT_START) {
        play = false;
        // Skip non-priority events during priority melodies
      }
      
      if (play) {
        switch(newEvent) {
          case EVT_START:      currentMelody = &melStart; break;
          case EVT_SHOT_BLUE:  currentMelody = &melShotBlue; break;
          case EVT_SHOT_RED:   currentMelody = &melShotRed; break;
          case EVT_SHOT_GREEN: currentMelody = &melShotGreen; break;
          case EVT_SHOT_WHITE: currentMelody = &melShotWhite; break;
          case EVT_SHOT_CANNON: currentMelody = &melShotCannon; break;
          case EVT_MISTAKE:    currentMelody = &melMistake; break;
          case EVT_HIT_SUCCESS:currentMelody = &melHit; break;
          case EVT_WIN:        currentMelody = &melWin; break;
          case EVT_LOSE:       currentMelody = &melLose; break;
          case EVT_FIREWORK_LAUNCH: currentMelody = &melFireworkLaunch; break;
          case EVT_FIREWORK_EXPLODE: currentMelody = &melFireworkExplode; break;
          case EVT_ENEMIES_BUILDING: currentMelody = &melEnemiesBuilding; break;
          case EVT_LEVEL_VICTORY: currentMelody = &melLevelVictory; break;
          case EVT_CITY_SELECT:   currentMelody = &melCitySelect; break;
          case EVT_ENEMY_ENCROACH: currentMelody = &melEnemyEncroach; break;
          case EVT_HERO_DEATH:    currentMelody = &melHeroDeath; break;
          case EVT_THEME:         currentMelody = &melTheme; break;
          case EVT_GAME_START:   currentMelody = &melGameStart; break;
          case EVT_COMET_LAUNCH: currentMelody = &melCometLaunch; break;
          default: DBG_ERROR("Unknown audio event: %d\n", newEvent); break;
        }
        noteIndex = 0;
        i2s_zero_dma_buffer(I2S_NUM_0);
        soundsPlayed++;
      }
    }

    if (currentMelody != nullptr) {
      if (noteIndex >= currentMelody->size()) {
        // Melody finished - removed verbose logging
        currentMelody = nullptr; 
        playToneI2S(0, 20); 
        i2s_zero_dma_buffer(I2S_NUM_0);
      } else {
        ToneCmd t = (*currentMelody)[noteIndex];
        // Removed per-note verbose logging for performance
        playToneI2S(t.freq, t.duration);
        noteIndex++;
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10)); 
    }
  }
}

// --------------------------------------------------------------------------
// AUDIO SYSTEM MANAGEMENT
// --------------------------------------------------------------------------

void initAudio() {
  // Create audio queue
  audioQueue = xQueueCreate(10, sizeof(SoundEvent));
  if (audioQueue == nullptr) {
    DBG_ERROR("Failed to create audio queue\n");
    return;
  } else {
    DBG_INFO("Audio queue created\n");
  }
  
  // Initialize melodies from string configurations
  if (config_sound_on) {
    DBG_INFO("Initializing audio melodies...\n");
    melodyFromStr(melStart, MELODY_BUTTON_PRESS);
    melodyFromStr(melWin, MELODY_VICTORY);
    melodyFromStr(melLose, MELODY_GAME_OVER);
    melodyFromStr(melMistake, "60,150");
    melodyFromStr(melShotBlue, MELODY_SHOT_BLUE);
    melodyFromStr(melShotRed, MELODY_SHOT_RED);
    melodyFromStr(melShotGreen, MELODY_SHOT_GREEN);
    melodyFromStr(melShotWhite, "1047,20;1319,20;1568,20;2093,4");
    melodyFromStr(melHit, MELODY_ENEMY_HIT);
    melodyFromStr(melShotCannon, MELODY_POWER_UP);
    // Initialize firework and level progression melodies
    melodyFromStr(melFireworkLaunch, MELODY_BOSS_DEFEAT);
    melodyFromStr(melFireworkExplode, MELODY_ENEMY_DESTROYED);
    // Initialize level progression melodies  
    melodyFromStr(melEnemiesBuilding, MELODY_ENEMIES_BUILDING);
    melodyFromStr(melLevelVictory, MELODY_LEVEL_COMPLETE);
    melodyFromStr(melCitySelect, MELODY_CITY_SELECT);
    melodyFromStr(melEnemyEncroach, MELODY_ENEMY_ENCROACH);
    melodyFromStr(melHeroDeath, MELODY_HERO_DEATH);
    melodyFromStr(melTheme, MELODY_THEME);
    melodyFromStr(melGameStart, MELODY_GAME_START);
    melodyFromStr(melCometLaunch, MELODY_COMET_LAUNCH);
    
    // Create audio task on core 0
    BaseType_t result = xTaskCreatePinnedToCore(audioTask, "AudioTask", 10240, NULL, 2, &audioTaskHandle, 0);
    if (result == pdPASS) {
      DBG_INFO("Audio task created\n");
      audioEnabled = true;
    } else {
      DBG_ERROR("Failed to create audio task: %d\n", result);
      audioEnabled = false;
    }
  } else {
    DBG_INFO("Audio disabled in configuration\n");
    audioEnabled = false;
  }
}

void enableAudio() {
  config_sound_on = true;
  audioEnabled = true;
}

void disableAudio() {
  // config_sound_on = false; // Disabled: always enable sound
  audioEnabled = false;
}

bool isAudioEnabled() {
  return audioEnabled && config_sound_on;
}

void setAudioVolume(int volume) {
  config_volume_pct = constrain(volume, 0, 100);
}

// --------------------------------------------------------------------------
// AUDIO QUEUE MANAGEMENT
// --------------------------------------------------------------------------

bool queueAudioEvent(SoundEvent evt) {
  if (!isAudioEnabled() || audioQueue == nullptr) {
    return false;
  }
  
  SoundEvent soundEvt = static_cast<SoundEvent>(evt);
  return xQueueSend(audioQueue, &soundEvt, 0) == pdTRUE;
}

void clearAudioQueue() {
  if (audioQueue != nullptr) {
    xQueueReset(audioQueue);
  }
}

int getQueuedEventCount() {
  if (audioQueue != nullptr) {
    return uxQueueMessagesWaiting(audioQueue);
  }
  return 0;
}