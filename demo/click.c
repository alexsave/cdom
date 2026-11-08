/* The click demo: a tap plays a click. The click is made here, as WAV bytes (a 30 ms burst of
 * noise that dies away), and loaded at start; every tap on the button plays it at a slightly
 * different pitch. Until the browser lets the page make sound the hint says to tap, and the first
 * tap both unlocks the sound and is heard. Built by tests/build.sh; tools/unit.mjs taps it. */
#include "../src/cdom.h"
#include "../src/cdom_audio.h"

#define L(s) CD_LIT(s)
#define RATE 22050
#define SAMPLES (RATE * 30 / 1000)

static uint8_t click_wav[44 + 2 * SAMPLES];
static uint32_t rng = 0x9e3779b9u;
static uint32_t next_random(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
/* 16-bit mono PCM: noise whose loudness falls by 1/64 every sample (to under 1/10,000 by the end). */
static void make_click(void) {
  uint8_t *w = click_wav;
  __builtin_memcpy(w, "RIFF", 4); put32(w + 4, 36 + 2 * SAMPLES);
  __builtin_memcpy(w + 8, "WAVEfmt ", 8); put32(w + 16, 16);
  put16(w + 20, 1); put16(w + 22, 1); put32(w + 24, RATE); put32(w + 28, 2 * RATE);
  put16(w + 32, 2); put16(w + 34, 16);
  __builtin_memcpy(w + 36, "data", 4); put32(w + 40, 2 * SAMPLES);
  int32_t loudness = 24000;
  for (uint32_t i = 0; i < SAMPLES; i++) {
    int32_t sample = (int32_t)(next_random() % (uint32_t)(2 * loudness + 1)) - loudness;
    put16(w + 44 + 2 * i, (uint32_t)sample);
    loudness = loudness * 63 / 64;
  }
}

static uint32_t clip = CD_AUDIO_FAILED;
static uint32_t played;
static cd_id hint, button, played_txt;

static void show_state(void) {
  uint32_t state = cd_audio_state();
  cd_set_text(hint, state == CD_AUDIO_RUNNING ? L("sound on") : L("tap for sound"));
}

void cd_main(void) {
  make_click();
  cd_audio_load(1, click_wav, sizeof click_wav);
  cd_at(CD_MOUNT, 0);
  cd_open(L("p")); cd_text(L("tap for sound")); cd_name(hint = cd_id_new()); cd_close();
  cd_open(L("button"));
  cd_attr(L("style"), L("font: 24px sans-serif; padding: 24px 48px; touch-action: manipulation;"));
  cd_listen(button = cd_id_new(), CD_EV_POINTERDOWN);
  cd_text(L("Tap"));
  cd_close();
  cd_open(L("p")); cd_text(L("clicks played: ")); cd_text_num(0); cd_name(played_txt = cd_id_new()); cd_close();
  show_state();
}

void cd_on_audio_loaded(uint32_t token, uint32_t loaded) {
  (void)token;
  clip = loaded;
}

void cd_on_audio_state(uint32_t state) {
  (void)state;
  show_state();
}

/* A tap plays the click now, its pitch within 10% either way. */
void cd_on_event(const cd_event_rec *e) {
  if (e->type != CD_EV_POINTERDOWN || (cd_id)e->id != button || clip == CD_AUDIO_FAILED) return;
  double rate = 0.9 + (double)(next_random() % 21) / 100;
  cd_audio_play(clip, 0.8, rate, 0);
}

void cd_on_audio_ended(uint32_t voice) {
  (void)voice;
  played++;
  cd_set_text_num(played_txt, played);
}
