import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor, binary_sensor, switch, uart
from esphome import pins

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["climate"]

abcdesp_ns = cg.esphome_ns.namespace("abcdesp")
AbcdEspComponent = abcdesp_ns.class_(
    "AbcdEspComponent", cg.Component, climate.Climate, uart.UARTDevice
)

CONF_FLOW_PIN = "flow_pin"
CONF_OUTDOOR_TEMP_SENSOR = "outdoor_temp_sensor"
CONF_AIRFLOW_CFM_SENSOR = "airflow_cfm_sensor"
CONF_BLOWER_SENSOR = "blower_sensor"
CONF_HEAT_STAGE_SENSOR = "heat_stage_sensor"
CONF_ALLOW_CONTROL_SWITCH = "allow_control_switch"
CONF_INDOOR_HUMIDITY_SENSOR = "indoor_humidity_sensor"
CONF_HP_COIL_TEMP_SENSOR = "hp_coil_temp_sensor"
CONF_HP_STAGE_SENSOR = "hp_stage_sensor"
CONF_COMMS_OK_SENSOR = "comms_ok_sensor"

CONFIG_SCHEMA = (
    climate.climate_schema(AbcdEspComponent)
    .extend(
        {
            cv.Optional(CONF_FLOW_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_OUTDOOR_TEMP_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_AIRFLOW_CFM_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_BLOWER_SENSOR): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_HEAT_STAGE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_ALLOW_CONTROL_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_INDOOR_HUMIDITY_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_HP_COIL_TEMP_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_HP_STAGE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_COMMS_OK_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await uart.register_uart_device(var, config)

    if CONF_FLOW_PIN in config:
        flow_pin = await cg.gpio_pin_expression(config[CONF_FLOW_PIN])
        cg.add(var.set_flow_pin(flow_pin))

    if CONF_OUTDOOR_TEMP_SENSOR in config:
        sens = await cg.get_variable(config[CONF_OUTDOOR_TEMP_SENSOR])
        cg.add(var.set_outdoor_temp_sensor(sens))

    if CONF_AIRFLOW_CFM_SENSOR in config:
        sens = await cg.get_variable(config[CONF_AIRFLOW_CFM_SENSOR])
        cg.add(var.set_airflow_cfm_sensor(sens))

    if CONF_BLOWER_SENSOR in config:
        sens = await cg.get_variable(config[CONF_BLOWER_SENSOR])
        cg.add(var.set_blower_sensor(sens))

    if CONF_HEAT_STAGE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_HEAT_STAGE_SENSOR])
        cg.add(var.set_heat_stage_sensor(sens))

    if CONF_ALLOW_CONTROL_SWITCH in config:
        sw = await cg.get_variable(config[CONF_ALLOW_CONTROL_SWITCH])
        cg.add(var.set_allow_control_switch(sw))

    if CONF_INDOOR_HUMIDITY_SENSOR in config:
        sens = await cg.get_variable(config[CONF_INDOOR_HUMIDITY_SENSOR])
        cg.add(var.set_indoor_humidity_sensor(sens))

    if CONF_HP_COIL_TEMP_SENSOR in config:
        sens = await cg.get_variable(config[CONF_HP_COIL_TEMP_SENSOR])
        cg.add(var.set_hp_coil_temp_sensor(sens))

    if CONF_HP_STAGE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_HP_STAGE_SENSOR])
        cg.add(var.set_hp_stage_sensor(sens))

    if CONF_COMMS_OK_SENSOR in config:
        sens = await cg.get_variable(config[CONF_COMMS_OK_SENSOR])
        cg.add(var.set_comms_ok_sensor(sens))
