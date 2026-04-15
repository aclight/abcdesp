#include "abcdesp.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstring>

namespace esphome {
namespace abcdesp {

static const char *const TAG = "abcdesp";

// ==========================================================================
// CRC-16/ARC (poly 0x8005 reflected = 0xA001)
// ==========================================================================
uint16_t AbcdEspComponent::crc16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0x0000;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ CRC_POLY;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// ==========================================================================
// Frame parsing — validate CRC, extract fields
// ==========================================================================
bool AbcdEspComponent::parse_frame(const uint8_t *buf, uint16_t len,
                                           InfinityFrame &frame) {
  if (len < FRAME_MIN_LEN) {
    return false;
  }

  uint8_t data_len = buf[4];
  uint16_t expected_len = FRAME_HEADER_LEN + data_len + FRAME_CRC_LEN;
  if (len < expected_len) {
    return false;
  }

  // CRC check — CRC is little-endian after header+data
  uint16_t calc = crc16(buf, FRAME_HEADER_LEN + data_len);
  uint16_t received = buf[FRAME_HEADER_LEN + data_len] |
                      (buf[FRAME_HEADER_LEN + data_len + 1] << 8);
  if (calc != received) {
    return false;
  }

  frame.dst = (buf[0] << 8) | buf[1];
  frame.src = (buf[2] << 8) | buf[3];
  frame.length = data_len;
  frame.func = buf[7];
  if (data_len > 0) {
    memcpy(frame.data, buf + FRAME_HEADER_LEN, data_len);
  }

  return true;
}

// ==========================================================================
// Frame building — header + data + CRC
// ==========================================================================
void AbcdEspComponent::build_frame(const InfinityFrame &frame,
                                           uint8_t *out, uint16_t &out_len) {
  out[0] = (frame.dst >> 8) & 0xFF;
  out[1] = frame.dst & 0xFF;
  out[2] = (frame.src >> 8) & 0xFF;
  out[3] = frame.src & 0xFF;
  out[4] = frame.length;
  out[5] = 0x00;  // PID
  out[6] = 0x00;  // EXT
  out[7] = frame.func;
  if (frame.length > 0) {
    memcpy(out + FRAME_HEADER_LEN, frame.data, frame.length);
  }

  uint16_t payload_end = FRAME_HEADER_LEN + frame.length;
  uint16_t crc = crc16(out, payload_end);
  out[payload_end] = crc & 0xFF;         // low byte first
  out[payload_end + 1] = (crc >> 8) & 0xFF;
  out_len = payload_end + FRAME_CRC_LEN;
}

// ==========================================================================
// Send frame over UART with flow control
// ==========================================================================
void AbcdEspComponent::send_frame(const uint8_t *buf, uint16_t len) {
  if (flow_pin_ != nullptr) {
    flow_pin_->digital_write(true);   // assert TX
  }
  delayMicroseconds(TX_SETTLE_US);

  write_array(buf, len);
  flush();

  delayMicroseconds(TX_SETTLE_US);
  if (flow_pin_ != nullptr) {
    flow_pin_->digital_write(false);  // back to RX
  }

  last_send_ms_ = millis();
}

// ==========================================================================
// Send ACK06 with data=0x00 (write acknowledgment)
// ==========================================================================
void AbcdEspComponent::send_ack(uint16_t dst) {
  InfinityFrame ack;
  ack.dst = dst;
  ack.src = ADDR_SAM;
  ack.func = FUNC_ACK06;
  ack.length = 1;
  ack.data[0] = 0x00;

  uint8_t buf[16];
  uint16_t len;
  build_frame(ack, buf, len);
  send_frame(buf, len);

  ESP_LOGD(TAG, "Sent ACK to 0x%04X", dst);
}

// ==========================================================================
// Send a READ request
// ==========================================================================
void AbcdEspComponent::send_read_request(uint16_t dst, uint8_t table,
                                                  uint8_t row) {
  // Enforce minimum gap between self-initiated frames
  uint32_t now = millis();
  if (now - last_send_ms_ < MIN_FRAME_GAP_MS) {
    return;
  }

  InfinityFrame f;
  f.dst = dst;
  f.src = ADDR_SAM;
  f.func = FUNC_READ;
  f.length = 3;
  f.data[0] = REG_PREFIX;
  f.data[1] = table;
  f.data[2] = row;

  uint8_t buf[16];
  uint16_t len;
  build_frame(f, buf, len);
  send_frame(buf, len);

  awaiting_response_ = true;
  request_sent_ms_ = millis();
  pending_table_ = table;
  pending_row_ = row;

  ESP_LOGD(TAG, "Sent READ 0x%02X%02X to 0x%04X", table, row, dst);
}

// ==========================================================================
// Send a WRITE request
// ==========================================================================
void AbcdEspComponent::send_write_request(uint16_t dst, uint8_t table,
                                                   uint8_t row,
                                                   const uint8_t *payload,
                                                   uint8_t payload_len) {
  uint32_t now = millis();
  if (now - last_send_ms_ < MIN_FRAME_GAP_MS) {
    return;
  }

  InfinityFrame f;
  f.dst = dst;
  f.src = ADDR_SAM;
  f.func = FUNC_WRITE;
  f.length = 3 + payload_len;
  f.data[0] = REG_PREFIX;
  f.data[1] = table;
  f.data[2] = row;
  if (payload_len > 0) {
    memcpy(f.data + 3, payload, payload_len);
  }

  uint8_t buf[270];
  uint16_t len;
  build_frame(f, buf, len);
  send_frame(buf, len);

  ESP_LOGD(TAG, "Sent WRITE 0x%02X%02X to 0x%04X (%d bytes)", table, row, dst,
           payload_len);
}

// ==========================================================================
// Component setup
// ==========================================================================
void AbcdEspComponent::setup() {
  ESP_LOGI(TAG, "ABCDESP component initializing");
  if (flow_pin_ != nullptr) {
    flow_pin_->setup();
    flow_pin_->digital_write(false);  // start in RX mode
  }
  rx_len_ = 0;
  last_poll_ms_ = millis();
  poll_step_ = 0;
}

// ==========================================================================
// Component loop — drain UART, parse frames, poll
// ==========================================================================
void AbcdEspComponent::loop() {
  // --- Drain UART into ring buffer ---
  while (available() > 0 && rx_len_ < RX_BUF_SIZE) {
    uint8_t byte;
    if (read_byte(&byte)) {
      rx_buf_[rx_len_++] = byte;
    }
  }

  // --- Try to parse frames from buffer ---
  while (rx_len_ >= FRAME_MIN_LEN) {
    uint8_t data_len = rx_buf_[4];
    uint16_t frame_len = FRAME_HEADER_LEN + data_len + FRAME_CRC_LEN;

    if (frame_len > RX_BUF_SIZE) {
      // Corrupt length, skip one byte
      memmove(rx_buf_, rx_buf_ + 1, --rx_len_);
      continue;
    }

    if (rx_len_ < frame_len) {
      break;  // need more bytes
    }

    InfinityFrame frame;
    if (parse_frame(rx_buf_, frame_len, frame)) {
      handle_frame(frame);
      // Consume the frame
      rx_len_ -= frame_len;
      if (rx_len_ > 0) {
        memmove(rx_buf_, rx_buf_ + frame_len, rx_len_);
      }
    } else {
      // CRC failed — skip one byte, try to re-sync
      memmove(rx_buf_, rx_buf_ + 1, --rx_len_);
    }
  }

  // --- Check for response timeout ---
  if (awaiting_response_ && (millis() - request_sent_ms_ > RESPONSE_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Response timeout for READ 0x%02X%02X", pending_table_,
             pending_row_);
    awaiting_response_ = false;
  }

  // --- Periodic polling of thermostat ---
  uint32_t now = millis();
  if (!awaiting_response_ && !write_pending_ &&
      (now - last_poll_ms_ >= POLL_INTERVAL_MS)) {
    poll_thermostat();
    last_poll_ms_ = now;
  }

  // --- Send pending write if gap is satisfied ---
  if (write_pending_ && !awaiting_response_ &&
      (now - last_send_ms_ >= MIN_FRAME_GAP_MS)) {
    send_write_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_ZONES,
                       write_buf_, write_len_);
    write_pending_ = false;
  }
}

// ==========================================================================
// Poll thermostat — alternate between 3B02 and 3B03
// ==========================================================================
void AbcdEspComponent::poll_thermostat() {
  switch (poll_step_) {
    case 0:
      send_read_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_STATE);
      break;
    case 1:
      send_read_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_ZONES);
      break;
  }
  poll_step_ = (poll_step_ + 1) % 2;
}

