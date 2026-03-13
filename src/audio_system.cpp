#include "audio_system.h"

// --------------------------------------------------------------------------
// AUDIO CONFIGURATION STRINGS
// --------------------------------------------------------------------------

const String MELODY_SHOT_BLUE = "698,50;659,50;698,30";  // Enhanced blue shot with echo
const String MELODY_SHOT_RED = "784,30;1047,30;1319,30;784,20";  // Enhanced red shot with tail  
const String MELODY_SHOT_GREEN = "523,30;554,30;523,30";  // Restored interesting green shot - C5 trill
const String MELODY_ENEMY_HIT = "2093,30";
const String MELODY_ENEMY_DESTROYED = "1500,20;1200,20;900,20;700,20;500,20;300,20";  // Explosion cascade
const String MELODY_PLAYER_HIT = "370,100;349,100;330,100;311,400";
const String MELODY_LEVEL_COMPLETE = "392,120;523,120;659,120;784,120;1047,120;1319,120;1568,120;1319,120;0,40;415,120;523,120;622,120;830,120;1047,120;1319,120;1661,120;1319,120;0,40;466,120;587,120;698,120;932,120;1175,120;1397,120;1865,120;1865,120;1865,120;2093,180;0,200;523,100;659,100;784,100;1047,200;0,100;1319,80;1568,80;2093,300;2349,200;0,300";  // Extended Mario Course Clear with finale
const String MELODY_GAME_OVER = "370,100;349,100;330,100;311,400";
const String MELODY_BUTTON_PRESS = "523,80;659,80;784,80;1047,300";
const String MELODY_POWER_UP = "130,80;110,80;90,100;70,100;50,120";  // Deep bass rumble
const String MELODY_VICTORY = "523,80;659,80;784,80;1047,300;0,150;1047,60;1319,60;0,100;523,60;659,60;784,60;1047,200";  // Extended win melody
const String MELODY_BOSS_DEFEAT = "220,30;180,30;150,30;120,30;100,30";  // Doppler bomb drop
const String MELODY_ENEMIES_BUILDING = "147,140;165,140;185,140;208,140;233,140;262,140;294,150";  // D3 to D4 - extended 990ms bubbling growth

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
    Serial.printf("Audio disabled - sound event %d ignored\\n", evt);
    return;
  }
  
  SoundEvent soundEvt = static_cast<SoundEvent>(evt);
  BaseType_t result = xQueueSend(audioQueue, &soundEvt, 0);
  if (result == pdTRUE) {
    Serial.printf("✓ Audio event %d queued successfully\\n", evt);
  } else {
    Serial.printf("✗ Failed to queue audio event %d\\n", evt);
  }
}

