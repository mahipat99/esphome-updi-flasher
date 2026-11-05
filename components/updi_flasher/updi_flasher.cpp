#include "updi_flasher.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace updi_flasher {

static const char *const TAG = "updi_flasher";

// ---- UPDI constants ----
static const uint8_t SYNC = 0x55, ACK = 0x40;
static const uint8_t CS_STATUSA = 0x00, CS_CTRLA = 0x02, CS_CTRLB = 0x03;
static const uint8_t CS_KEY_STATUS = 0x07, CS_RESET_REQ = 0x08, CS_SYS_STATUS = 0x0B;
static const uint8_t KEY_STAT_CHIPERASE = 0x08, KEY_STAT_NVMPROG = 0x10;
static const uint8_t SYS_LOCKSTATUS = 0x01, SYS_NVMPROG = 0x08;
static const uint16_t NVM_CTRLA = 0x1000, NVM_STATUS = 0x1002;
static const uint8_t NVM_CMD_WP = 0x01, NVM_CMD_PBC = 0x04;

// ======================= Web handler =======================
class UpdiWebHandler : public AsyncWebHandler {
 public:
  explicit UpdiWebHandler(UpdiFlasher *p) : p_(p) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char buf[AsyncWebServerRequest::URL_BUF_SIZE];
    return std::string(request->url_to(buf)) == "/updi";
  }
  bool isRequestHandlerTrivial() const override { return false; }

  void handleRequest(AsyncWebServerRequest *request) override {
    std::string html =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>";
    if (request->method() == HTTP_POST) html += "<meta http-equiv=refresh content='3;url=/updi'>";
    html += "<title>UPDI flasher</title><h3>ATtiny UPDI flasher</h3><p>Status: <b>" + p_->status() +
            "</b></p><form method=POST action=/updi enctype=multipart/form-data>"
            "<input type=file name=fw accept='.hex' required> "
            "<button>Upload &amp; flash</button></form>";
    request->send(200, "text/html", html.c_str());
  }

  void handleUpload(AsyncWebServerRequest *request, const std::string &filename, size_t index, uint8_t *data,
                    size_t len, bool final) override {
    if (index == 0) p_->begin_upload();
    if (len) p_->feed_hex(data, len);
    if (final) p_->end_upload();
  }

 protected:
  UpdiFlasher *p_;
};

// ======================= Component =======================
void UpdiFlasher::setup() {
  this->baud_ = this->parent_->get_baud_rate();
  this->base_->add_handler(new UpdiWebHandler(this));
}

void UpdiFlasher::dump_config() {
  ESP_LOGCONFIG(TAG, "UPDI flasher: open http://<device>/updi to upload a .hex");
  ESP_LOGCONFIG(TAG, "  Target: tinyAVR 1-series, %u B flash, %u B pages", (unsigned) FLASH_SIZE,
                (unsigned) PAGE_SIZE);
}

void UpdiFlasher::loop() {
  if (!this->pending_ || this->busy_) return;
  this->busy_ = true;
  this->pending_ = false;
  bool ok = this->flash_();
  if (ok) this->status_ = "Done: flashed and verified " + to_string(this->max_addr_) + " bytes";
  ESP_LOGI(TAG, "%s", this->status_.c_str());
  this->busy_ = false;
}

// ======================= Intel HEX =======================
void UpdiFlasher::begin_upload() {
  if (this->busy()) {
    this->upload_ok_ = false;
    return;
  }
  this->image_.assign(FLASH_SIZE, 0xFF);
  this->line_.clear();
  this->parse_error_.clear();
  this->ext_base_ = 0;
  this->max_addr_ = 0;
  this->upload_ok_ = true;
  this->status_ = "Receiving hex...";
}

void UpdiFlasher::feed_hex(const uint8_t *d, size_t n) {
  if (!this->upload_ok_) return;
  for (size_t i = 0; i < n; i++) {
    char c = (char) d[i];
    if (c == '\n') {
      if (!this->parse_line_()) {
        this->upload_ok_ = false;
        return;
      }
      this->line_.clear();
    } else if (c != '\r') {
      if (this->line_.size() < 600) this->line_ += c;
    }
  }
}

void UpdiFlasher::end_upload() {
  if (!this->upload_ok_) {
    if (!this->parse_error_.empty()) this->status_ = "Hex error: " + this->parse_error_;
    else if (!this->busy_ && !this->pending_) this->status_ = "Upload rejected";
    return;
  }
  if (!this->parse_line_()) {
    this->status_ = "Hex error: " + this->parse_error_;
    return;
  }
  if (this->max_addr_ == 0) {
    this->status_ = "Hex error: no data records";
    return;
  }
  this->status_ = "Queued, flashing...";
  this->pending_ = true;
}

static int hexbyte(const std::string &s, size_t pos) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if (pos + 1 >= s.size()) return -1;
  int a = nib(s[pos]), b = nib(s[pos + 1]);
  return (a < 0 || b < 0) ? -1 : (a << 4) | b;
}