// ==========================================================================
// Frame dispatch
// ==========================================================================
void AbcdEspComponent::handle_frame(const InfinityFrame &frame) {
  ESP_LOGD(TAG, "Frame: 0x%04X -> 0x%04X func=0x%02X len=%d",
           frame.src, frame.dst, frame.func, frame.length);

  // --- ACK06/Response to our READ request ---
  if (frame.func == FUNC_ACK06 && frame.dst == ADDR_SAM) {
    handle_ack_response(frame);
    return;
  }

  // --- NAK to our request ---
  if (frame.func == FUNC_NAK && frame.dst == ADDR_SAM) {
    ESP_LOGW(TAG, "NAK received from 0x%04X", frame.src);
    awaiting_response_ = false;
    return;
  }

  // --- WRITE from thermostat to SAM (3B0E ack) ---
  if (frame.func == FUNC_WRITE && frame.dst == ADDR_SAM &&
      frame.src == ADDR_TSTAT) {
    handle_write_to_sam(frame);
    return;
  }

  // --- READ directed at SAM — we don't serve registers, ignore ---
  if (frame.func == FUNC_READ && frame.dst == ADDR_SAM) {
    // Thermostat polls SAM for 030D, 0104, etc. — just ignore.
    // The thermostat will time out, which is normal behavior.
    return;
  }

  // --- Snoop: ACK06 responses from air handler or heat pump ---
  if (frame.func == FUNC_ACK06 && frame.length > 3) {
    uint16_t src_class = frame.src & 0xFF00;
    if (src_class == 0x4000 || src_class == 0x4200 ||
        src_class == 0x5000 || src_class == 0x5100 || src_class == 0x5200) {
      handle_snooped_response(frame);
    }
  }
}

