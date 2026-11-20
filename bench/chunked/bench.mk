# The chunked-read bench's page set for one piece size (bench/chunked/run.mjs builds one per size):
#   make -f bench/chunked/bench.mk OUT=dir PIECE=bytes
# writes OUT/bench.html: bench/chunked/app.c with the image, file and job modules, the image module built with
# -DCD_IMAGE_PIECE=$(PIECE), and the image job (tests/image/job.c, cd_job_pieces_cb and cd_job_file_cb) bundled.
CDOM := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
APP_WASM := $(OUT)/bench.wasm
APP_PAGE := $(OUT)/bench.html
APP_PAGE_SRC := $(CDOM)/bench/chunked/page.html
APP_SRC := $(CDOM)/bench/chunked/app.c
APP_MODULES := image file job
APP_CFLAGS := -Wall -Wextra -Werror -DCD_IMAGE_PIECE=$(PIECE)
APP_JOBS := imgjob
APP_JOB_imgjob_SRC := $(CDOM)/tests/image/job.c
APP_JOB_imgjob_MODULES := image
APP_JOB_CFLAGS := -Wall -Wextra -Werror -DCD_IMAGE_PIECE=$(PIECE)
include $(CDOM)/cdom.mk
all: $(APP_PAGE)
