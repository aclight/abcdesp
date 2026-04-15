#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/button/button.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace abcdesp {

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------
static const uint8_t FRAME_HEADER_LEN   = 8;
static const uint8_t FRAME_CRC_LEN      = 2;
static const uint8_t FRAME_MIN_LEN      = FRAME_HEADER_LEN + FRAME_CRC_LEN; // 10

// Device addresses (full 16-bit: addr << 8 | bus)
static const uint16_t ADDR_TSTAT        = 0x2001;
static const uint16_t ADDR_AIR_HANDLER  = 0x4001;
static const uint16_t ADDR_HEAT_PUMP    = 0x5001;
static const uint16_t ADDR_SAM          = 0x9201;
static const uint16_t ADDR_BROADCAST    = 0xF1F1;

// Function codes
static const uint8_t FUNC_ACK02         = 0x02;
static const uint8_t FUNC_ACK06         = 0x06;
static const uint8_t FUNC_READ          = 0x0B;
static const uint8_t FUNC_WRITE         = 0x0C;
static const uint8_t FUNC_NAK           = 0x15;

// Register addresses (table, row) — sent as 3 bytes: 0x00, table, row
static const uint8_t REG_PREFIX         = 0x00;
static const uint8_t TBL_SAM_INFO       = 0x3B;
static const uint8_t ROW_SAM_STATE      = 0x02;  // 3B02 — temps, mode
static const uint8_t ROW_SAM_ZONES      = 0x03;  // 3B03 — setpoints, fan
static const uint8_t ROW_SAM_VACATION   = 0x04;  // 3B04 — vacation settings
static const uint8_t ROW_SAM_ACK        = 0x0E;  // 3B0E — ack from tstat

static const uint8_t TBL_RLCSMAIN      = 0x03;
static const uint8_t ROW_AH_06         = 0x06;   // 0306 — blower RPM
static const uint8_t ROW_AH_16         = 0x16;   // 0316 — CFM, heat state

static const uint8_t TBL_HEATPUMP      = 0x3E;
static const uint8_t ROW_HP_01         = 0x01;   // 3E01 — coil/outside temps
static const uint8_t ROW_HP_02         = 0x02;   // 3E02 — stage

// Mode values (low nibble of 3B02 byte 22)
static const uint8_t MODE_HEAT          = 0x00;
static const uint8_t MODE_COOL          = 0x01;
static const uint8_t MODE_AUTO          = 0x02;
static const uint8_t MODE_OFF           = 0x05;

// Fan mode values
static const uint8_t FAN_AUTO           = 0x00;
static const uint8_t FAN_LOW            = 0x01;
static const uint8_t FAN_MED            = 0x02;
static const uint8_t FAN_HIGH           = 0x03;

// Timing
static const uint32_t POLL_INTERVAL_MS        = 5000;
static const uint32_t RESPONSE_TIMEOUT_MS     = 500;
static const uint32_t TX_SETTLE_US            = 100;
static const uint32_t MIN_FRAME_GAP_MS        = 50;
static const uint16_t RX_BUF_SIZE             = 512;

// Communication health — consider comms failed if no response in this many ms
static const uint32_t COMMS_TIMEOUT_MS          = 30000;

// CRC-16/ARC: poly 0x8005, reflected, init 0x0000
static const uint16_t CRC_POLY          = 0xA001; // reflected 0x8005

// ---------------------------------------------------------------------------
// Frame structure
// ---------------------------------------------------------------------------
struct InfinityFrame {
  uint16_t dst;
  uint16_t src;
  uint8_t  length;
  uint8_t  func;
  uint8_t  data[255];
};

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------
class AbcdEspComponent;

// ---------------------------------------------------------------------------
// Clear Hold button — calls back into the main component
// ---------------------------------------------------------------------------
class ClearHoldButton : public button::Button {
 public:
  void set_parent(AbcdEspComponent *parent) { parent_ = parent; }

 protected:
  void press_action() override;
  AbcdEspComponent *parent_{nullptr};
};

// ---------------------------------------------------------------------------
// Allow Control switch — optimistic toggle, defaults to OFF on boot
// ---------------------------------------------------------------------------
class AllowControlSwitch : public switch_::Switch {
 protected:
  void write_state(bool state) override { publish_state(state); }
};

