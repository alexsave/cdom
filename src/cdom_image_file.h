/* cdom_image_file: a picked file decoded and fitted, read a piece at a time (cdom_file.h's reader into cdom_image.h's
 * fed decoder), so neither the file nor anything its size holds is ever in C memory whole. cdom.mk links
 * src/cdom_image_file.c when an app has both modules (APP_MODULES += file image). It is a few explicit calls with no
 * hidden wiring: an app that wants more (a progress bar, its own source of bytes) writes the same loop itself against
 * the two APIs. On the main thread each piece is decoded in its own task (input and paint run between them); a page
 * that must not stutter at all runs the same decoder in a job (cdom_job.h, cd_job_pieces_cb). */
#ifndef CDOM_IMAGE_FILE_H
#define CDOM_IMAGE_FILE_H
#include <stdint.h>
#include "cdom.h"
#include "cdom_image.h"

/* The file at index of input's files list, decoded and fitted to max_w x max_h as cd_image_resize does: fn(ctx,
 * status, rgba, w, h) once, from the event loop, with cd_image_resize's statuses plus UNREADABLE, which also stands
 * for cd_file_open's NO_FILE, TOO_BIG and UNSUPPORTED (no file to read). On OK, rgba is the caller's (cd_free); else
 * it is 0 and w and h are 0. *job names the decode for cd_image_file_stop. It counts against the 65,535 callbacks
 * pending at once. */
typedef void (*cd_image_file_fn)(void *ctx, uint32_t status, uint8_t *rgba, uint32_t w, uint32_t h);
void cd_image_file_cb(cd_id input, uint32_t index, uint32_t max_w, uint32_t max_h, cd_image_file_fn fn, void *ctx,
                      uint32_t *job);
/* Cancels it: the reader closed, the decoder ended, everything freed, fn never called. A decode already answered or
 * stopped does nothing. */
void cd_image_file_stop(uint32_t job);
#endif
