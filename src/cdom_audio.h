/* cdom_audio: sound through Web Audio. Link src/cdom_audio.c to use it. C decides what plays, when
 * and how loud; the shim only wraps Web Audio: a decoded buffer per clip, a source and a gain node
 * per voice, and one master gain in front of the speakers. Clip and voice ids are C's: handed out
 * from 1, and a freed id is handed out again first, so the shim's tables grow with the clips held
 * and the voices playing at once, not with every sound a page ever played. Every answer arrives
 * from the event loop through an export that flushes on its way out. */
#ifndef CDOM_AUDIO_H
#define CDOM_AUDIO_H
#include <stdint.h>

/* Decode an encoded clip (WAV, MP3, AAC, Ogg or Opus: whatever the browser plays) from len bytes
 * at p, read during the call. The answer is cd_on_audio_loaded(token, clip): the clip's id, or
 * CD_AUDIO_FAILED for bytes the browser cannot decode. Decoding needs no gesture: load at start. */
void cd_audio_load(uint32_t token, const void *p, uint32_t len);
#define CD_AUDIO_FAILED 0
/* Drop a loaded clip; its id may come back from a later load. Voices playing it play out.
 * Freeing a clip that is not loaded traps. */
void cd_audio_free(uint32_t clip);

/* Play a loaded clip (a clip that is not loaded traps) and return the voice's id. gain scales its
 * samples (1 as recorded, 0 silent); rate is the playback rate (1 as recorded, 2 twice as fast and
 * an octave up). at_ms is a time on cd_now()'s clock, the clock of cd_on_raf's t, at which it
 * starts: a frame's t plus 250 starts it a quarter second after that frame; a time already past
 * (0 among them) starts it now. A voice id is good from here until its cd_on_audio_ended, which
 * always comes, whether it played out or was stopped: the id is free by then, and a play (from
 * that hook or later) may hand it out again. */
uint32_t cd_audio_play(uint32_t clip, double gain, double rate, double at_ms);
/* Stop a voice now (one that has not started yet never starts); its ended answer follows. */
void cd_audio_stop(uint32_t voice);
/* The voice's gain from now on, eased in over a few ms so the change does not click. */
void cd_audio_set_gain(uint32_t voice, double gain);
/* After a voice's ended answer, stop and set_gain on its id are ignored (or reach the voice that
 * took the id since). */

/* The master gain, in front of every voice (1 at start), and mute: while muted the master is 0,
 * and unmuting brings back the last master gain set, muted or not. Eased in as a voice's. */
void cd_audio_master(double gain);
void cd_audio_mute(int on);

/* Whether sound can be heard. LOCKED: the browser has not let the page make sound yet (iOS
 * Safari and Chrome need a tap, a click or a key first); the shim resumes the audio context on
 * every pointer, touch or key gesture, so an app shows a "tap for sound" hint until it runs.
 * RUNNING: it plays. SUSPENDED: it ran, and stopped: the page is hidden (the shim suspends it then
 * and resumes it when the page shows again), or the system took it (a call on iOS), and the next
 * gesture brings it back. Voices played while not running start when it runs: an app that must
 * not pile sounds up checks the state first. */
enum { CD_AUDIO_LOCKED, CD_AUDIO_RUNNING, CD_AUDIO_SUSPENDED };
uint32_t cd_audio_state(void);

/* The app's hooks (empty weak defaults in cdom_audio.c). cd_on_audio_state comes each time the
 * audio context changes state, with the state cd_audio_state() reads from then on (it may repeat
 * one: the context has more states than these three). */
void cd_on_audio_loaded(uint32_t token, uint32_t clip);
void cd_on_audio_ended(uint32_t voice);
void cd_on_audio_state(uint32_t state);
#endif
