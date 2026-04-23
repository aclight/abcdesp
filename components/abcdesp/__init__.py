import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor, text_sensor, binary_sensor, lock, button, number, uart
from esphome.const import (
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_FAN,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)
from esphome import pins

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["climate", "sensor", "text_sensor", "binary_sensor", "lock", "button", "number"]

abcdesp_ns = cg.esphome_ns.namespace("abcdesp")
AbcdEspComponent = abcdesp_ns.class_(
    "AbcdEspComponent", cg.Component, climate.Climate, uart.UARTDevice
)
ClearHoldButton = abcdesp_ns.class_("ClearHoldButton", button.Button)
ActivateVacationButton = abcdesp_ns.class_("ActivateVacationButton", button.Button)
CancelVacationButton = abcdesp_ns.class_("CancelVacationButton", button.Button)
AllowControlLock = abcdesp_ns.class_("AllowControlLock", lock.Lock)
HoldDurationNumber = abcdesp_ns.class_("HoldDurationNumber", number.Number)
SetHoldTimeNumber = abcdesp_ns.class_("SetHoldTimeNumber", number.Number)
VacationDaysNumber = abcdesp_ns.class_("VacationDaysNumber", number.Number)
VacationMinTempNumber = abcdesp_ns.class_("VacationMinTempNumber", number.Number)
VacationMaxTempNumber = abcdesp_ns.class_("VacationMaxTempNumber", number.Number)

CONF_FLOW_PIN = "flow_pin"
CONF_OUTDOOR_TEMP_SENSOR = "outdoor_temp_sensor"
CONF_AIRFLOW_CFM_SENSOR = "airflow_cfm_sensor"
CONF_BLOWER_SENSOR = "blower_sensor"
CONF_HEAT_STAGE_SENSOR = "heat_stage_sensor"
CONF_HEAT_STAGE_TEXT_SENSOR = "heat_stage_text_sensor"
CONF_ALLOW_CONTROL_LOCK = "allow_control_lock"
CONF_INDOOR_HUMIDITY_SENSOR = "indoor_humidity_sensor"
CONF_HP_COIL_TEMP_SENSOR = "hp_coil_temp_sensor"
CONF_HP_STAGE_SENSOR = "hp_stage_sensor"
CONF_HP_STAGE_TEXT_SENSOR = "hp_stage_text_sensor"
CONF_COMMS_OK_SENSOR = "comms_ok_sensor"
CONF_HOLD_ACTIVE_SENSOR = "hold_active_sensor"
CONF_HOLD_TIME_REMAINING_SENSOR = "hold_time_remaining_sensor"
CONF_CLEAR_HOLD_BUTTON = "clear_hold_button"
CONF_HOLD_DURATION_MINUTES = "hold_duration_minutes"
CONF_HOLD_DURATION_NUMBER = "hold_duration_number"
CONF_SET_HOLD_TIME_NUMBER = "set_hold_time_number"
CONF_VACATION_DAYS_NUMBER = "vacation_days_number"
CONF_VACATION_MIN_TEMP_NUMBER = "vacation_min_temp_number"
CONF_VACATION_MAX_TEMP_NUMBER = "vacation_max_temp_number"
CONF_ACTIVATE_VACATION_BUTTON = "activate_vacation_button"
CONF_CANCEL_VACATION_BUTTON = "cancel_vacation_button"
CONF_LAST_SEEN_SENSOR = "last_seen_sensor"

