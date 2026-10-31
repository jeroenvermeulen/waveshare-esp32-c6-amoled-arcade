#include <string.h>
#include "audio.h"
#include "board.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// A tiny chip-tune synth: each effect is a list of notes with a pitch sweep,
// played on one of a few voices and mixed into the ES8311.

#define SAMPLE_RATE 16000
#define BLOCK       256
#define VOICES      4

enum { SQ, TRI, NOISE };

typedef struct {
    uint16_t f0, f1;    // Hz at start and end of the note; 0 = rest
    uint16_t ms;
    uint8_t wave;
    uint8_t vol;        // 0..100
} note_t;

typedef struct {
    const note_t *notes;
    uint8_t count;
} sfx_def_t;

#define N(...) (const note_t[]){ __VA_ARGS__ }
#define DEF(...) { N(__VA_ARGS__), sizeof(N(__VA_ARGS__)) / sizeof(note_t) }

static const sfx_def_t s_sfx[SFX_COUNT] = {
    [SFX_SELECT]    = DEF({660, 660, 35, SQ, 35}, {990, 990, 50, SQ, 35}),
    [SFX_START]     = DEF({523, 523, 70, SQ, 40}, {659, 659, 70, SQ, 40}, {784, 784, 70, SQ, 40}, {1047, 1047, 150, SQ, 40}),
    [SFX_PAUSE]     = DEF({880, 880, 40, SQ, 35}, {440, 440, 70, SQ, 35}),
    [SFX_GAME_OVER] = DEF({392, 392, 160, SQ, 40}, {330, 330, 160, SQ, 40}, {262, 262, 160, SQ, 40}, {196, 180, 450, SQ, 40}),
    [SFX_LEVEL_UP]  = DEF({784, 784, 70, SQ, 40}, {988, 988, 70, SQ, 40}, {1175, 1175, 70, SQ, 40}, {1568, 1568, 160, SQ, 40}),
    [SFX_MOVE]      = DEF({300, 300, 14, SQ, 15}),
    [SFX_ROTATE]    = DEF({500, 760, 30, SQ, 22}),
    [SFX_LOCK]      = DEF({180, 90, 50, TRI, 70}),
    [SFX_DROP]      = DEF({500, 120, 70, SQ, 30}, {120, 60, 40, TRI, 60}),
    [SFX_LINE]      = DEF({600, 600, 50, SQ, 35}, {800, 800, 50, SQ, 35}, {1000, 1000, 90, SQ, 35}),
    [SFX_QUAD]      = DEF({523, 523, 50, SQ, 40}, {659, 659, 50, SQ, 40}, {784, 784, 50, SQ, 40},
                          {1047, 1047, 50, SQ, 40}, {1319, 1319, 50, SQ, 40}, {1568, 1568, 180, SQ, 40}),
    [SFX_WALL]      = DEF({440, 440, 22, SQ, 25}),
    [SFX_PADDLE]    = DEF({262, 262, 35, SQ, 40}),
    [SFX_BRICK]     = DEF({880, 1320, 45, SQ, 32}),
    [SFX_BRICK_HIT] = DEF({660, 660, 30, TRI, 45}),
    [SFX_STEEL]     = DEF({1600, 1600, 25, TRI, 40}, {1200, 1200, 40, TRI, 30}),
    [SFX_POWERUP]   = DEF({400, 1400, 180, SQ, 32}),
    [SFX_LOSE_LIFE] = DEF({520, 140, 450, SQ, 40}),
    [SFX_HOP]       = DEF({360, 720, 40, SQ, 25}),
    [SFX_SPLASH]    = DEF({2400, 300, 300, NOISE, 45}),
    [SFX_CRASH]     = DEF({1500, 150, 320, NOISE, 55}),
};

typedef struct {
    const note_t *notes;
    uint8_t count, idx;
    uint32_t pos, len;
    uint32_t phase, inc;
    uint16_t lfsr;
    bool noise_hi;
} voice_t;

static const char *TAG = "audio";
static esp_codec_dev_handle_t s_dev;
static QueueHandle_t s_queue;
static voice_t s_voices[VOICES];
static bool s_enabled = true;