bool UpdiFlasher::parse_line_() {
  const std::string &s = this->line_;
  if (s.empty()) return true;
  if (s[0] != ':') {
    this->parse_error_ = "line does not start with ':'";
    return false;
  }
  int len = hexbyte(s, 1), ah = hexbyte(s, 3), al = hexbyte(s, 5), type = hexbyte(s, 7);
  if (len < 0 || ah < 0 || al < 0 || type < 0 || s.size() < (size_t) (11 + 2 * len)) {
    this->parse_error_ = "malformed record";
    return false;
  }
  uint8_t sum = len + ah + al + type;
  std::vector<uint8_t> data(len);
  for (int i = 0; i < len; i++) {
    int b = hexbyte(s, 9 + 2 * i);
    if (b < 0) {
      this->parse_error_ = "bad data byte";
      return false;
    }
    data[i] = b;
    sum += b;
  }
  int cs = hexbyte(s, 9 + 2 * len);
  if (cs < 0 || (uint8_t) (sum + cs) != 0) {
    this->parse_error_ = "checksum mismatch";
    return false;
  }
  uint32_t addr = (ah << 8) | al;
  if (type == 0x00) {
    for (int i = 0; i < len; i++) {
      uint32_t a = this->ext_base_ + addr + i;
      if (a >= FLASH_SIZE) {
        this->parse_error_ = "address beyond 16 KB flash";
        return false;
      }
      this->image_[a] = data[i];
      this->max_addr_ = std::max(this->max_addr_, a + 1);
    }
  } else if (type == 0x02 && len == 2) {
    this->ext_base_ = ((data[0] << 8) | data[1]) << 4;
  } else if (type == 0x04 && len == 2) {
    this->ext_base_ = ((uint32_t) ((data[0] << 8) | data[1])) << 16;
  }
  return true;
}

// ======================= UPDI link layer =======================
void UpdiFlasher::flush_rx_() {
  uint8_t b;
  while (this->available()) this->read_byte(&b);
}

bool UpdiFlasher::rx_(uint8_t *buf, size_t n, uint32_t timeout_ms) {
  uint32_t start = millis();
  size_t got = 0;
  while (got < n) {
    if (this->available()) {
      this->read_byte(&buf[got++]);
      continue;
    }
    if (millis() - start > timeout_ms) return false;
    delayMicroseconds(20);
  }
  return true;
}

// Send bytes, then swallow the echo (RX is wired to the same line as TX).
bool UpdiFlasher::tx_(const uint8_t *d, size_t n) {
  this->write_array(d, n);
  this->flush();
  uint8_t e;
  for (size_t i = 0; i < n; i++)
    if (!this->rx_(&e, 1, 50)) return false;
  return true;
}

// Break = 0x00 at 300 baud (8E2) -> ~33 ms of low.
// NOTE: set_baud_rate()/load_settings() are the ESPHome-version-sensitive calls.
void UpdiFlasher::send_break_() {
  this->parent_->set_baud_rate(300);
  this->parent_->load_settings(false);
  this->flush_rx_();
  uint8_t z = 0;
  this->write_array(&z, 1);
  this->flush();
  delay(15);
  this->parent_->set_baud_rate(this->baud_);
  this->parent_->load_settings(false);
  delay(2);
  this->flush_rx_();
}

bool UpdiFlasher::stcs_(uint8_t reg, uint8_t val) {
  uint8_t b[] = {SYNC, (uint8_t) (0xC0 | reg), val};
  return this->tx_(b, 3);
}

bool UpdiFlasher::ldcs_(uint8_t reg, uint8_t *val) {
  uint8_t b[] = {SYNC, (uint8_t) (0x80 | reg)};
  return this->tx_(b, 2) && this->rx_(val, 1);
}

bool UpdiFlasher::sts8_(uint16_t a, uint8_t v) {
  uint8_t h[] = {SYNC, 0x44, (uint8_t) a, (uint8_t) (a >> 8)};
  uint8_t r;
  if (!this->tx_(h, 4) || !this->rx_(&r, 1) || r != ACK) return false;
  if (!this->tx_(&v, 1) || !this->rx_(&r, 1) || r != ACK) return false;
  return true;
}

bool UpdiFlasher::lds8_(uint16_t a, uint8_t *v) {
  uint8_t h[] = {SYNC, 0x04, (uint8_t) a, (uint8_t) (a >> 8)};
  return this->tx_(h, 4) && this->rx_(v, 1);
}

bool UpdiFlasher::st_ptr_(uint16_t a) {
  uint8_t h[] = {SYNC, 0x69, (uint8_t) a, (uint8_t) (a >> 8)};
  uint8_t r;
  return this->tx_(h, 4) && this->rx_(&r, 1) && r == ACK;
}

bool UpdiFlasher::key_(const char *k) {
  uint8_t h[] = {SYNC, 0xE0};
  if (!this->tx_(h, 2)) return false;
  uint8_t rev[8];
  for (int i = 0; i < 8; i++) rev[i] = (uint8_t) k[7 - i];
  return this->tx_(rev, 8);
}

// ======================= Programming sequence =======================
bool UpdiFlasher::fail_(const char *msg) {
  this->status_ = std::string("Failed: ") + msg;
  return false;
}