// ---------------------------------------------------------------------------
// Main Component
// ---------------------------------------------------------------------------
class AbcdEspComponent : public Component,
                         public climate::Climate,
                         public uart::UARTDevice {
 public:
  void set_flow_pin(GPIOPin *pin) { flow_pin_ = pin; }
  void set_outdoor_temp_sensor(sensor::Sensor *s) { outdoor_temp_sensor_ = s; }
  void set_airflow_cfm_sensor(sensor::Sensor *s) { airflow_cfm_sensor_ = s; }
  void set_blower_sensor(binary_sensor::BinarySensor *s) { blower_sensor_ = s; }
  void set_heat_stage_sensor(sensor::Sensor *s) { heat_stage_sensor_ = s; }
  void set_heat_stage_text_sensor(text_sensor::TextSensor *s) { heat_stage_text_sensor_ = s; }
  void set_allow_control_switch(switch_::Switch *sw) { allow_control_switch_ = sw; }
  void set_indoor_humidity_sensor(sensor::Sensor *s) { indoor_humidity_sensor_ = s; }
  void set_hp_coil_temp_sensor(sensor::Sensor *s) { hp_coil_temp_sensor_ = s; }
  void set_hp_stage_sensor(sensor::Sensor *s) { hp_stage_sensor_ = s; }
  void set_comms_ok_sensor(binary_sensor::BinarySensor *s) { comms_ok_sensor_ = s; }
  void set_hold_active_sensor(binary_sensor::BinarySensor *s) { hold_active_sensor_ = s; }
  void set_clear_hold_button(ClearHoldButton *b) { clear_hold_button_ = b; }

  // Clear hold — sends a 3B03 write clearing the hold flag
  void clear_hold();

  // Component overrides
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Climate overrides
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  // UART / flow control
  GPIOPin *flow_pin_{nullptr};

  // Receive buffer
  uint8_t rx_buf_[RX_BUF_SIZE];
  uint16_t rx_len_{0};

  // Frame processing
  uint16_t crc16(const uint8_t *data, uint16_t len);
  bool parse_frame(const uint8_t *buf, uint16_t len, InfinityFrame &frame);
  void build_frame(const InfinityFrame &frame, uint8_t *out, uint16_t &out_len);
  void send_frame(const uint8_t *buf, uint16_t len);
  void send_ack(uint16_t dst);
  void send_read_request(uint16_t dst, uint8_t table, uint8_t row);
  void send_write_request(uint16_t dst, uint8_t table, uint8_t row,
                          const uint8_t *payload, uint8_t payload_len);

  // Frame dispatch
  void handle_frame(const InfinityFrame &frame);
  void handle_ack_response(const InfinityFrame &frame);
  void handle_write_to_sam(const InfinityFrame &frame);
  void handle_snooped_response(const InfinityFrame &frame);

  // Data parsing
  void parse_tstat_state(const uint8_t *data, uint8_t len);
  void parse_tstat_zones(const uint8_t *data, uint8_t len);
  void parse_airhandler_06(const uint8_t *data, uint8_t len);
  void parse_airhandler_16(const uint8_t *data, uint8_t len);
  void parse_heatpump_01(const uint8_t *data, uint8_t len);
  void parse_heatpump_02(const uint8_t *data, uint8_t len);
  void parse_vacation(const uint8_t *data, uint8_t len);
  void parse_vacation(const uint8_t *data, uint8_t len);

  // Polling
  void poll_thermostat();
  uint32_t last_poll_ms_{0};
  uint32_t last_send_ms_{0};

  // Pending read tracking
  bool awaiting_response_{false};
  uint32_t request_sent_ms_{0};
  uint8_t pending_table_{0};
  uint8_t pending_row_{0};
  uint8_t poll_step_{0};

  // Communication health
  uint32_t last_successful_response_ms_{0};
  bool comms_ok_{false};

  // Bus device detection
  bool seen_thermostat_{false};
  bool seen_air_handler_{false};
  bool seen_heat_pump_{false};
  bool bus_detection_logged_{false};

  // Pending write
  bool write_pending_{false};
  uint8_t write_buf_[160];
  uint8_t write_len_{0};

  // Cached state
  float indoor_temp_{NAN};
  float outdoor_temp_{NAN};
  uint8_t indoor_humidity_{0};
  uint8_t current_mode_{MODE_OFF};
  uint8_t fan_mode_{FAN_AUTO};
  uint8_t heat_setpoint_{68};
  uint8_t cool_setpoint_{74};
  uint8_t zone_hold_{0};

  // Vacation state
  bool vacation_active_{false};
  bool vacation_initialized_{false};

  // Vacation state
  bool vacation_active_{false};
  bool vacation_initialized_{false};

  // Air handler state
  uint16_t blower_rpm_{0};
  uint16_t airflow_cfm_{0};
  uint8_t heat_stage_{0};
  bool blower_running_{false};

  // Heat pump state
  float hp_outside_temp_{NAN};
  float hp_coil_temp_{NAN};
  uint8_t hp_stage_{0};

  // Previous sensor values for deduplication
  float prev_outdoor_temp_{NAN};
  uint16_t prev_airflow_cfm_{UINT16_MAX};
  bool prev_blower_running_{false};
  uint8_t prev_heat_stage_{UINT8_MAX};
  uint8_t prev_indoor_humidity_{UINT8_MAX};
  float prev_hp_coil_temp_{NAN};
  uint8_t prev_hp_stage_{UINT8_MAX};

  // Sensor pointers
  sensor::Sensor *outdoor_temp_sensor_{nullptr};
  sensor::Sensor *airflow_cfm_sensor_{nullptr};
  binary_sensor::BinarySensor *blower_sensor_{nullptr};
  sensor::Sensor *heat_stage_sensor_{nullptr};
  text_sensor::TextSensor *heat_stage_text_sensor_{nullptr};
  sensor::Sensor *indoor_humidity_sensor_{nullptr};
  sensor::Sensor *hp_coil_temp_sensor_{nullptr};
  sensor::Sensor *hp_stage_sensor_{nullptr};

  // Control gate
  switch_::Switch *allow_control_switch_{nullptr};

  // Communication health
  binary_sensor::BinarySensor *comms_ok_sensor_{nullptr};

  // Hold status
  binary_sensor::BinarySensor *hold_active_sensor_{nullptr};
  bool prev_hold_active_{false};
  bool hold_active_initialized_{false};

  // Clear hold button
  ClearHoldButton *clear_hold_button_{nullptr};

  // Publish helpers
  void publish_climate_state();
  void publish_sensors();
};

}  // namespace abcdesp
}  // namespace esphome
