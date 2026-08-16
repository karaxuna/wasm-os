/**
 * JD9365 MIPI-DSI panel bring-up over the wasm-os esp_lcd bindings,
 * mirroring the vendor demo's esp_lcd_jd9365 + BSP flow: DSI PHY LDO power,
 * DSI bus (2 lanes @ 1500 Mbps), DBI command channel, hardware reset, vendor
 * init sequence, then the 60 MHz DPI pixel stream and backlight.
 */
#include "jd9365.h"

#include "jd9365_init_cmds.h"
#include "wasm_os.h"

/* JC8012P4A1C pins */
#define PIN_LCD_RST 27
#define PIN_LCD_BACKLIGHT 23

/* MIPI DPHY power (LDO VO3 feeds VDD_MIPI_DPHY on the P4) */
#define DSI_PHY_LDO_CHAN 3
#define DSI_PHY_LDO_MV 2500

#define DSI_LANES 2
#define DSI_LANE_MBPS 1500
#define DPI_CLOCK_MHZ 60

#define LCD_CMD_MADCTL 0x36
#define LCD_CMD_COLMOD 0x3A
#define COLMOD_RGB565 0x55

static uint32_t s_panel;

#define TRY(expr)                                                                                                      \
  do {                                                                                                                 \
    int32_t err_ = (expr);                                                                                             \
    if (err_ < 0) {                                                                                                    \
      return err_;                                                                                                     \
    }                                                                                                                  \
  } while (0)

static int32_t enable_dsi_phy_power(void) {
  uint32_t cfg = wos_ldo_channel_config_create();
  TRY(wos_ldo_channel_config_set_chan_id(cfg, DSI_PHY_LDO_CHAN));
  TRY(wos_ldo_channel_config_set_voltage_mv(cfg, DSI_PHY_LDO_MV));
  uint32_t channel = 0;
  TRY(wos_ldo_acquire_channel(cfg, &channel));
  return wos_ldo_channel_config_destroy(cfg);
}

static int32_t new_dsi_bus(uint32_t* bus_out) {
  uint32_t cfg = wos_dsi_bus_config_create();
  TRY(wos_dsi_bus_config_set_bus_id(cfg, 0));
  TRY(wos_dsi_bus_config_set_num_data_lanes(cfg, DSI_LANES));
  TRY(wos_dsi_bus_config_set_lane_bit_rate_mbps(cfg, DSI_LANE_MBPS));
  TRY(wos_lcd_new_dsi_bus(cfg, bus_out));
  return wos_dsi_bus_config_destroy(cfg);
}

static int32_t new_dbi_io(uint32_t bus, uint32_t* io_out) {
  uint32_t cfg = wos_dbi_io_config_create();
  TRY(wos_dbi_io_config_set_virtual_channel(cfg, 0));
  TRY(wos_dbi_io_config_set_lcd_cmd_bits(cfg, 8));
  TRY(wos_dbi_io_config_set_lcd_param_bits(cfg, 8));
  TRY(wos_lcd_new_panel_io_dbi(bus, cfg, io_out));
  return wos_dbi_io_config_destroy(cfg);
}

static int32_t hardware_reset(void) {
  TRY(wos_gpio_set_direction(PIN_LCD_RST, WOS_GPIO_MODE_OUTPUT));
  TRY(wos_gpio_set_level(PIN_LCD_RST, 1));
  wos_delay(5);
  TRY(wos_gpio_set_level(PIN_LCD_RST, 0));
  wos_delay(10);
  TRY(wos_gpio_set_level(PIN_LCD_RST, 1));
  wos_delay(120);
  return 0;
}

