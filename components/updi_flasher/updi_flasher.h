#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <string>
#include <vector>

namespace esphome {
namespace updi_flasher {

// ATtiny1616 (tinyAVR 1-series): 16 KB flash mapped at 0x8000, 64-byte pages.
static const size_t FLASH_SIZE = 16384;
static const size_t PAGE_SIZE = 64;
static const uint16_t FLASH_BASE = 0x8000;

class UpdiFlasher : public Component, public uart::UARTDevice {
 public:
  explicit UpdiFlasher(web_server_base::WebServerBase *base) : base_(base) {}
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // called by the web handler (async server task)
  void begin_upload();
  void feed_hex(const uint8_t *d, size_t n);
  void end_upload();
  const std::string &status() const { return status_; }
  bool busy() const { return busy_ || pending_; }

 protected:
  // --- Intel HEX ---
  bool parse_line_();
  // --- UPDI link layer ---
  void flush_rx_();
  bool rx_(uint8_t *buf, size_t n, uint32_t timeout_ms = 100);
  bool tx_(const uint8_t *d, size_t n);
  void send_break_();
  bool stcs_(uint8_t reg, uint8_t val);
  bool ldcs_(uint8_t reg, uint8_t *val);
  bool sts8_(uint16_t addr, uint8_t val);
  bool lds8_(uint16_t addr, uint8_t *val);
  bool st_ptr_(uint16_t addr);
  bool key_(const char *key8);
  // --- Programming sequence ---
  bool connect_();
  bool reset_();
  bool wait_sys_(uint8_t mask, bool set, uint32_t timeout_ms);
  bool nvm_cmd_(uint8_t cmd);
  bool wait_nvm_();
  bool write_page_(size_t off);
  bool verify_page_(size_t off);
  bool flash_();
  bool fail_(const char *msg);

  web_server_base::WebServerBase *base_;
  std::vector<uint8_t> image_;
  std::string line_, status_{"Idle"}, parse_error_;
  uint32_t ext_base_{0}, max_addr_{0}, baud_{115200};
  volatile bool busy_{false}, pending_{false};
  bool upload_ok_{false};
};

}  // namespace updi_flasher
}  // namespace esphome
