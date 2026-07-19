/**
 * GSL3680 touch controller over the wasm-os i2c_master bindings, following
 * the vendor demo's esp_lcd_touch_gsl3680 flow: GPIO reset with the INT pin
 * selecting the I2C address, firmware upload (the chip has no flash), chip
 * startup, then 24-byte point reports from register 0x80 filtered through
 * Silead's gsl_alg_id algorithm.
 *
 * The vendor driver talks through an esp_lcd_panel_io I2C channel with the
 * control phase disabled, so on the wire a register write is
 * [reg, data...] and a register read is [reg] + repeated-start read.
 */
#include "gsl3680.h"

#include "gsl_fw.h"
#include "gsl_point_id.h"
#include "wasm_os.h"

/* JC8012P4A1C pins */
#define PIN_TP_SDA 7
#define PIN_TP_SCL 8
#define PIN_TP_RST 22
#define PIN_TP_INT 21

#define I2C_PORT 0
#define GSL3680_ADDR 0x40
#define I2C_SPEED_HZ 400000
#define I2C_TIMEOUT_MS 1000

#define REG_TOUCH_DATA 0x80
#define REG_PAGE 0xf0

static uint32_t s_device;

#define TRY(expr)                                                                                                      \
  do {                                                                                                                 \
    int32_t err_ = (expr);                                                                                             \
    if (err_ < 0) {                                                                                                    \
      return err_;                                                                                                     \
    }                                                                                                                  \
  } while (0)

static int32_t reg_write(uint8_t reg, const uint8_t* data, uint32_t len) {
  uint8_t frame[5];
  frame[0] = reg;
  for (uint32_t i = 0; i < len && i < 4; i++) {
    frame[1 + i] = data[i];
  }
  return wos_i2c_master_transmit(s_device, frame, 1 + len, I2C_TIMEOUT_MS);
}

static int32_t reg_write1(uint8_t reg, uint8_t value) {
  return reg_write(reg, &value, 1);
}

