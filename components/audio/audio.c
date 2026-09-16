#include "audio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#define PIN_I2S_BCLK 48
#define PIN_I2S_LRCK 38
#define PIN_I2S_DOUT 47

static i2s_chan_handle_t tx;
static bool muted;
void audio_set_mute(bool m) { muted = m; }
bool audio_muted(void) { return muted; }

void audio_init(int sample_rate, int frames_per_write)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = AUDIO_DMA_BUFFERS;
    chan.dma_frame_num = frames_per_write;
    chan.auto_clear = true;   /* starved queue plays silence, not the last buffer again */
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_LRCK, .dout = PIN_I2S_DOUT, .din = I2S_GPIO_UNUSED },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_LOGI("AUDIO", "PCM5101 I2S up: %d Hz, %d-frame buffers x %d", sample_rate, frames_per_write, AUDIO_DMA_BUFFERS);
}

void audio_write(const int16_t *stereo, size_t frames)
{
    size_t written;
    static int16_t zeros[1200 * 2];
    if (muted && frames <= 1200) stereo = zeros;
    i2s_channel_write(tx, stereo, frames * 4, &written, portMAX_DELAY);
}