static int32_t send_init_sequence(uint32_t io) {
  /* Page user, orientation, and pixel format first, as the vendor driver
   * does; the table then carries the panel-specific tuning and ends with
   * sleep-out + display-on. */
  uint8_t page_user = 0x00;
  TRY(wos_lcd_panel_io_tx_param(io, 0xE0, &page_user, 1));
  uint8_t madctl = 0x00;
  TRY(wos_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, &madctl, 1));
  uint8_t colmod = COLMOD_RGB565;
  TRY(wos_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, &colmod, 1));
  /* DSI_INT0: 2-lane mode, sent before the password unlock exactly like the
   * vendor driver (the table repeats it after the unlock). */
  uint8_t lanes = 0x01;
  TRY(wos_lcd_panel_io_tx_param(io, 0x80, &lanes, 1));

  for (uint32_t i = 0; i < sizeof(JD9365_INIT_CMDS) / sizeof(JD9365_INIT_CMDS[0]); i++) {
    const struct jd9365_init_cmd* c = &JD9365_INIT_CMDS[i];
    TRY(wos_lcd_panel_io_tx_param(io, c->cmd, &c->data, 1));
    if (c->delay_ms) {
      wos_delay(c->delay_ms);
    }
  }
  return 0;
}

static int32_t new_dpi_panel(uint32_t bus, uint32_t* panel_out) {
  uint32_t cfg = wos_dpi_panel_config_create();
  TRY(wos_dpi_panel_config_set_virtual_channel(cfg, 0));
  TRY(wos_dpi_panel_config_set_dpi_clock_freq_mhz(cfg, DPI_CLOCK_MHZ));
  TRY(wos_dpi_panel_config_set_h_size(cfg, JD9365_HOR_RES));
  TRY(wos_dpi_panel_config_set_v_size(cfg, JD9365_VER_RES));
  TRY(wos_dpi_panel_config_set_hsync_pulse_width(cfg, 20));
  TRY(wos_dpi_panel_config_set_hsync_back_porch(cfg, 20));
  TRY(wos_dpi_panel_config_set_hsync_front_porch(cfg, 40));
  TRY(wos_dpi_panel_config_set_vsync_pulse_width(cfg, 4));
  TRY(wos_dpi_panel_config_set_vsync_back_porch(cfg, 8));
  TRY(wos_dpi_panel_config_set_vsync_front_porch(cfg, 20));
  TRY(wos_lcd_new_panel_dpi(bus, cfg, panel_out));
  return wos_dpi_panel_config_destroy(cfg);
}

static void log_lcd_id(uint32_t io) {
  uint8_t id[3] = {0};
  int32_t err = wos_lcd_panel_io_rx_param(io, 0x04, id, 3);
  char msg[] = "jd9365: LCD ID xx xx xx (err 0)";
  static const char hex[] = "0123456789abcdef";
  msg[15] = hex[id[0] >> 4];
  msg[16] = hex[id[0] & 0xf];
  msg[18] = hex[id[1] >> 4];
  msg[19] = hex[id[1] & 0xf];
  msg[21] = hex[id[2] >> 4];
  msg[22] = hex[id[2] & 0xf];
  msg[29] = err == 0 ? '0' : '!';
  wos_println(msg);
}

int jd9365_init(void) {
  TRY(enable_dsi_phy_power());

  uint32_t bus = 0;
  TRY(new_dsi_bus(&bus));
  wos_println("jd9365: dsi bus up");

  uint32_t io = 0;
  TRY(new_dbi_io(bus, &io));

  /* Mirror the vendor driver's order exactly: DPI panel object first, then
   * hardware reset + DCS init, then start the pixel stream, and finally
   * display-on while the stream is already running. */
  TRY(new_dpi_panel(bus, &s_panel));

  TRY(hardware_reset());
  log_lcd_id(io);
  TRY(send_init_sequence(io));
  wos_println("jd9365: init sequence sent");

  TRY(wos_lcd_panel_init(s_panel));
  wos_println("jd9365: dpi stream started");

  TRY(wos_lcd_panel_io_tx_param(io, 0x29, (const void*)0, 0)); /* DISPON */
  wos_println("jd9365: display on");

  TRY(wos_gpio_set_direction(PIN_LCD_BACKLIGHT, WOS_GPIO_MODE_OUTPUT));
  TRY(wos_gpio_set_level(PIN_LCD_BACKLIGHT, 1));
  return 0;
}

int jd9365_draw(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void* data, uint32_t len) {
  return wos_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, data, len);
}
