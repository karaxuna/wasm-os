/**
 * wasm-os host imports used by this app. Signatures follow the ABI
 * documented in the wasm-os-core/main/bindings .wit files: handles are opaque u32
 * (0 = invalid), status returns are 0 on success / negative on error.
 */
#pragma once

#include <stdint.h>

#define WOS_IMPORT(module, name) __attribute__((import_module(module), import_name(name)))

/* env */
WOS_IMPORT("env", "println") void wos_println(const char* message);
WOS_IMPORT("env", "delay") void wos_delay(int32_t milliseconds);
WOS_IMPORT("env", "millis") uint32_t wos_millis(void);
/* snprintf-style: returns the value length, copies value + NUL when it fits;
 * -5 when the variable is not set. */
WOS_IMPORT("env", "getenv") int32_t wos_getenv(const char* name, char* buf, uint32_t cap);

/* gpio */
#define WOS_GPIO_MODE_INPUT 1
#define WOS_GPIO_MODE_OUTPUT 2

WOS_IMPORT("gpio", "gpio_set_direction") int32_t wos_gpio_set_direction(int32_t pin, int32_t mode);
WOS_IMPORT("gpio", "gpio_set_level") int32_t wos_gpio_set_level(int32_t pin, uint32_t level);
WOS_IMPORT("gpio", "gpio_get_level") int32_t wos_gpio_get_level(int32_t pin);

/* esp_lcd (MIPI-DSI, ESP32-P4 only) */
WOS_IMPORT("esp_lcd", "ldo_channel_config_create") uint32_t wos_ldo_channel_config_create(void);
WOS_IMPORT("esp_lcd", "ldo_channel_config_set_chan_id") int32_t wos_ldo_channel_config_set_chan_id(uint32_t cfg, int32_t chan_id);
WOS_IMPORT("esp_lcd", "ldo_channel_config_set_voltage_mv") int32_t wos_ldo_channel_config_set_voltage_mv(uint32_t cfg, int32_t mv);
WOS_IMPORT("esp_lcd", "ldo_channel_config_destroy") int32_t wos_ldo_channel_config_destroy(uint32_t cfg);
WOS_IMPORT("esp_lcd", "ldo_acquire_channel") int32_t wos_ldo_acquire_channel(uint32_t cfg, uint32_t* out);

WOS_IMPORT("esp_lcd", "dsi_bus_config_create") uint32_t wos_dsi_bus_config_create(void);
WOS_IMPORT("esp_lcd", "dsi_bus_config_set_bus_id") int32_t wos_dsi_bus_config_set_bus_id(uint32_t cfg, int32_t bus_id);
WOS_IMPORT("esp_lcd", "dsi_bus_config_set_num_data_lanes") int32_t wos_dsi_bus_config_set_num_data_lanes(uint32_t cfg, int32_t lanes);
WOS_IMPORT("esp_lcd", "dsi_bus_config_set_lane_bit_rate_mbps") int32_t wos_dsi_bus_config_set_lane_bit_rate_mbps(uint32_t cfg, int32_t mbps);
WOS_IMPORT("esp_lcd", "dsi_bus_config_destroy") int32_t wos_dsi_bus_config_destroy(uint32_t cfg);
WOS_IMPORT("esp_lcd", "lcd_new_dsi_bus") int32_t wos_lcd_new_dsi_bus(uint32_t cfg, uint32_t* out);

WOS_IMPORT("esp_lcd", "dbi_io_config_create") uint32_t wos_dbi_io_config_create(void);
WOS_IMPORT("esp_lcd", "dbi_io_config_set_virtual_channel") int32_t wos_dbi_io_config_set_virtual_channel(uint32_t cfg, int32_t ch);
WOS_IMPORT("esp_lcd", "dbi_io_config_set_lcd_cmd_bits") int32_t wos_dbi_io_config_set_lcd_cmd_bits(uint32_t cfg, int32_t bits);
WOS_IMPORT("esp_lcd", "dbi_io_config_set_lcd_param_bits") int32_t wos_dbi_io_config_set_lcd_param_bits(uint32_t cfg, int32_t bits);
WOS_IMPORT("esp_lcd", "dbi_io_config_destroy") int32_t wos_dbi_io_config_destroy(uint32_t cfg);
WOS_IMPORT("esp_lcd", "lcd_new_panel_io_dbi") int32_t wos_lcd_new_panel_io_dbi(uint32_t bus, uint32_t cfg, uint32_t* out);
WOS_IMPORT("esp_lcd", "lcd_panel_io_tx_param") int32_t wos_lcd_panel_io_tx_param(uint32_t io, int32_t cmd, const void* params, uint32_t params_len);
WOS_IMPORT("esp_lcd", "lcd_panel_io_rx_param") int32_t wos_lcd_panel_io_rx_param(uint32_t io, int32_t cmd, void* params, uint32_t params_len);