static void start_note(voice_t *v)
{
    v->pos = 0;
    v->len = v->notes[v->idx].ms * SAMPLE_RATE / 1000;
}

static void claim_voice(sfx_t id)
{
    // Prefer a free voice, otherwise steal the one closest to finishing
    voice_t *best = &s_voices[0];
    for (int i = 0; i < VOICES; i++) {
        voice_t *v = &s_voices[i];
        if (!v->notes) { best = v; break; }
        if (v->idx > best->idx) best = v;
    }
    best->notes = s_sfx[id].notes;
    best->count = s_sfx[id].count;
    best->idx = 0;
    best->lfsr = 0xACE1;
    start_note(best);
}

static int16_t voice_sample(voice_t *v)
{
    const note_t *nt = &v->notes[v->idx];
    int32_t out = 0;
    if (nt->f0) {
        if ((v->pos & 31) == 0) {
            int32_t f = nt->f0 + ((int32_t)nt->f1 - nt->f0) * (int32_t)v->pos / (int32_t)v->len;
            v->inc = (uint32_t)(((uint64_t)f << 32) / SAMPLE_RATE);
        }
        uint32_t prev = v->phase;
        v->phase += v->inc;
        int32_t env = 256 - (int32_t)(v->pos * 180 / v->len);
        int32_t amp = nt->vol * 70 * env / 256;
        switch (nt->wave) {
        case SQ:
            out = (int32_t)v->phase < 0 ? amp : -amp;
            break;
        case TRI: {
            int32_t t = v->phase >> 16;
            int32_t tri = t < 32768 ? t * 2 - 32768 : (65535 - t) * 2 - 32768;
            out = tri * amp / 32768;
            break;
        }
        default:
            if (v->phase < prev) {
                v->lfsr = (v->lfsr >> 1) ^ (-(v->lfsr & 1) & 0xB400);
                v->noise_hi = v->lfsr & 1;
            }
            out = v->noise_hi ? amp : -amp;
            break;
        }
    }
    if (++v->pos >= v->len) {
        if (++v->idx >= v->count) v->notes = NULL;
        else start_note(v);
    }
    return out;
}

static void audio_task(void *arg)
{
    static int16_t buf[BLOCK * 2];
    for (;;) {
        sfx_t id;
        while (xQueueReceive(s_queue, &id, 0) == pdTRUE) {
            claim_voice(id);
        }

        bool active = false;
        for (int i = 0; i < VOICES; i++) active |= s_voices[i].notes != NULL;
        if (!active) {
            // Nothing to play: let I2S send silence and wait for the next effect
            if (xQueueReceive(s_queue, &id, portMAX_DELAY) == pdTRUE) claim_voice(id);
            continue;
        }

        for (int n = 0; n < BLOCK; n++) {
            int32_t mix = 0;
            for (int i = 0; i < VOICES; i++) {
                if (s_voices[i].notes) mix += voice_sample(&s_voices[i]);
            }
            if (mix > 32767) mix = 32767;
            if (mix < -32768) mix = -32768;
            buf[2 * n] = buf[2 * n + 1] = (int16_t)mix;
        }
        esp_codec_dev_write(s_dev, buf, sizeof(buf));
    }
}

void audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 128;
    i2s_chan_handle_t tx;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = board_i2c() };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_dev = codec_if ? esp_codec_dev_new(&dev_cfg) : NULL;
    if (!s_dev) {
        ESP_LOGE(TAG, "ES8311 not found, running without sound");
        return;
    }
    esp_codec_dev_sample_info_t fs = { .sample_rate = SAMPLE_RATE, .channel = 2, .bits_per_sample = 16 };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES8311 open failed, running without sound");
        s_dev = NULL;
        return;
    }
    esp_codec_dev_set_out_vol(s_dev, 70);

    s_queue = xQueueCreate(8, sizeof(sfx_t));
    xTaskCreate(audio_task, "audio", 3072, NULL, 6, NULL);
}

void audio_set_enabled(bool on)
{
    s_enabled = on;
}

bool audio_enabled(void)
{
    return s_enabled;
}

void sfx(sfx_t id)
{
    if (s_enabled && s_queue) {
        xQueueSend(s_queue, &id, 0);
    }
}