// ==========================================================================
// Handle ACK06 response to our READ
// ==========================================================================
void AbcdEspComponent::handle_ack_response(const InfinityFrame &frame) {
  awaiting_response_ = false;

  if (frame.length <= 3) {
    return;  // Simple write-ack (data=0x00), nothing to parse
  }

  // Data layout: [0]=0x00, [1]=table, [2]=row, [3..]=register content
  uint8_t table = frame.data[1];
  uint8_t row = frame.data[2];
  const uint8_t *reg_data = frame.data + 3;
  uint8_t reg_len = frame.length - 3;

  if (table == TBL_SAM_INFO && row == ROW_SAM_STATE) {
    parse_tstat_state(reg_data, reg_len);
  } else if (table == TBL_SAM_INFO && row == ROW_SAM_ZONES) {
    parse_tstat_zones(reg_data, reg_len);
  }
}

// ==========================================================================
// Handle WRITE from thermostat to SAM — just ACK it
// ==========================================================================
void AbcdEspComponent::handle_write_to_sam(const InfinityFrame &frame) {
  // ACK any writes from thermostat (typically 3B0E acknowledgments)
  send_ack(frame.src);
}

// ==========================================================================
// Handle snooped ACK06 responses from air handler / heat pump
// ==========================================================================
void AbcdEspComponent::handle_snooped_response(
    const InfinityFrame &frame) {
  if (frame.length <= 3) {
    return;
  }

  uint8_t table = frame.data[1];
  uint8_t row = frame.data[2];
  const uint8_t *reg_data = frame.data + 3;
  uint8_t reg_len = frame.length - 3;

  uint16_t src_class = frame.src & 0xFF00;

  // Air handler responses (0x40xx, 0x42xx)
  if (src_class == 0x4000 || src_class == 0x4200) {
    if (table == TBL_RLCSMAIN && row == ROW_AH_06) {
      parse_airhandler_06(reg_data, reg_len);
    } else if (table == TBL_RLCSMAIN && row == ROW_AH_16) {
      parse_airhandler_16(reg_data, reg_len);
    }
  }

  // Heat pump responses (0x50xx, 0x51xx, 0x52xx)
  if (src_class == 0x5000 || src_class == 0x5100 || src_class == 0x5200) {
    if (table == TBL_HEATPUMP && row == ROW_HP_01) {
      parse_heatpump_01(reg_data, reg_len);
    } else if (table == TBL_HEATPUMP && row == ROW_HP_02) {
      parse_heatpump_02(reg_data, reg_len);
    }
  }
}