void playCannonChargeSound(int stage) {
  if (!config_sound_on) {
    Serial.println("Audio disabled - cannon charge sound ignored");
    return;
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
  Serial.println("Audio task starting...");
  Serial.printf("Task running on core: %d\\n", xPortGetCoreID());
  Serial.printf("Free heap in audio task: %d bytes\\n", ESP.getFreeHeap());
  
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
  Serial.printf("Verifying GPIO pins - BCLK: %d, LRC: %d, DOUT: %d, SD: %d\\n", 
                I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_SD);
  
  Serial.println("Installing I2S driver...");
  esp_err_t result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (result == ESP_OK) {
    Serial.println("✓ I2S driver installed successfully");
  } else {
    Serial.printf("✗ I2S driver install failed: %d (ESP_ERR_NO_MEM=%d, ESP_ERR_INVALID_ARG=%d)\\n", 
                  result, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG);
    vTaskDelete(NULL);
    return;
  }
  
  Serial.println("Setting I2S pins...");
  result = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (result == ESP_OK) {
    Serial.println("✓ I2S pins configured successfully");
  } else {
    Serial.printf("✗ I2S pin config failed: %d\\n", result);
    i2s_driver_uninstall(I2S_NUM_0);
    vTaskDelete(NULL);
    return;
  }
  
  // Start I2S driver
  Serial.println("Starting I2S driver...");
  result = i2s_start(I2S_NUM_0);
  if (result == ESP_OK) {
    Serial.println("✓ I2S driver started successfully");
  } else {
    Serial.printf("✗ I2S driver start failed: %d\\n", result);
  }
  
  i2s_zero_dma_buffer(I2S_NUM_0);
  
  // Audio system ready
  Serial.println("✓ Audio task initialized and ready");

  const Melody* currentMelody = nullptr;
  int noteIndex = 0;
  unsigned long debugTimer = 0;
  int soundsPlayed = 0;
  
  while(true) {
    // Periodic I2S status check
    if (millis() - debugTimer > 10000) { // Every 10 seconds
      debugTimer = millis();
      Serial.printf("I2S Status Check: Sounds played: %d, Free heap: %d bytes\\n", 
                    soundsPlayed, ESP.getFreeHeap());
    }
    
    SoundEvent newEvent;
    if (xQueueReceive(audioQueue, &newEvent, 10) == pdTRUE) {
      Serial.printf("Processing audio event: %d (Queue messages waiting: %d)\\n", 
                    newEvent, uxQueueMessagesWaiting(audioQueue));
      
      bool play = true;
      if ((currentMelody == &melWin || currentMelody == &melLose) && newEvent != EVT_START) {
        play = false;
        Serial.println("Ignoring event - priority melody playing");
      }
      
      if (play) {
        switch(newEvent) {
          case EVT_START:      currentMelody = &melStart; Serial.println("Playing start melody"); break;
          case EVT_SHOT_BLUE:  currentMelody = &melShotBlue; Serial.println("Playing blue shot"); break;
          case EVT_SHOT_RED:   currentMelody = &melShotRed; Serial.println("Playing red shot"); break;
          case EVT_SHOT_GREEN: currentMelody = &melShotGreen; Serial.println("Playing green shot"); break;
          case EVT_SHOT_WHITE: currentMelody = &melShotWhite; Serial.println("Playing white shot"); break;
          case EVT_SHOT_CANNON: currentMelody = &melShotCannon; Serial.println("Playing cannon shot"); break;
          case EVT_MISTAKE:    currentMelody = &melMistake; Serial.println("Playing mistake sound"); break;
          case EVT_HIT_SUCCESS:currentMelody = &melHit; Serial.println("Playing hit sound"); break;
          case EVT_WIN:        currentMelody = &melWin; Serial.println("Playing win melody"); break;
          case EVT_LOSE:       currentMelody = &melLose; Serial.println("Playing lose melody"); break;
          case EVT_FIREWORK_LAUNCH: currentMelody = &melFireworkLaunch; Serial.println("Playing firework launch"); break;
          case EVT_FIREWORK_EXPLODE: currentMelody = &melFireworkExplode; Serial.println("Playing firework explosion"); break;
          case EVT_ENEMIES_BUILDING: currentMelody = &melEnemiesBuilding; Serial.println("Playing enemies building"); break;
          case EVT_LEVEL_VICTORY: currentMelody = &melLevelVictory; Serial.println("Playing level victory"); break;
          default: Serial.printf("Unknown audio event: %d\\n", newEvent); break;
        }
        noteIndex = 0;
        i2s_zero_dma_buffer(I2S_NUM_0);
        soundsPlayed++;
      }
    }

    if (currentMelody != nullptr) {
      if (noteIndex >= currentMelody->size()) {
        Serial.printf("Melody finished (played %d notes)\\n", noteIndex);
        currentMelody = nullptr; 
        playToneI2S(0, 20); 
        i2s_zero_dma_buffer(I2S_NUM_0);
      } else {
        ToneCmd t = (*currentMelody)[noteIndex];
        Serial.printf("Playing note %d/%d: %dHz for %dms\\n", 
                      noteIndex+1, currentMelody->size(), t.freq, t.duration);
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
    Serial.println("✗ Failed to create audio queue");
    return;
  }
  
  // Initialize melodies from string configurations
  if (config_sound_on) {
    Serial.println("🎵 Initializing audio melodies...");
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
    
    // Create audio task on core 0
    BaseType_t result = xTaskCreatePinnedToCore(audioTask, "AudioTask", 10240, NULL, 2, &audioTaskHandle, 0);
    if (result == pdPASS) {
      Serial.println("✓ Audio task created successfully");
      audioEnabled = true;
    } else {
      Serial.printf("✗ Failed to create audio task: %d\\n", result);
      audioEnabled = false;
    }
  } else {
    Serial.println("🔇 Audio disabled in configuration");
    audioEnabled = false;
  }
}

void enableAudio() {
  config_sound_on = true;
  audioEnabled = true;
}

void disableAudio() {
  config_sound_on = false;
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