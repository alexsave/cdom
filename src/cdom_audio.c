/* cdom_audio: the sound imports, the ids and the mute, and the exports that answer (cdom_audio.h).
 * Separate from cdom.c so an app without sound does not carry its exports. */
#include "cdom.h"
#include "cdom_audio.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("audio_load") void j_audio_load(uint32_t token, uint32_t clip, const void *p, uint32_t len);
IMPORT("audio_free") void j_audio_free(uint32_t clip);
IMPORT("audio_play") void j_audio_play(uint32_t clip, uint32_t voice, double gain, double rate, double at_ms);
IMPORT("audio_stop") void j_audio_stop(uint32_t voice);
IMPORT("audio_gain") void j_audio_gain(uint32_t voice, double gain);
IMPORT("audio_master") void j_audio_master(double gain);
IMPORT("audio_state") uint32_t j_audio_state(void);

/* Ids from 1: a freed id is handed out again first (last freed, first out), and a byte per id says
 * whether it is live, so freeing a free id is caught rather than handing one id out twice. */
typedef struct { uint32_t *freed; uint8_t *live; uint32_t nfreed, high, cap; } id_pool;
static id_pool clips, voices;

static uint32_t id_take(id_pool *p) {
  uint32_t id;
  if (p->nfreed) {
    id = p->freed[--p->nfreed];
  } else {
    id = ++p->high;
    if (id >= p->cap) { /* freed holds at most every id, live one byte each */
      p->cap = p->cap ? p->cap * 2 : 16;
      p->freed = cd_realloc(p->freed, p->cap * sizeof *p->freed);
      p->live = cd_realloc(p->live, p->cap);
    }
  }
  p->live[id] = 1;
  return id;
}
static int id_live(const id_pool *p, uint32_t id) { return id && id <= p->high && p->live[id]; }
static void id_give(id_pool *p, uint32_t id) {
  if (!id_live(p, id)) __builtin_trap();
  p->live[id] = 0;
  p->freed[p->nfreed++] = id;
}

void cd_audio_load(uint32_t token, const void *p, uint32_t len) { j_audio_load(token, id_take(&clips), p, len); }
void cd_audio_free(uint32_t clip) {
  id_give(&clips, clip);
  j_audio_free(clip);
}
uint32_t cd_audio_play(uint32_t clip, double gain, double rate, double at_ms) {
  if (!id_live(&clips, clip)) __builtin_trap();
  uint32_t voice = id_take(&voices);
  j_audio_play(clip, voice, gain, rate, at_ms);
  return voice;
}
void cd_audio_stop(uint32_t voice) {
  if (id_live(&voices, voice)) j_audio_stop(voice);
}
void cd_audio_set_gain(uint32_t voice, double gain) {
  if (id_live(&voices, voice)) j_audio_gain(voice, gain);
}

/* The mute is C's: the shim has one master gain and is told what it is. */
static double master = 1;
static int muted;
void cd_audio_master(double gain) {
  master = gain;
  j_audio_master(muted ? 0 : master);
}
void cd_audio_mute(int on) {
  muted = on;
  j_audio_master(muted ? 0 : master);
}
uint32_t cd_audio_state(void) { return j_audio_state(); }

__attribute__((weak)) void cd_on_audio_loaded(uint32_t token, uint32_t clip) { (void)token; (void)clip; }
__attribute__((weak)) void cd_on_audio_ended(uint32_t voice) { (void)voice; }
__attribute__((weak)) void cd_on_audio_state(uint32_t state) { (void)state; }

/* A clip that did not decode gives its id back before the app hears. */
EXPORT("cd_audio_loaded") void cd_audio_loaded(uint32_t token, uint32_t clip, uint32_t ok) {
  if (!ok) {
    id_give(&clips, clip);
    clip = CD_AUDIO_FAILED;
  }
  cd_on_audio_loaded(token, clip);
  cd_flush();
}
/* The voice's id is free before the app hears, so the hook may play again on it. */
EXPORT("cd_audio_ended") void cd_audio_ended(uint32_t voice) {
  id_give(&voices, voice);
  cd_on_audio_ended(voice);
  cd_flush();
}
EXPORT("cd_audio_state_changed") void cd_audio_state_changed(uint32_t state) {
  cd_on_audio_state(state);
  cd_flush();
}