CONFIG_SCHEMA = (
    climate.climate_schema(AbcdEspComponent)
    .extend(
        {
            cv.Optional(CONF_FLOW_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_OUTDOOR_TEMP_SENSOR): sensor.sensor_schema(
                unit_of_measurement="°F",
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_INDOOR_HUMIDITY_SENSOR): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_HUMIDITY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_AIRFLOW_CFM_SENSOR): sensor.sensor_schema(
                unit_of_measurement="CFM",
                accuracy_decimals=0,
                icon=ICON_FAN,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_HEAT_STAGE_SENSOR): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:fire",
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_HEAT_STAGE_TEXT_SENSOR): text_sensor.text_sensor_schema(
                icon="mdi:fire",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_HP_COIL_TEMP_SENSOR): sensor.sensor_schema(
                unit_of_measurement="°F",
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_HP_STAGE_SENSOR): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:heat-pump",
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_HP_STAGE_TEXT_SENSOR): text_sensor.text_sensor_schema(
                icon="mdi:heat-pump",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BLOWER_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_RUNNING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_COMMS_OK_SENSOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_HOLD_ACTIVE_SENSOR): binary_sensor.binary_sensor_schema(
                icon="mdi:hand-back-left",
            ),
            cv.Optional(CONF_HOLD_TIME_REMAINING_SENSOR): sensor.sensor_schema(
                unit_of_measurement="min",
                accuracy_decimals=0,
                icon="mdi:timer-sand",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CLEAR_HOLD_BUTTON): button.button_schema(
                ClearHoldButton,
                icon="mdi:hand-back-left-off",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ALLOW_CONTROL_LOCK): lock.lock_schema(
                AllowControlLock,
                icon="mdi:shield-lock",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_HOLD_DURATION_MINUTES, default=0): cv.int_range(
                min=0, max=1440
            ),
            cv.Optional(CONF_HOLD_DURATION_NUMBER): number.number_schema(
                HoldDurationNumber,
                icon="mdi:timer-cog",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_SET_HOLD_TIME_NUMBER): number.number_schema(
                SetHoldTimeNumber,
                icon="mdi:timer-edit",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_VACATION_DAYS_NUMBER): number.number_schema(
                VacationDaysNumber,
                icon="mdi:calendar-range",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_VACATION_MIN_TEMP_NUMBER): number.number_schema(
                VacationMinTempNumber,
                unit_of_measurement="°F",
                icon="mdi:thermometer-low",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_VACATION_MAX_TEMP_NUMBER): number.number_schema(
                VacationMaxTempNumber,
                unit_of_measurement="°F",
                icon="mdi:thermometer-high",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ACTIVATE_VACATION_BUTTON): button.button_schema(
                ActivateVacationButton,
                icon="mdi:airplane-takeoff",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_CANCEL_VACATION_BUTTON): button.button_schema(
                CancelVacationButton,
                icon="mdi:airplane-off",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_LAST_SEEN_SENSOR): sensor.sensor_schema(
                unit_of_measurement="s",
                accuracy_decimals=0,
                icon="mdi:clock-check-outline",
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_FLOW_PIN in config:
        flow_pin = await cg.gpio_pin_expression(config[CONF_FLOW_PIN])
        cg.add(var.set_flow_pin(flow_pin))

    if CONF_OUTDOOR_TEMP_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_OUTDOOR_TEMP_SENSOR])
        cg.add(var.set_outdoor_temp_sensor(sens))

    if CONF_INDOOR_HUMIDITY_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_INDOOR_HUMIDITY_SENSOR])
        cg.add(var.set_indoor_humidity_sensor(sens))

    if CONF_AIRFLOW_CFM_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_AIRFLOW_CFM_SENSOR])
        cg.add(var.set_airflow_cfm_sensor(sens))

    if CONF_HEAT_STAGE_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_HEAT_STAGE_SENSOR])
        cg.add(var.set_heat_stage_sensor(sens))

    if CONF_HEAT_STAGE_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_HEAT_STAGE_TEXT_SENSOR])
        cg.add(var.set_heat_stage_text_sensor(sens))

    if CONF_HP_COIL_TEMP_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_HP_COIL_TEMP_SENSOR])
        cg.add(var.set_hp_coil_temp_sensor(sens))

    if CONF_HP_STAGE_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_HP_STAGE_SENSOR])
        cg.add(var.set_hp_stage_sensor(sens))

    if CONF_HP_STAGE_TEXT_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_HP_STAGE_TEXT_SENSOR])
        cg.add(var.set_hp_stage_text_sensor(sens))

    if CONF_BLOWER_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_BLOWER_SENSOR])
        cg.add(var.set_blower_sensor(sens))

    if CONF_COMMS_OK_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_COMMS_OK_SENSOR])
        cg.add(var.set_comms_ok_sensor(sens))

    if CONF_HOLD_ACTIVE_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_HOLD_ACTIVE_SENSOR])
        cg.add(var.set_hold_active_sensor(sens))

    if CONF_HOLD_TIME_REMAINING_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_HOLD_TIME_REMAINING_SENSOR])
        cg.add(var.set_hold_time_remaining_sensor(sens))

    if CONF_CLEAR_HOLD_BUTTON in config:
        btn = await button.new_button(config[CONF_CLEAR_HOLD_BUTTON])
        cg.add(btn.set_parent(var))
        cg.add(var.set_clear_hold_button(btn))

    if CONF_ALLOW_CONTROL_LOCK in config:
        lck = await lock.new_lock(config[CONF_ALLOW_CONTROL_LOCK])
        cg.add(var.set_allow_control_lock(lck))

    if CONF_HOLD_DURATION_NUMBER in config:
        num = await number.new_number(
            config[CONF_HOLD_DURATION_NUMBER],
            min_value=0,
            max_value=1440,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_hold_duration_number(num))

    if CONF_SET_HOLD_TIME_NUMBER in config:
        num = await number.new_number(
            config[CONF_SET_HOLD_TIME_NUMBER],
            min_value=0,
            max_value=1440,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_set_hold_time_number(num))

    if CONF_VACATION_DAYS_NUMBER in config:
        num = await number.new_number(
            config[CONF_VACATION_DAYS_NUMBER],
            min_value=1,
            max_value=30,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_vacation_days_number(num))

    if CONF_VACATION_MIN_TEMP_NUMBER in config:
        num = await number.new_number(
            config[CONF_VACATION_MIN_TEMP_NUMBER],
            min_value=40,
            max_value=99,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_vacation_min_temp_number(num))

    if CONF_VACATION_MAX_TEMP_NUMBER in config:
        num = await number.new_number(
            config[CONF_VACATION_MAX_TEMP_NUMBER],
            min_value=40,
            max_value=99,
            step=1,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_vacation_max_temp_number(num))

    if CONF_ACTIVATE_VACATION_BUTTON in config:
        btn = await button.new_button(config[CONF_ACTIVATE_VACATION_BUTTON])
        cg.add(btn.set_parent(var))
        cg.add(var.set_activate_vacation_button(btn))

    if CONF_CANCEL_VACATION_BUTTON in config:
        btn = await button.new_button(config[CONF_CANCEL_VACATION_BUTTON])
        cg.add(btn.set_parent(var))
        cg.add(var.set_cancel_vacation_button(btn))

    cg.add(var.set_hold_duration_minutes(config[CONF_HOLD_DURATION_MINUTES]))

    if CONF_LAST_SEEN_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_LAST_SEEN_SENSOR])
        cg.add(var.set_last_seen_sensor(sens))