// ==========================================================================
// Parse 3B02 — thermostat current state (29 bytes with 3-byte header)
// Offsets relative to register content start (after 3-byte register address):
//   [0]     = zone bitmap
//   [1-2]   = field flags (uint16)
//   [3-10]  = zone 1-8 current temps (uint8, °F)
//   [11-18] = zone 1-8 current humidity (uint8, %RH)
//   [19]    = unknown
//   [20]    = outdoor air temp (int8, °F)
//   [21]    = unoccupied zone flags
//   [22]    = mode (low nibble: 0=heat,1=cool,2=auto,5=off; bits 5-7=stage)
//   [23-28] = misc
// ==========================================================================
void AbcdEspComponent::parse_tstat_state(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 29) {
    ESP_LOGW(TAG, "3B02 too short: %d bytes", len);
    return;
  }

  // Zone 1 temp — uint8, direct °F
  indoor_temp_ = static_cast<float>(data[3]);
  indoor_humidity_ = data[11];
  outdoor_temp_ = static_cast<float>(static_cast<int8_t>(data[20]));
  current_mode_ = data[22] & 0x0F;

  ESP_LOGD(TAG, "3B02: indoor=%.0f°F  humid=%d%%  outdoor=%.0f°F  mode=%d",
           indoor_temp_, indoor_humidity_, outdoor_temp_, current_mode_);

  publish_climate_state();
  publish_sensors();
}

// ==========================================================================
// Parse 3B03 — thermostat zone settings (150 bytes with 3-byte header)
// Offsets relative to register content:
//   [0]     = zone bitmap
//   [1-2]   = field flags
//   [3-10]  = fan mode for zones 1-8 (uint8)
//   [11]    = hold flag bitmap
//   [12-19] = heat setpoints for zones 1-8 (uint8, °F)
//   [20-27] = cool setpoints for zones 1-8 (uint8, °F)
// ==========================================================================
void AbcdEspComponent::parse_tstat_zones(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 28) {
    ESP_LOGW(TAG, "3B03 too short: %d bytes", len);
    return;
  }

  fan_mode_ = data[3];        // Zone 1 fan mode
  zone_hold_ = data[11];
  heat_setpoint_ = data[12];  // Zone 1 heat setpoint
  cool_setpoint_ = data[20];  // Zone 1 cool setpoint

  ESP_LOGD(TAG, "3B03: fan=%d  hold=0x%02X  heat=%d  cool=%d",
           fan_mode_, zone_hold_, heat_setpoint_, cool_setpoint_);

  publish_climate_state();
}

