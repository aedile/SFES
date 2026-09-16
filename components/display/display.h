/*
 * display.h - ST77916 360x360 QSPI panel on the Waveshare ESP32-S3-Touch-LCD-1.85.
 * The SNES frame (256x224 RGB565) is pushed 1:1, centred, in STRIP_ROWS strips.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH   360
#define DISPLAY_HEIGHT  360
#define GAME_WIDTH      256
#define GAME_HEIGHT     224
#define STRIP_ROWS      16

/* Waveshare ESP32-S3-Touch-LCD-1.85 wiring */
#define PIN_LCD_SCK     40
#define PIN_LCD_D0      46
#define PIN_LCD_D1      45
#define PIN_LCD_D2      42
#define PIN_LCD_D3      41
#define PIN_LCD_CS      21
#define PIN_LCD_TE      18
#define PIN_LCD_BL      5
#define PIN_I2C_SCL     10   /* TCA9554 expander (0x20), IMU, RTC */
#define PIN_I2C_SDA     11
#define EXIO_TP_RST     0    /* TCA9554 bit numbers */
#define EXIO_LCD_RST    1
#define EXIO_SD_CS      2

void display_init(void);
/* Push a native-endian RGB565 frame: GAME_HEIGHT rows of GAME_WIDTH pixels, pitch in bytes. */
void display_push_rgb565(const uint16_t *fb, int pitch);
extern uint32_t display_wait_us;   /* time blocked on the previous strip's DMA (profiling) */
void display_fill(uint16_t color);
void display_set_backlight(uint8_t brightness);   /* 0-255 */

#ifdef __cplusplus
}
#endif
