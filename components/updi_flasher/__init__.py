import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import pins
from esphome.components.esp32 import get_esp32_variant
from esphome.components import web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID

AUTO_LOAD = ["web_server_base"]
CODEOWNERS = []

CONF_PIN = "pin"
CONF_UART_NUM = "uart_num"
CONF_BAUD_RATE = "baud_rate"

updi_ns = cg.esphome_ns.namespace("updi_flasher")
UpdiFlasher = updi_ns.class_("UpdiFlasher", cg.Component)

# ESP-IDF framework only (uses the IDF UART driver + GPIO matrix directly).
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(UpdiFlasher),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
            web_server_base.WebServerBase
        ),
        cv.Required(CONF_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_UART_NUM, default=1): cv.int_range(min=0, max=4),
        cv.Optional(CONF_BAUD_RATE, default=115200): cv.int_range(min=9600, max=921600),
    }
).extend(cv.COMPONENT_SCHEMA)


# Number of UART peripherals per chip (UART0..UART<n-1>).
UART_COUNT = {
    "ESP32": 3,
    "ESP32S3": 3,
    "ESP32S2": 2,
    "ESP32C2": 2,
    "ESP32C3": 2,
    "ESP32C5": 2,
    "ESP32C6": 2,
    "ESP32H2": 2,
    "ESP32P4": 5,
}
LOGGER_UART = {"UART0": 0, "UART0_SWAP": 0, "UART1": 1, "UART2": 2}


def _final_validate(config):
    full = fv.full_config.get()
    variant = get_esp32_variant()
    count = UART_COUNT.get(variant, 2)
    n = config[CONF_UART_NUM]
    if n >= count:
        raise cv.Invalid(
            f"{variant} only has UART0..UART{count - 1}; uart_num {n} is not available",
            path=[CONF_UART_NUM],
        )
    logger = full.get("logger")
    if logger is not None and logger.get("baud_rate", 115200) != 0:
        used = LOGGER_UART.get(str(logger.get("hardware_uart", "UART0")).upper())
        if used == n:
            raise cv.Invalid(
                f"UART{n} is used by the logger; pick another uart_num or move the "
                "logger (e.g. hardware_uart: USB_SERIAL_JTAG / USB_CDC)",
                path=[CONF_UART_NUM],
            )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)
    cg.add(var.set_pin(config[CONF_PIN]))
    cg.add(var.set_uart_num(config[CONF_UART_NUM]))
    cg.add(var.set_baud_rate(config[CONF_BAUD_RATE]))
