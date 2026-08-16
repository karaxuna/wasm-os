#include "xpt2046.h"

#include "ili9341.h"
#include "wasm_os.h"

/* CYD touch wiring: its own SPI pins + active-low pen IRQ. */
#define PIN_MOSI 32
#define PIN_MISO 39
#define PIN_SCLK 25
#define PIN_CS 33
#define PIN_IRQ 36

#define SPI_CLOCK_HZ (2 * 1000 * 1000)

/* 12-bit differential conversions. */
#define CMD_READ_X 0xD0
#define CMD_READ_Y 0x90

/* Raw ADC range observed on CYD panels; the app's button is large enough
 * to tolerate the usual unit-to-unit variance. */
#define RAW_MIN 200
#define RAW_MAX 3800

static uint32_t s_device;
static uint32_t s_txn;

static uint16_t read_channel(uint8_t command) {
  uint8_t tx[3] = {command, 0, 0};
  uint8_t rx[3] = {0, 0, 0};

  wos_spi_transaction_set_length(s_txn, 24);
  wos_spi_transaction_set_tx_buffer(s_txn, tx);
  wos_spi_transaction_set_rx_buffer(s_txn, rx);
  wos_spi_device_transmit(s_device, s_txn);

  return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 3); /* 12-bit result */
}

/* Median of three samples suppresses the odd wild ADC reading. */
static uint16_t read_channel_filtered(uint8_t command) {
  uint16_t a = read_channel(command);
  uint16_t b = read_channel(command);
  uint16_t c = read_channel(command);

  if (a > b) {
    uint16_t t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    b = c;
  }
  return a > b ? a : b;
}

static int16_t scale(uint16_t raw, int16_t screen_max) {
  if (raw <= RAW_MIN) {
    return 0;
  }
  if (raw >= RAW_MAX) {
    return (int16_t)(screen_max - 1);
  }
  return (int16_t)(((int32_t)(raw - RAW_MIN) * screen_max) / (RAW_MAX - RAW_MIN));
}

void xpt2046_init(void) {
  wos_gpio_set_direction(PIN_IRQ, WOS_GPIO_MODE_INPUT);

  uint32_t bus = wos_spi_bus_config_create();
  wos_spi_bus_config_set_mosi_io_num(bus, PIN_MOSI);
  wos_spi_bus_config_set_miso_io_num(bus, PIN_MISO);
  wos_spi_bus_config_set_sclk_io_num(bus, PIN_SCLK);
  wos_spi_bus_initialize(WOS_SPI3_HOST, bus, WOS_SPI_DMA_AUTO);
  wos_spi_bus_config_destroy(bus);

  uint32_t dev_cfg = wos_spi_device_config_create();
  wos_spi_device_config_set_spics_io_num(dev_cfg, PIN_CS);
  wos_spi_device_config_set_clock_speed_hz(dev_cfg, SPI_CLOCK_HZ);
  wos_spi_device_config_set_mode(dev_cfg, 0);
  wos_spi_bus_add_device(WOS_SPI3_HOST, dev_cfg, &s_device);
  wos_spi_device_config_destroy(dev_cfg);

  s_txn = wos_spi_transaction_create();
}

bool xpt2046_read(int16_t* x, int16_t* y) {
  /* Pen IRQ is low while the panel is pressed. */
  if (wos_gpio_get_level(PIN_IRQ) != 0) {
    return false;
  }

  uint16_t raw_x = read_channel_filtered(CMD_READ_X);
  uint16_t raw_y = read_channel_filtered(CMD_READ_Y);

  *x = scale(raw_x, ILI9341_HOR_RES);
  *y = scale(raw_y, ILI9341_VER_RES);
  return true;
}
