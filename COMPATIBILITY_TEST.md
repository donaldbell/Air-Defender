# ESP32 Dual-Board Compatibility Test Instructions

## Test Procedure:

### Step 1: Test Display Unit (Your XX5R69 Board)

1. **Modify platformio.ini to use display test:**
   - Comment out `src_filter = <*> -<*.cpp>` 
   - Add `src_filter = <*> -<main.cpp> -<test_console_unit.cpp>`
   - Set `src_dir = .` and make sure `test_display_unit.cpp` is compiled

2. **Upload to your XX5R69 board:**
   ```
   pio run --target upload
   ```

3. **Check Serial Monitor:**
   - Should show rainbow LED test on startup
   - Note the MAC address displayed
   - Should see "Display unit ready!" message
   - Blue heartbeat flash every 2 seconds

### Step 2: Set Up Console Unit (Your Main Board)

1. **Update test_console_unit.cpp:**
   - Replace the displayUnitMAC array with the MAC address from Step 1
   - Example: `{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}`

2. **Modify platformio.ini for console test:**
   - Change `src_filter = <*> -<main.cpp> -<test_display_unit.cpp>`

3. **Upload to main board:**
   ```
   pio run --target upload
   ```

### Step 3: Test Communication

1. **Open Serial Monitor on console board**
2. **Send test commands:**
   - Press `1` for red test pattern
   - Press `2` for green test pattern  
   - Press `3` for blue test pattern
   - Press `4` to clear display
   - Press `5` for rainbow segments

### Expected Results:

✅ **Display Unit:**
- LEDs work and respond to FastLED commands
- ESP-NOW receives messages successfully
- Serial shows incoming command details

✅ **Console Unit:**  
- ESP-NOW sends messages successfully
- Serial confirms message delivery status

✅ **Communication:**
- Commands trigger correct LED responses
- No significant latency in LED updates
- Reliable message delivery

### Troubleshooting:

- **No LED response:** Check LED_PIN in test_display_unit.cpp (try GPIO 2, 4, 16, 17)
- **ESP-NOW fails:** Verify both boards have ESP-NOW capability
- **Message delivery fails:** Double-check MAC address in console code
- **Compilation errors:** Ensure FastLED library is installed

### Pin Compatibility Notes:

- **Safe GPIO pins for LEDs:** 2, 4, 16, 17, 18, 19, 21, 22, 23
- **Avoid:** GPIO 0, 6-11 (flash), 34-39 (input-only)

Let me know the results and any error messages!