bool UpdiFlasher::connect_() {
  for (int attempt = 0; attempt < 3; attempt++) {
    this->send_break_();
    uint8_t s = 0;
    if (this->stcs_(CS_CTRLB, 0x08) &&   // CCDETDIS
        this->stcs_(CS_CTRLA, 0x80) &&   // IBDLY
        this->ldcs_(CS_STATUSA, &s) && s != 0)
      return true;
    App.feed_wdt();
  }
  return false;
}

bool UpdiFlasher::reset_() {
  if (!this->stcs_(CS_RESET_REQ, 0x59)) return false;
  if (!this->stcs_(CS_RESET_REQ, 0x00)) return false;
  delay(5);
  return true;
}

bool UpdiFlasher::wait_sys_(uint8_t mask, bool set, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    uint8_t v;
    if (this->ldcs_(CS_SYS_STATUS, &v) && (((v & mask) != 0) == set)) return true;
    App.feed_wdt();
    delay(2);
  }
  return false;
}

bool UpdiFlasher::wait_nvm_() {
  uint32_t start = millis();
  while (millis() - start < 200) {
    uint8_t v;
    if (!this->lds8_(NVM_STATUS, &v)) return false;
    if (v & 0x04) return false;          // WRERROR
    if ((v & 0x03) == 0) return true;    // FBUSY | EEBUSY clear
    delay(1);
  }
  return false;
}

bool UpdiFlasher::nvm_cmd_(uint8_t cmd) { return this->sts8_(NVM_CTRLA, cmd); }

bool UpdiFlasher::write_page_(size_t off) {
  if (!this->wait_nvm_() || !this->nvm_cmd_(NVM_CMD_PBC) || !this->wait_nvm_()) return false;
  if (!this->st_ptr_(FLASH_BASE + off)) return false;
  uint8_t rep[] = {SYNC, 0xA0, (uint8_t) (PAGE_SIZE / 2 - 1)};
  if (!this->tx_(rep, 3)) return false;
  const uint8_t *p = &this->image_[off];
  uint8_t h[] = {SYNC, 0x65, p[0], p[1]};  // ST *ptr++ (word)
  uint8_t r;
  if (!this->tx_(h, 4) || !this->rx_(&r, 1) || r != ACK) return false;
  for (size_t i = 2; i < PAGE_SIZE; i += 2) {
    if (!this->tx_(p + i, 2) || !this->rx_(&r, 1) || r != ACK) return false;
  }
  return this->nvm_cmd_(NVM_CMD_WP) && this->wait_nvm_();
}

bool UpdiFlasher::verify_page_(size_t off) {
  if (!this->st_ptr_(FLASH_BASE + off)) return false;
  uint8_t rep[] = {SYNC, 0xA0, (uint8_t) (PAGE_SIZE - 1)};
  if (!this->tx_(rep, 3)) return false;
  uint8_t ld[] = {SYNC, 0x24};  // LD *ptr++ (byte)
  if (!this->tx_(ld, 2)) return false;
  uint8_t buf[PAGE_SIZE];
  if (!this->rx_(buf, PAGE_SIZE, 200)) return false;
  return memcmp(buf, &this->image_[off], PAGE_SIZE) == 0;
}

bool UpdiFlasher::flash_() {
  this->status_ = "Connecting...";
  if (!this->connect_()) return this->fail_("no UPDI response (check wiring, resistor, power)");

  // 1) Chip erase via key
  this->status_ = "Erasing...";
  uint8_t v;
  if (!this->key_("NVMErase") || !this->ldcs_(CS_KEY_STATUS, &v) || !(v & KEY_STAT_CHIPERASE))
    return this->fail_("chip-erase key rejected");
  if (!this->reset_() || !this->wait_sys_(SYS_LOCKSTATUS, false, 1500))
    return this->fail_("chip erase timed out");

  // 2) Enter programming mode
  if (!this->key_("NVMProg ") || !this->ldcs_(CS_KEY_STATUS, &v) || !(v & KEY_STAT_NVMPROG))
    return this->fail_("NVMProg key rejected");
  if (!this->reset_() || !this->wait_sys_(SYS_NVMPROG, true, 1500))
    return this->fail_("could not enter programming mode");

  // 3) Write pages
  size_t end = ((this->max_addr_ + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
  for (size_t off = 0; off < end; off += PAGE_SIZE) {
    this->status_ = "Writing " + to_string(off) + "/" + to_string(end);
    bool blank = true;
    for (size_t i = 0; i < PAGE_SIZE && blank; i++) blank = this->image_[off + i] == 0xFF;
    if (!blank && !this->write_page_(off)) return this->fail_("page write error");
    App.feed_wdt();
  }

  // 4) Verify
  for (size_t off = 0; off < end; off += PAGE_SIZE) {
    this->status_ = "Verifying " + to_string(off) + "/" + to_string(end);
    if (!this->verify_page_(off)) return this->fail_("verify mismatch");
    App.feed_wdt();
  }

  // 5) Leave prog mode, reset target, disable UPDI until next power-up
  this->reset_();
  this->stcs_(CS_CTRLB, 0x04 | 0x08);
  return true;
}

}  // namespace updi_flasher
}  // namespace esphome
