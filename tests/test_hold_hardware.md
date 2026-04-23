# Hardware Testing Plan — Runtime Hold Duration

These tests **must be run on real Carrier Infinity equipment** to verify hold duration behavior.
The HVAC bus is sensitive to incorrect writes — monitor the thermostat display and ESPHome logs closely.

> **How to use this file:** Copy into a GitHub Issue (checkboxes become interactive) or edit directly.
> Mark each test PASS or FAIL, and fill in the Actual / Notes field with observations.
> If any test fails, paste the filled-in section back to Copilot for diagnosis.

## Prerequisites

- [ ] ESP32 connected to Carrier Infinity bus and communicating (Comms OK = true)
- [ ] Allow Control switch: ON
- [ ] System in heat or cool mode (not off)
- [ ] ESPHome logs visible at DEBUG level
- [ ] Home Assistant dashboard open showing the climate entity

**Firmware version:** _______________
**Date tested:** _______________
**Tester:** _______________

---

### 1. Setpoint change with hold_duration_number configured

**Steps:**
1. Set Hold Duration number entity to 120 (minutes) in HA
2. Change a setpoint from the HA thermostat card
3. Observe thermostat display and ESPHome logs

**Expected:**
- Hold Active binary sensor → ON
- Hold Time Remaining sensor ≈ 120 min, counting down
- Thermostat display shows timed hold with ~120 minutes
- Log: "Setting native timed hold for 120 minutes"

**Hold Time Remaining observed:** ___ min

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 2. Adjust active hold time via Set Hold Time number

**Steps:**
1. With an active hold from test 1, set "Set Hold Time" number to 60
2. Observe thermostat display

**Expected:**
- Hold remains active
- Hold Time Remaining updates to ≈ 60 min
- Thermostat display shows updated countdown
- Log: "Adjusting hold to 60 minutes"

**Hold Time Remaining observed:** ___ min

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 3. Make active hold permanent via Set Hold Time

**Steps:**
1. With an active timed hold, set "Set Hold Time" number to 0
2. Observe thermostat display

**Expected:**
- Hold remains active
- Hold Time Remaining → 0 (no timed override)
- Thermostat display shows permanent hold (no countdown)
- Log: "Adjusting hold to permanent"

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 4. Adjust hold blocked when no hold active

**Steps:**
1. Clear any active hold (press Clear Hold button)
2. Verify Hold Active = OFF
3. Set "Set Hold Time" number to 60

**Expected:**
- No write sent to the bus
- Log: "Adjust hold blocked: no active hold on zone 1"
- Thermostat display unchanged

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 5. Adjust hold blocked when Allow Control is OFF

**Steps:**
1. Set a hold via thermostat (not HA)
2. Turn Allow Control switch OFF in HA
3. Set "Set Hold Time" number to 60

**Expected:**
- No write sent to the bus
- Log: "Adjust hold blocked: Allow Control switch is OFF"

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 6. Change default duration and verify next setpoint change uses it

**Steps:**
1. Set Hold Duration number to 30
2. Change a setpoint from HA

**Expected:**
- New hold has ≈ 30 min countdown
- Log: "Setting native timed hold for 30 minutes"

**Hold Time Remaining observed:** ___ min

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 7. Set Hold Duration to 0 (permanent) and change setpoint

**Steps:**
1. Set Hold Duration number to 0
2. Change a setpoint from HA

**Expected:**
- Hold is permanent (no timed override)
- Hold Time Remaining stays 0
- Thermostat display shows permanent hold

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 8. Reboot persistence of Hold Duration

**Steps:**
1. Set Hold Duration number to 45
2. Reboot the ESP32 (HA → Developer Tools → Services → `esphome.abcdesp_restart`)
3. After reconnect, check Hold Duration number value

**Expected:**
- Hold Duration number restores to 45

**Hold Duration after reboot:** ___

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 9. Backward compatibility — no number entities configured

- [ ] **Skipped** (requires YAML change and reflash)

**Steps:**
1. Remove `hold_duration_number` and `set_hold_time_number` from YAML
2. Keep `hold_duration_minutes: 120` in YAML
3. Flash and change a setpoint

**Expected:**
- Behavior identical to before this feature
- Timed hold uses compile-time 120 minute value
- No errors in logs about missing entities

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

### 10. Hold expiry via ESP fallback timer

**Steps:**
1. Set Hold Duration number to 2 (2 minutes for quick testing)
2. Change a setpoint from HA
3. Wait > 2 minutes

**Expected:**
- After ≈ 2 minutes, hold auto-clears
- Hold Active → OFF
- Log: "Temporary hold expired after 2 minutes — clearing"

**Time until hold cleared:** ___ min

**Result:**
- [ ] PASS
- [ ] FAIL

**Actual / Notes:**
> 

---

## Summary

| Test | Description | Result |
|------|-------------|--------|
| 1 | Setpoint with hold duration | |
| 2 | Adjust active hold time | |
| 3 | Convert to permanent hold | |
| 4 | Adjust blocked (no hold) | |
| 5 | Adjust blocked (Allow Control OFF) | |
| 6 | Change default duration | |
| 7 | Permanent hold (duration = 0) | |
| 8 | Reboot persistence | |
| 9 | Backward compatibility | |
| 10 | Hold expiry timer | |
| **TOTAL** | **10 tests** | |

## Failure Criteria

If any of the following occur, **stop testing and investigate**:
- Thermostat shows error codes or enters fault state
- HVAC system starts/stops unexpectedly
- NAK responses to adjust_hold writes (check logs)
- Communication lost after writes
- Hold state becomes inconsistent between thermostat display and HA
