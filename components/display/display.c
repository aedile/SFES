/* display.c - ST77916 over QSPI via esp_lcd; panel reset lives on a TCA9554 I/O expander. */
#include "sfes_display.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY";
extern const st77916_lcd_init_cmd_t vendor_specific_init_new[];
extern const int vendor_specific_init_new_count;

static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static uint16_t *strip[2];
static int cur;
uint32_t display_wait_us;
#define GAME_X ((DISPLAY_WIDTH - GAME_WIDTH) / 2)
#define GAME_Y ((DISPLAY_HEIGHT - GAME_HEIGHT) / 2)
#define STRIP_BYTES (GAME_WIDTH * STRIP_ROWS * 2)

/* ---- TCA9554: the only pins we drive are resets and SD CS, all outputs ---- */
static i2c_master_dev_handle_t exio;
static uint8_t exio_out = 0xFF;

static void exio_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    ESP_ERROR_CHECK(i2c_master_transmit(exio, buf, 2, 100));
}

static void exio_set(int bit, bool level)
{
    exio_out = level ? (exio_out | (1 << bit)) : (exio_out & ~(1 << bit));
    exio_write(0x01, exio_out);
}

static void exio_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t h;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &h));
    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x20, .scl_speed_hz = 400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(h, &dev, &exio));
    exio_write(0x01, exio_out);   /* outputs high (resets released, SD deselected) */
    exio_write(0x03, 0x00);       /* all pins output */
}

void display_init(void)
{
    exio_init();
    exio_set(EXIO_LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    exio_set(EXIO_LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));

    spi_bus_config_t bus = ST77916_PANEL_BUS_QSPI_CONFIG(PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3, STRIP_BYTES);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    /* Waveshare ships two panel revisions; the ID register tells them apart. Read it slowly, then reopen fast. */
    esp_lcd_panel_io_spi_config_t iocfg = ST77916_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    iocfg.pclk_hz = 3 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &io));
    uint8_t id[4] = {0};
    esp_err_t rd = esp_lcd_panel_io_rx_param(io, (0x0BULL << 24) | (0x04 << 8), id, sizeof id);
    ESP_LOGI(TAG, "ST77916 ID reg 04h: %02x %02x %02x %02x (%s)", id[0], id[1], id[2], id[3], esp_err_to_name(rd));
    ESP_ERROR_CHECK(esp_lcd_panel_io_del(io));
    iocfg.pclk_hz = 80 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &iocfg, &io));

    st77916_vendor_config_t vendor = { .flags.use_qspi_interface = 1 };
    if (id[0] == 0x00 && id[1] == 0x02 && id[2] == 0x7F && id[3] == 0x7F) {
        vendor.init_cmds = vendor_specific_init_new;
        vendor.init_cmds_size = vendor_specific_init_new_count;
        ESP_LOGI(TAG, "using the newer panel's init table");
    }
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16, .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io, &pcfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    strip[0] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA);
    strip[1] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA);
    assert(strip[0] && strip[1]);
    display_fill(0x0000);
    display_set_backlight(200);
    ESP_LOGI(TAG, "ST77916 360x360 up, game window at %d,%d", GAME_X, GAME_Y);
}

/* draw_bitmap queues the strip and returns; the next call waits for it, so convert the following
 * strip into the other buffer meanwhile. */
IRAM_ATTR void display_push_strip(const uint16_t *fb, int pitch, int y0)
{
    uint32_t *dst = (uint32_t *)strip[cur];
    for (int r = 0; r < STRIP_ROWS; r++) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)fb + (y0 + r) * pitch);
        for (int x = 0; x < GAME_WIDTH / 2; x += 4) {
            uint32_t a = src[x], b = src[x + 1], c = src[x + 2], d = src[x + 3];
            dst[0] = ((a & 0xFF00FF00u) >> 8) | ((a & 0x00FF00FFu) << 8);
            dst[1] = ((b & 0xFF00FF00u) >> 8) | ((b & 0x00FF00FFu) << 8);
            dst[2] = ((c & 0xFF00FF00u) >> 8) | ((c & 0x00FF00FFu) << 8);
            dst[3] = ((d & 0xFF00FF00u) >> 8) | ((d & 0x00FF00FFu) << 8);
            dst += 4;
        }
    }
    int64_t w0 = esp_timer_get_time();
    esp_lcd_panel_draw_bitmap(panel, GAME_X, GAME_Y + y0, GAME_X + GAME_WIDTH, GAME_Y + y0 + STRIP_ROWS, strip[cur]);
    display_wait_us += esp_timer_get_time() - w0;
    cur ^= 1;
}

void display_push_rgb565(const uint16_t *fb, int pitch)
{
    for (int y0 = 0; y0 < GAME_HEIGHT; y0 += STRIP_ROWS)
        display_push_strip(fb, pitch, y0);
}

void display_fill(uint16_t color)
{
    uint16_t sw = (color >> 8) | (color << 8);
    for (int i = 0; i < STRIP_BYTES / 2; i++) strip[0][i] = sw;
    /* the strip buffer is 256 px wide; fill in 256-wide column bands of STRIP_ROWS */
    for (int y = 0; y < DISPLAY_HEIGHT; y += STRIP_ROWS)
        for (int x = 0; x < DISPLAY_WIDTH; x += GAME_WIDTH) {
            int w = DISPLAY_WIDTH - x < GAME_WIDTH ? DISPLAY_WIDTH - x : GAME_WIDTH;
            esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + STRIP_ROWS, strip[0]);
        }
}

void display_set_backlight(uint8_t brightness)
{
    static bool init;
    if (!init) {
        ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
            .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
        ledc_timer_config(&t);
        ledc_channel_config_t c = { .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0, .gpio_num = PIN_LCD_BL, .duty = 0 };
        ledc_channel_config(&c);
        init = true;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