// ==========================================================================
// Parse 0306 — air handler blower RPM
// Per Infinitive api.go: data[1:5] = blower RPM (uint16 at offset 1)
// But the wiki says: [0]=unknown, [1-2]=blower_rpm, [3-4]=indoor_airflow_cfm
// ==========================================================================
void AbcdEspComponent::parse_airhandler_06(const uint8_t *data,
                                                    uint8_t len) {
  if (len < 5) {
    return;
  }

  blower_rpm_ = (data[1] << 8) | data[2];
  // 0306 also has airflow but 0316 is more frequently updated
  bool was_running = blower_running_;
  blower_running_ = (blower_rpm_ > 0);

  ESP_LOGD(TAG, "0306: blower RPM=%d", blower_rpm_);

  if (blower_running_ != was_running) {
    publish_sensors();
  }
}

// ==========================================================================
// Parse 0316 — air handler CFM and heat state
// Per wiki: [0]=state (00=no_heat,01=low,02=med,03=high),
//           [1-3]=unknown, [4-5]=airflow_cfm
// Per Infinitive api.go: data[4:8]=CFM, data[0]&0x03=elecHeat
// ==========================================================================
void AbcdEspComponent::parse_airhandler_16(const uint8_t *data,
                                                    uint8_t len) {
  if (len < 6) {
    return;
  }

  heat_stage_ = data[0];
  airflow_cfm_ = (data[4] << 8) | data[5];
  blower_running_ = (airflow_cfm_ > 0 || blower_rpm_ > 0);

  ESP_LOGD(TAG, "0316: heat_stage=%d  CFM=%d", heat_stage_, airflow_cfm_);

  publish_sensors();
}

// ==========================================================================
// Parse 3E01 — heat pump outside/coil temps (int16/16 format)
// Per Infinitive api.go:
//   data[0:2] = outside_temp * 16  (uint16 big-endian)
//   data[2:4] = coil_temp * 16
// ==========================================================================
void AbcdEspComponent::parse_heatpump_01(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 4) {
    return;
  }

  hp_outside_temp_ =
      static_cast<float>((data[0] << 8) | data[1]) / 16.0f;
  hp_coil_temp_ =
      static_cast<float>((data[2] << 8) | data[3]) / 16.0f;

  ESP_LOGD(TAG, "3E01: outside=%.1f°F  coil=%.1f°F", hp_outside_temp_,
           hp_coil_temp_);

  // Use heat pump outside temp if we have it (higher precision than 3B02)
  if (!std::isnan(hp_outside_temp_)) {
    outdoor_temp_ = hp_outside_temp_;
    publish_sensors();
  }
}

// ==========================================================================
// Parse 3E02 — heat pump stage
// Per Infinitive api.go: data[0] >> 1 = stage
// ==========================================================================
void AbcdEspComponent::parse_heatpump_02(const uint8_t *data,
                                                  uint8_t len) {
  if (len < 1) {
    return;
  }
  hp_stage_ = data[0] >> 1;
  ESP_LOGD(TAG, "3E02: HP stage=%d", hp_stage_);
}

// ==========================================================================
// Climate traits
// ==========================================================================
climate::ClimateTraits AbcdEspComponent::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);
  traits.set_supports_two_point_target_temperature(true);
  traits.set_visual_min_temperature(40);
  traits.set_visual_max_temperature(99);
  traits.set_visual_temperature_step(1);

  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT_COOL,
  });

  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });

  return traits;
}