static int32_t reg_write4(uint8_t reg, uint32_t value) {
  uint8_t data[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
  return reg_write(reg, data, 4);
}

static int32_t reg_read(uint8_t reg, uint8_t* data, uint32_t len) {
  return wos_i2c_master_transmit_receive(s_device, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

static int32_t open_bus(void) {
  uint32_t bus_cfg = wos_i2c_master_bus_config_create();
  TRY(wos_i2c_master_bus_config_set_i2c_port(bus_cfg, I2C_PORT));
  TRY(wos_i2c_master_bus_config_set_sda_io_num(bus_cfg, PIN_TP_SDA));
  TRY(wos_i2c_master_bus_config_set_scl_io_num(bus_cfg, PIN_TP_SCL));
  TRY(wos_i2c_master_bus_config_set_enable_internal_pullup(bus_cfg, 1));
  TRY(wos_i2c_new_master_bus(bus_cfg));
  TRY(wos_i2c_master_bus_config_destroy(bus_cfg));

  uint32_t dev_cfg = wos_i2c_device_config_create();
  TRY(wos_i2c_device_config_set_device_address(dev_cfg, GSL3680_ADDR));
  TRY(wos_i2c_device_config_set_scl_speed_hz(dev_cfg, I2C_SPEED_HZ));
  TRY(wos_i2c_master_bus_add_device(I2C_PORT, dev_cfg, &s_device));
  return wos_i2c_device_config_destroy(dev_cfg);
}

/* Power-on reset; INT held low during reset selects I2C address 0x40. */
static int32_t power_on_reset(void) {
  TRY(wos_gpio_set_direction(PIN_TP_RST, WOS_GPIO_MODE_OUTPUT));
  TRY(wos_gpio_set_direction(PIN_TP_INT, WOS_GPIO_MODE_OUTPUT));
  TRY(wos_gpio_set_level(PIN_TP_RST, 0));
  TRY(wos_gpio_set_level(PIN_TP_INT, 0));
  wos_delay(10);
  TRY(wos_gpio_set_level(PIN_TP_INT, 0));
  wos_delay(1);
  TRY(wos_gpio_set_level(PIN_TP_RST, 1));
  wos_delay(60);
  TRY(wos_gpio_set_direction(PIN_TP_INT, WOS_GPIO_MODE_INPUT));
  return 0;
}

static int32_t chip_reset(void) {
  TRY(wos_gpio_set_level(PIN_TP_RST, 0));
  wos_delay(20);
  TRY(wos_gpio_set_level(PIN_TP_RST, 1));
  wos_delay(20);
  TRY(reg_write1(0xe4, 0x04));
  wos_delay(10);
  TRY(reg_write4(0xbc, 0));
  wos_delay(10);
  return 0;
}

static int32_t clear_reg(void) {
  TRY(reg_write1(0xe0, 0x88));
  wos_delay(20);
  TRY(reg_write1(0x88, 0x01));
  wos_delay(5);
  TRY(reg_write1(0xe4, 0x04));
  wos_delay(5);
  TRY(reg_write1(0xe0, 0x00));
  wos_delay(20);
  return 0;
}

static int32_t load_fw(void) {
  for (uint32_t i = 0; i < sizeof(GSL_FW) / sizeof(GSL_FW[0]); i++) {
    const struct gsl_fw_entry* e = &GSL_FW[i];
    if (e->offset == REG_PAGE) {
      TRY(reg_write1(REG_PAGE, (uint8_t)e->val));
    } else {
      TRY(reg_write4(e->offset, e->val));
    }
  }
  return 0;
}

static int32_t startup_chip(void) {
  TRY(reg_write1(0xe0, 0x00));
  wos_delay(10);
  gsl_DataInit(gsl_config_data_id);
  return 0;
}

int gsl3680_init(void) {
  TRY(power_on_reset());
  TRY(open_bus());

  TRY(clear_reg());
  TRY(chip_reset());
  wos_println("gsl3680: loading firmware...");
  TRY(load_fw());
  TRY(startup_chip());
  TRY(chip_reset());
  TRY(startup_chip());

  /* The chip reports 0x5a5a5a5a at 0xb0 once the firmware is running. */
  wos_delay(30);
  uint8_t status[4] = {0};
  TRY(reg_read(0xb0, status, 4));
  if (status[0] != 0x5a || status[1] != 0x5a || status[2] != 0x5a || status[3] != 0x5a) {
    wos_println("gsl3680: firmware status check failed");
    return -4; /* WOS_ERR_INTERNAL */
  }
  wos_println("gsl3680: ready");
  return 0;
}

int gsl3680_read(int16_t* x, int16_t* y) {
  uint8_t data[24];
  if (reg_read(REG_TOUCH_DATA, data, sizeof(data)) < 0) {
    return 0;
  }

  struct gsl_touch_info cinfo = {0};
  int fingers = data[0];
  if (fingers > 5) {
    fingers = 5; /* 24-byte report holds at most 5 points */
  }
  cinfo.finger_num = fingers;
  for (int j = 0; j < fingers; j++) {
    const uint8_t* p = &data[(j + 1) * 4];
    cinfo.x[j] = ((p[3] & 0x0f) << 8) | p[2];
    cinfo.y[j] = (p[1] << 8) | p[0];
    cinfo.id[j] = (p[3] & 0xf0) >> 4;
  }

  gsl_alg_id_main(&cinfo);

  unsigned int mask = gsl_mask_tiaoping();
  if (mask > 0 && mask < 0xffffffff) {
    reg_write4(REG_PAGE, 0xa);
    reg_write4(0x8, mask);
  }

  if (cinfo.finger_num < 1) {
    return 0;
  }
  *x = (int16_t)cinfo.x[0];
  *y = (int16_t)cinfo.y[0];
  return 1;
}
