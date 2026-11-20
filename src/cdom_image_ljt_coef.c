/* libjpeg-turbo's coefficient controller, built as its own wrapper/jdcoefct-8.c builds it (8-bit samples), under
 * another name: cdom's jinit_d_coef_controller (src/cdom_image_jpegp.c) is the one jdmaster.c calls, and it hands this
 * one every file that is not progressive. Compiled with the vendored sources' flags (cdom.mk, CDOM_IMAGE_VENDOR_SRC);
 * the vendored files are not edited. */
#define BITS_IN_JSAMPLE 8
#define jinit_d_coef_controller cdom_ljt_d_coef_controller
#include "jdcoefct.c"