WOS_IMPORT("esp_lcd", "dpi_panel_config_create") uint32_t wos_dpi_panel_config_create(void);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_virtual_channel") int32_t wos_dpi_panel_config_set_virtual_channel(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_dpi_clock_freq_mhz") int32_t wos_dpi_panel_config_set_dpi_clock_freq_mhz(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_h_size") int32_t wos_dpi_panel_config_set_h_size(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_v_size") int32_t wos_dpi_panel_config_set_v_size(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_hsync_pulse_width") int32_t wos_dpi_panel_config_set_hsync_pulse_width(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_hsync_back_porch") int32_t wos_dpi_panel_config_set_hsync_back_porch(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_hsync_front_porch") int32_t wos_dpi_panel_config_set_hsync_front_porch(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_vsync_pulse_width") int32_t wos_dpi_panel_config_set_vsync_pulse_width(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_vsync_back_porch") int32_t wos_dpi_panel_config_set_vsync_back_porch(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_set_vsync_front_porch") int32_t wos_dpi_panel_config_set_vsync_front_porch(uint32_t cfg, uint32_t v);
WOS_IMPORT("esp_lcd", "dpi_panel_config_destroy") int32_t wos_dpi_panel_config_destroy(uint32_t cfg);
WOS_IMPORT("esp_lcd", "lcd_new_panel_dpi") int32_t wos_lcd_new_panel_dpi(uint32_t bus, uint32_t cfg, uint32_t* out);

WOS_IMPORT("esp_lcd", "lcd_panel_init") int32_t wos_lcd_panel_init(uint32_t panel);
WOS_IMPORT("esp_lcd", "lcd_panel_draw_bitmap") int32_t wos_lcd_panel_draw_bitmap(uint32_t panel, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void* data, uint32_t data_len);

/* wifi (credentials come from the app, e.g. via wos_getenv) */
WOS_IMPORT("wifi", "wifi_connect") int32_t wos_wifi_connect(const char* ssid, const char* pass);
WOS_IMPORT("wifi", "wifi_disconnect") int32_t wos_wifi_disconnect(void);
WOS_IMPORT("wifi", "wifi_state") int32_t wos_wifi_state(void);
WOS_IMPORT("wifi", "wifi_wait") int32_t wos_wifi_wait(int32_t timeout_ms);
WOS_IMPORT("wifi", "wifi_ip") int32_t wos_wifi_ip(char* buf, uint32_t cap);

/* http_client */
#define WOS_HTTP_METHOD_GET 0
#define WOS_HTTP_METHOD_POST 1

WOS_IMPORT("http_client", "http_client_config_create") uint32_t wos_http_client_config_create(void);
WOS_IMPORT("http_client", "http_client_config_set_url") int32_t wos_http_client_config_set_url(uint32_t cfg, const char* url);
WOS_IMPORT("http_client", "http_client_config_set_method") int32_t wos_http_client_config_set_method(uint32_t cfg, int32_t method);
WOS_IMPORT("http_client", "http_client_config_set_timeout_ms") int32_t wos_http_client_config_set_timeout_ms(uint32_t cfg, int32_t timeout_ms);
WOS_IMPORT("http_client", "http_client_config_destroy") int32_t wos_http_client_config_destroy(uint32_t cfg);
WOS_IMPORT("http_client", "http_client_init") uint32_t wos_http_client_init(uint32_t cfg);
WOS_IMPORT("http_client", "http_client_set_header") int32_t wos_http_client_set_header(uint32_t client, const char* key, const char* value);
WOS_IMPORT("http_client", "http_client_open") int32_t wos_http_client_open(uint32_t client, int32_t write_len);
WOS_IMPORT("http_client", "http_client_write") int32_t wos_http_client_write(uint32_t client, const void* buf, uint32_t len);
WOS_IMPORT("http_client", "http_client_fetch_headers") int32_t wos_http_client_fetch_headers(uint32_t client);
WOS_IMPORT("http_client", "http_client_read_response") int32_t wos_http_client_read_response(uint32_t client, void* buf, uint32_t cap);
WOS_IMPORT("http_client", "http_client_get_status_code") int32_t wos_http_client_get_status_code(uint32_t client);
WOS_IMPORT("http_client", "http_client_cleanup") int32_t wos_http_client_cleanup(uint32_t client);

