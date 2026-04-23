# Hardware Testing Plan — Configurable Vacation Parameters

These tests **must be run on real Carrier Infinity equipment** after merging.
Monitor the thermostat display and ESPHome logs closely.

## Prerequisites

- ESP32 connected to Carrier Infinity bus and communicating (Comms OK = true)
- Allow Control switch: ON
- System NOT in vacation mode at start
- ESPHome logs visible at DEBUG level
- Home Assistant dashboard open showing the climate entity

---

## Test Cases

### 1. Activate vacation with default parameters

**Steps:**
1. Do NOT set any vacation number entities (leave at defaults)
2. Set climate preset to "Away" in HA

**Expected:**
- Thermostat enters vacation mode
- Thermostat display shows: 7 days, 60–80°F
- Climate entity shows preset "Away"
- Log: "Activating vacation mode (7 days, 60-80F)"

### 2. Cancel vacation from HA

**Steps:**
1. With vacation active from test 1, set preset to "Home"

**Expected:**
- Thermostat exits vacation mode
- Climate entity shows preset "Home"
- Log: "Deactivating vacation mode"

### 3. Configure custom parameters then activate

**Steps:**
1. Set "Vacation Days" number to 14
2. Set "Vacation Min Temp" number to 55
3. Set "Vacation Max Temp" number to 85
4. Set climate preset to "Away"

**Expected:**
- Thermostat enters vacation mode
- Thermostat display shows: 14 days, 55–85°F
- Log: "Activating vacation mode (14 days, 55-85F)"

### 4. Verify number entities update from thermostat

**Steps:**
1. Activate vacation from the physical thermostat (not HA) with custom values
2. Wait for a 3B04 poll cycle (~15 seconds)

**Expected:**
- Vacation Days, Vacation Min Temp, Vacation Max Temp number entities update to match thermostat values
- Climate entity shows preset "Away"

### 5. Activate vacation with 1 day (minimum)

**Steps:**
1. Set "Vacation Days" to 1
2. Leave temps at defaults (60–80°F)
3. Set preset to "Away"

**Expected:**
- Thermostat shows 1 day vacation
- Log: "Activating vacation mode (1 days, 60-80F)"

### 6. Activate vacation with 30 days (maximum)

**Steps:**
1. Set "Vacation Days" to 30
2. Set preset to "Away"

**Expected:**
- Thermostat shows 30 day vacation
- No NAK responses

### 7. Narrow temperature range

**Steps:**
1. Set "Vacation Min Temp" to 70
2. Set "Vacation Max Temp" to 72
3. Set preset to "Away"

**Expected:**
- Thermostat accepts narrow range
- Thermostat display shows 70–72°F

### 8. Backward compatibility — no number entities configured

**Steps:**
1. Remove `vacation_days_number`, `vacation_min_temp_number`, `vacation_max_temp_number` from YAML
2. Flash and set preset to "Away"

**Expected:**
- Vacation activates with defaults: 7 days, 60–80°F
- Behavior identical to before this PR

### 9. Cancel vacation from thermostat, verify HA updates

**Steps:**
1. With vacation active (started from HA), cancel from the physical thermostat
2. Wait for a poll cycle

**Expected:**
- Climate entity updates to preset "Home"
- Log: "3B04: vacation=off"

### 10. Vacation activation blocked when Allow Control is OFF

**Steps:**
1. Turn Allow Control switch OFF
2. Try to set preset to "Away"

**Expected:**
- No write sent to bus
- Log: "Control blocked: Allow Control switch is OFF"
- Climate entity remains on "Home"

---

## Failure Criteria

If any of the following occur, **do not merge**:
- Thermostat shows error codes or enters fault state
- HVAC system starts/stops unexpectedly
- NAK responses to vacation writes (check logs)
- Communication lost after writes
- Thermostat vacation display doesn't match configured values
