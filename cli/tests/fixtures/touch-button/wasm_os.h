/**
 * wasm-os host imports used by this app. Signatures follow the ABI
 * documented in the main/bindings .wit files: handles are opaque u32
 * (0 = invalid), status returns are 0 on success / negative on error.
 */
#pragma once

#include <stdint.h>

#define WOS_IMPORT(module, name) __attribute__((import_module(module), import_name(name)))

/* env */
WOS_IMPORT("env", "println") void wos_println(const char* message);
WOS_IMPORT("env", "delay") void wos_delay(int32_t milliseconds);
WOS_IMPORT("env", "millis") uint32_t wos_millis(void);

/* gpio */
#define WOS_GPIO_MODE_INPUT 1
#define WOS_GPIO_MODE_OUTPUT 2

WOS_IMPORT("gpio", "gpio_set_direction") int32_t wos_gpio_set_direction(int32_t pin, int32_t mode);
WOS_IMPORT("gpio", "gpio_set_level") int32_t wos_gpio_set_level(int32_t pin, uint32_t level);
WOS_IMPORT("gpio", "gpio_get_level") int32_t wos_gpio_get_level(int32_t pin);

/* spi_master */
#define WOS_SPI2_HOST 1
#define WOS_SPI3_HOST 2
#define WOS_SPI_DMA_AUTO 3

WOS_IMPORT("spi_master", "spi_bus_config_create") uint32_t wos_spi_bus_config_create(void);
WOS_IMPORT("spi_master", "spi_bus_config_set_mosi_io_num") int32_t wos_spi_bus_config_set_mosi_io_num(uint32_t cfg, int32_t pin);
WOS_IMPORT("spi_master", "spi_bus_config_set_miso_io_num") int32_t wos_spi_bus_config_set_miso_io_num(uint32_t cfg, int32_t pin);
WOS_IMPORT("spi_master", "spi_bus_config_set_sclk_io_num") int32_t wos_spi_bus_config_set_sclk_io_num(uint32_t cfg, int32_t pin);
WOS_IMPORT("spi_master", "spi_bus_config_destroy") int32_t wos_spi_bus_config_destroy(uint32_t cfg);
WOS_IMPORT("spi_master", "spi_bus_initialize") int32_t wos_spi_bus_initialize(int32_t host, uint32_t cfg, int32_t dma_chan);

WOS_IMPORT("spi_master", "spi_device_interface_config_create") uint32_t wos_spi_device_config_create(void);
WOS_IMPORT("spi_master", "spi_device_interface_config_set_spics_io_num") int32_t wos_spi_device_config_set_spics_io_num(uint32_t cfg, int32_t pin);
WOS_IMPORT("spi_master", "spi_device_interface_config_set_clock_speed_hz") int32_t wos_spi_device_config_set_clock_speed_hz(uint32_t cfg, int32_t hz);
WOS_IMPORT("spi_master", "spi_device_interface_config_set_mode") int32_t wos_spi_device_config_set_mode(uint32_t cfg, int32_t mode);
WOS_IMPORT("spi_master", "spi_device_interface_config_destroy") int32_t wos_spi_device_config_destroy(uint32_t cfg);
WOS_IMPORT("spi_master", "spi_bus_add_device") int32_t wos_spi_bus_add_device(int32_t host, uint32_t cfg, uint32_t* device_out);

WOS_IMPORT("spi_master", "spi_transaction_create") uint32_t wos_spi_transaction_create(void);
WOS_IMPORT("spi_master", "spi_transaction_set_length") int32_t wos_spi_transaction_set_length(uint32_t txn, int32_t length_bits);
WOS_IMPORT("spi_master", "spi_transaction_set_tx_buffer") int32_t wos_spi_transaction_set_tx_buffer(uint32_t txn, const void* buf);
WOS_IMPORT("spi_master", "spi_transaction_set_rx_buffer") int32_t wos_spi_transaction_set_rx_buffer(uint32_t txn, void* buf);
WOS_IMPORT("spi_master", "spi_transaction_destroy") int32_t wos_spi_transaction_destroy(uint32_t txn);
WOS_IMPORT("spi_master", "spi_device_transmit") int32_t wos_spi_device_transmit(uint32_t device, uint32_t txn);