// ==========================================================================
// Climate control — handle HA calls to change mode/setpoint/fan
// ==========================================================================
void AbcdEspComponent::control(const climate::ClimateCall &call) {
  // Block control when Allow Control switch is off (or not configured)
  if (allow_control_switch_ == nullptr || !allow_control_switch_->state) {
    ESP_LOGW(TAG, "Control blocked: Allow Control switch is OFF");
    return;
  }

  // Build a 3B03 write payload with flag header
  // Layout: [0]=zone_bitmap, [1-2]=field_flags(BE), [3..]=zone data
  // We only write zone 1 fields.

  // Start by reading current cached state into the write buffer
  // We need at least 28 bytes (through cool setpoints)
  memset(write_buf_, 0, sizeof(write_buf_));
  write_buf_[0] = 0x01;  // zone bitmap: zone 1 only

  uint16_t flags = 0;

  // Current values as defaults
  uint8_t new_fan = fan_mode_;
  uint8_t new_hold = zone_hold_;
  uint8_t new_heat = heat_setpoint_;
  uint8_t new_cool = cool_setpoint_;
  uint8_t new_mode = current_mode_;

  if (call.get_mode().has_value()) {
    switch (*call.get_mode()) {
      case climate::CLIMATE_MODE_OFF:
        new_mode = MODE_OFF;
        break;
      case climate::CLIMATE_MODE_HEAT:
        new_mode = MODE_HEAT;
        break;
      case climate::CLIMATE_MODE_COOL:
        new_mode = MODE_COOL;
        break;
      case climate::CLIMATE_MODE_HEAT_COOL:
        new_mode = MODE_AUTO;
        break;
      default:
        break;
    }
    // Mode is written via 3B02 WRITE, not 3B03
    // We'll handle mode writes separately below
  }

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_AUTO:
        new_fan = FAN_AUTO;
        break;
      case climate::CLIMATE_FAN_LOW:
        new_fan = FAN_LOW;
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        new_fan = FAN_MED;
        break;
      case climate::CLIMATE_FAN_HIGH:
        new_fan = FAN_HIGH;
        break;
      default:
        break;
    }
    flags |= 0x0001;  // fan mode flag
  }

  if (call.get_target_temperature_low().has_value()) {
    new_heat = static_cast<uint8_t>(*call.get_target_temperature_low());
    flags |= 0x0004;  // heat setpoint flag
  }

  if (call.get_target_temperature_high().has_value()) {
    new_cool = static_cast<uint8_t>(*call.get_target_temperature_high());
    flags |= 0x0008;  // cool setpoint flag
  }

  // Handle mode change via 3B02 WRITE
  if (call.get_mode().has_value()) {
    uint8_t mode_buf[29];
    memset(mode_buf, 0, sizeof(mode_buf));
    mode_buf[0] = 0x01;               // zone bitmap
    mode_buf[1] = 0x00;               // flags high
    mode_buf[2] = 0x10;               // flags low — mode field (bit 4)
    mode_buf[22] = new_mode;
    send_write_request(ADDR_TSTAT, TBL_SAM_INFO, ROW_SAM_STATE,
                       mode_buf, 29);
    current_mode_ = new_mode;
    ESP_LOGI(TAG, "Setting mode to %d", new_mode);
  }

  // Send 3B03 if any zone settings changed
  if (flags != 0) {
    write_buf_[1] = (flags >> 8) & 0xFF;
    write_buf_[2] = flags & 0xFF;

    // Fan modes for zones 1-8 (offsets 3-10)
    write_buf_[3] = new_fan;
    for (int i = 1; i < 8; i++) {
      write_buf_[3 + i] = FAN_AUTO;
    }

    // Hold flag (offset 11) — set hold for zone 1
    write_buf_[11] = 0x01;
    flags |= 0x0002;
    write_buf_[1] = (flags >> 8) & 0xFF;
    write_buf_[2] = flags & 0xFF;

    // Heat setpoints (offsets 12-19)
    write_buf_[12] = new_heat;
    for (int i = 1; i < 8; i++) {
      write_buf_[12 + i] = new_heat;
    }

    // Cool setpoints (offsets 20-27)
    write_buf_[20] = new_cool;
    for (int i = 1; i < 8; i++) {
      write_buf_[20 + i] = new_cool;
    }

    write_len_ = 28;
    write_pending_ = true;

    fan_mode_ = new_fan;
    heat_setpoint_ = new_heat;
    cool_setpoint_ = new_cool;

    ESP_LOGI(TAG, "Queued 3B03 write: fan=%d heat=%d cool=%d", new_fan,
             new_heat, new_cool);
  }

  publish_climate_state();
}

