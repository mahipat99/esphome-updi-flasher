import esphome.codegen as cg
import esphome.config_validation as cv
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
        cv.Required(CONF_PIN): cv.int_range(min=0, max=48),
        cv.Optional(CONF_UART_NUM, default=1): cv.int_range(min=1, max=2),
        cv.Optional(CONF_BAUD_RATE, default=115200): cv.int_range(min=9600, max=921600),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)
    cg.add(var.set_pin(config[CONF_PIN]))
    cg.add(var.set_uart_num(config[CONF_UART_NUM]))
    cg.add(var.set_baud_rate(config[CONF_BAUD_RATE]))
