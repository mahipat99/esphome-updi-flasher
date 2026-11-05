import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["web_server_base"]
CODEOWNERS = []

updi_ns = cg.esphome_ns.namespace("updi_flasher")
UpdiFlasher = updi_ns.class_("UpdiFlasher", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(UpdiFlasher),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
                web_server_base.WebServerBase
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "updi_flasher",
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="EVEN",
    stop_bits=2,
)


async def to_code(config):
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