// ==========================================================================
// Publish climate entity state to HA
// ==========================================================================
void AbcdEspComponent::publish_climate_state() {
  // Current temperature
  if (!std::isnan(indoor_temp_)) {
    this->current_temperature = indoor_temp_;
  }

  // Target temperatures
  this->target_temperature_low = static_cast<float>(heat_setpoint_);
  this->target_temperature_high = static_cast<float>(cool_setpoint_);

  // Mode
  switch (current_mode_) {
    case MODE_HEAT:
      this->mode = climate::CLIMATE_MODE_HEAT;
      break;
    case MODE_COOL:
      this->mode = climate::CLIMATE_MODE_COOL;
      break;
    case MODE_AUTO:
      this->mode = climate::CLIMATE_MODE_HEAT_COOL;
      break;
    case MODE_OFF:
    default:
      this->mode = climate::CLIMATE_MODE_OFF;
      break;
  }

  // Fan mode
  switch (fan_mode_) {
    case FAN_LOW:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case FAN_MED:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case FAN_HIGH:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    case FAN_AUTO:
    default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
  }

  // Action
  if (current_mode_ == MODE_OFF) {
    this->action = climate::CLIMATE_ACTION_OFF;
  } else if (blower_running_) {
    if (heat_stage_ > 0) {
      this->action = climate::CLIMATE_ACTION_HEATING;
    } else if (current_mode_ == MODE_COOL ||
             (current_mode_ == MODE_AUTO && !std::isnan(indoor_temp_) &&
              indoor_temp_ > static_cast<float>(cool_setpoint_))) {
      this->action = climate::CLIMATE_ACTION_COOLING;
    } else if (current_mode_ == MODE_HEAT ||
             (current_mode_ == MODE_AUTO && !std::isnan(indoor_temp_) &&
              indoor_temp_ < static_cast<float>(heat_setpoint_))) {
      this->action = climate::CLIMATE_ACTION_HEATING;
    } else {
      this->action = climate::CLIMATE_ACTION_FAN;
    }
  } else {
    this->action = climate::CLIMATE_ACTION_IDLE;
  }

  this->publish_state();
}

// ==========================================================================
// Publish sensor entities
// ==========================================================================
void AbcdEspComponent::publish_sensors() {
  if (outdoor_temp_sensor_ != nullptr && !std::isnan(outdoor_temp_)) {
    outdoor_temp_sensor_->publish_state(outdoor_temp_);
  }

  if (airflow_cfm_sensor_ != nullptr) {
    airflow_cfm_sensor_->publish_state(static_cast<float>(airflow_cfm_));
  }

  if (blower_sensor_ != nullptr) {
    blower_sensor_->publish_state(blower_running_);
  }

  if (heat_stage_sensor_ != nullptr) {
    heat_stage_sensor_->publish_state(static_cast<float>(heat_stage_));
  }
}

// ==========================================================================
// Dump config
// ==========================================================================
void AbcdEspComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ABCDESP HVAC:");
  ESP_LOGCONFIG(TAG, "  SAM Address: 0x%04X", ADDR_SAM);
  ESP_LOGCONFIG(TAG, "  Thermostat Address: 0x%04X", ADDR_TSTAT);
  ESP_LOGCONFIG(TAG, "  Poll Interval: %d ms", POLL_INTERVAL_MS);
  if (flow_pin_ != nullptr) {
    LOG_PIN("  Flow Control Pin: ", flow_pin_);
  }
  LOG_SENSOR("  ", "Outdoor Temp", outdoor_temp_sensor_);
  LOG_SENSOR("  ", "Airflow CFM", airflow_cfm_sensor_);
  LOG_BINARY_SENSOR("  ", "Blower", blower_sensor_);
  LOG_SENSOR("  ", "Heat Stage", heat_stage_sensor_);
}

}  // namespace abcdesp
}  // namespace esphome