/* gfx (native RGB565 fill/blend helpers) */
WOS_IMPORT("gfx", "gfx_fill_rgb565") int32_t wos_gfx_fill_rgb565(void* dst, int32_t dst_stride, int32_t w, int32_t h, uint32_t color);
WOS_IMPORT("gfx", "gfx_fill_rgb565_opa") int32_t wos_gfx_fill_rgb565_opa(void* dst, int32_t dst_stride, int32_t w, int32_t h, uint32_t color, uint32_t opa);
WOS_IMPORT("gfx", "gfx_fill_rgb565_mask") int32_t wos_gfx_fill_rgb565_mask(void* dst, int32_t dst_stride, const void* mask, int32_t mask_stride, int32_t w, int32_t h, uint32_t color, uint32_t opa);

/* i2c_master */
WOS_IMPORT("i2c_master", "i2c_master_bus_config_create") uint32_t wos_i2c_master_bus_config_create(void);
WOS_IMPORT("i2c_master", "i2c_master_bus_config_set_i2c_port") int32_t wos_i2c_master_bus_config_set_i2c_port(uint32_t cfg, int32_t port);
WOS_IMPORT("i2c_master", "i2c_master_bus_config_set_sda_io_num") int32_t wos_i2c_master_bus_config_set_sda_io_num(uint32_t cfg, int32_t pin);
WOS_IMPORT("i2c_master", "i2c_master_bus_config_set_scl_io_num") int32_t wos_i2c_master_bus_config_set_scl_io_num(uint32_t cfg, int32_t pin);
WOS_IMPORT("i2c_master", "i2c_master_bus_config_set_enable_internal_pullup") int32_t wos_i2c_master_bus_config_set_enable_internal_pullup(uint32_t cfg, uint32_t enable);
WOS_IMPORT("i2c_master", "i2c_master_bus_config_destroy") int32_t wos_i2c_master_bus_config_destroy(uint32_t cfg);
WOS_IMPORT("i2c_master", "i2c_new_master_bus") int32_t wos_i2c_new_master_bus(uint32_t cfg);

WOS_IMPORT("i2c_master", "i2c_device_config_create") uint32_t wos_i2c_device_config_create(void);
WOS_IMPORT("i2c_master", "i2c_device_config_set_device_address") int32_t wos_i2c_device_config_set_device_address(uint32_t cfg, uint32_t address);
WOS_IMPORT("i2c_master", "i2c_device_config_set_scl_speed_hz") int32_t wos_i2c_device_config_set_scl_speed_hz(uint32_t cfg, uint32_t hz);
WOS_IMPORT("i2c_master", "i2c_device_config_destroy") int32_t wos_i2c_device_config_destroy(uint32_t cfg);
WOS_IMPORT("i2c_master", "i2c_master_bus_add_device") int32_t wos_i2c_master_bus_add_device(int32_t port, uint32_t cfg, uint32_t* out);

WOS_IMPORT("i2c_master", "i2c_master_transmit") int32_t wos_i2c_master_transmit(uint32_t device, const void* data, uint32_t len, int32_t timeout_ms);
WOS_IMPORT("i2c_master", "i2c_master_transmit_receive") int32_t wos_i2c_master_transmit_receive(uint32_t device, const void* tx, uint32_t tx_len, void* rx, uint32_t rx_len, int32_t timeout_ms);
