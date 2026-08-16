#include "ili9341.h"

#include "wasm_os.h"

/* CYD (ESP32-2432S028 / HW-458) display wiring: HSPI + DC/backlight GPIOs. */
#define PIN_MOSI 13
#define PIN_SCLK 14
#define PIN_CS 15
#define PIN_DC 2
#define PIN_BACKLIGHT 21

#define SPI_CLOCK_HZ (24 * 1000 * 1000)

/* Keep chunks under the SPI driver's default 4092-byte DMA transfer limit. */
#define MAX_CHUNK_BYTES 4000

#define CMD_SLEEP_OUT 0x11
#define CMD_DISPLAY_ON 0x29
#define CMD_COLUMN_SET 0x2A
#define CMD_PAGE_SET 0x2B
#define CMD_MEMORY_WRITE 0x2C
#define CMD_MADCTL 0x36
#define CMD_PIXEL_FORMAT 0x3A

static uint32_t s_device;
static uint32_t s_txn;

static void spi_write(const uint8_t* buf, uint32_t len) {
  wos_spi_transaction_set_length(s_txn, (int32_t)(len * 8));
  wos_spi_transaction_set_tx_buffer(s_txn, buf);
  wos_spi_device_transmit(s_device, s_txn);
}

static void write_command(uint8_t command) {
  wos_gpio_set_level(PIN_DC, 0);
  spi_write(&command, 1);
}

static void write_data(const uint8_t* data, uint32_t len) {
  wos_gpio_set_level(PIN_DC, 1);
  spi_write(data, len);
}

static void set_window(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  uint8_t column[4] = {(uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2};
  uint8_t page[4] = {(uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2};
  write_command(CMD_COLUMN_SET);
  write_data(column, 4);
  write_command(CMD_PAGE_SET);
  write_data(page, 4);
  write_command(CMD_MEMORY_WRITE);
}

void ili9341_init(void) {
  wos_gpio_set_direction(PIN_DC, WOS_GPIO_MODE_OUTPUT);
  wos_gpio_set_direction(PIN_BACKLIGHT, WOS_GPIO_MODE_OUTPUT);

  uint32_t bus = wos_spi_bus_config_create();
  wos_spi_bus_config_set_mosi_io_num(bus, PIN_MOSI);
  wos_spi_bus_config_set_sclk_io_num(bus, PIN_SCLK);
  wos_spi_bus_initialize(WOS_SPI2_HOST, bus, WOS_SPI_DMA_AUTO);
  wos_spi_bus_config_destroy(bus);

  uint32_t dev_cfg = wos_spi_device_config_create();
  wos_spi_device_config_set_spics_io_num(dev_cfg, PIN_CS);
  wos_spi_device_config_set_clock_speed_hz(dev_cfg, SPI_CLOCK_HZ);
  wos_spi_device_config_set_mode(dev_cfg, 0);
  wos_spi_bus_add_device(WOS_SPI2_HOST, dev_cfg, &s_device);
  wos_spi_device_config_destroy(dev_cfg);

  s_txn = wos_spi_transaction_create();

  write_command(CMD_SLEEP_OUT);
  wos_delay(120);

  /* 16-bit pixels; portrait 240x320, BGR panel. */
  write_command(CMD_PIXEL_FORMAT);
  write_data((const uint8_t[]){0x55}, 1);
  write_command(CMD_MADCTL);
  write_data((const uint8_t[]){0x48}, 1);

  write_command(CMD_DISPLAY_ON);
  wos_delay(20);

  wos_gpio_set_level(PIN_BACKLIGHT, 1);
}

void ili9341_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t* pixels, uint32_t byte_len) {
  set_window(x1, y1, x2, y2);
  wos_gpio_set_level(PIN_DC, 1);

  for (uint32_t offset = 0; offset < byte_len; offset += MAX_CHUNK_BYTES) {
    uint32_t chunk = byte_len - offset;
    if (chunk > MAX_CHUNK_BYTES) {
      chunk = MAX_CHUNK_BYTES;
    }
    spi_write(pixels + offset, chunk);
  }
}

void ili9341_fill(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) {
  static uint8_t line[ILI9341_HOR_RES * 2];
  int32_t width = x2 - x1 + 1;

  for (int32_t i = 0; i < width; i++) {
    line[i * 2] = (uint8_t)(color >> 8); /* panel expects big-endian */
    line[i * 2 + 1] = (uint8_t)color;
  }

  set_window(x1, y1, x2, y2);
  wos_gpio_set_level(PIN_DC, 1);
  for (int32_t row = y1; row <= y2; row++) {
    spi_write(line, (uint32_t)width * 2);
  }
}
