/* audio.h - PCM5101 DAC on I2S (Waveshare ESP32-S3-Touch-LCD-1.85: BCLK 48, LRCK 38, DIN 47), 16-bit stereo. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* frames_per_write sizes each DMA buffer; the queue holds AUDIO_DMA_BUFFERS of them. */
#define AUDIO_DMA_BUFFERS 6
void audio_init(int sample_rate, int frames_per_write);
/* Blocks while the DMA queue is full, so the caller is paced to the DAC clock. */
void audio_write(const int16_t *stereo, size_t frames);

#ifdef __cplusplus
}
#endif
