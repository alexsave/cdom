# Netscape 2 paints itself (docs/descent.md): render.c, a span renderer of boxes into runs of stretched 1x1 GIFs, built
#   out/ns2render         natively (main.c: the HTML, the reference pictures, the stats and the timing)
#   out/paint.wasm        as wasm (wasm.c's exports), then out/paint.wasm.js1.js through tools/js1c (tools/js1.sh)
#   out/paint.app.html    the app document of the frames tier: host.js, status.js and the module inline (build.mjs app-doc-raw)
#   out/index.html        the frameset (cdomView over cdomApp), out/blank.html, and the palette's GIFs
# make -C demo/ns2render; serve out/ and open index.html in Netscape 2.02.
CDOM := $(abspath ../..)
WASM_CC ?= /opt/homebrew/opt/llvm/bin/clang
CC ?= cc
SRC := render.c page_data.c
W := -Wall -Wextra -Werror
WASM_FLAGS := --target=wasm32 -nostdlib -mbulk-memory -O2 $(W) -Wl,--no-entry -Wl,--export-memory -Wl,--stack-first \
  -Wl,-z,stack-size=8192 -Wl,--initial-memory=196608 -Wl,--max-memory=196608 -Wl,--strip-all

all: out/ns2render out/paint.wasm.js1.js out/paint.app.html out/index.html
.PHONY: all clean

out/ns2render: main.c $(SRC) render.h
	@mkdir -p out
	$(CC) -O2 $(W) -o $@ main.c $(SRC)
out/paint.wasm: wasm.c $(SRC) render.h
	@mkdir -p out
	$(WASM_CC) $(WASM_FLAGS) -o $@ wasm.c $(SRC)
out/paint.wasm.js1.js: out/paint.wasm $(CDOM)/tools/js1.sh $(CDOM)/tools/js1c.c
	$(CDOM)/tools/js1.sh $< $@
out/paint.app.html: host.js status.js start.js out/paint.wasm.js1.js $(CDOM)/tools/legacy/build.mjs
	node $(CDOM)/tools/legacy/build.mjs app-doc-raw host.js+status.js+start.js out/paint.wasm.js1.js $@
out/index.html: index.html blank.html $(CDOM)/tools/gen_ns2_gifs.py page_data.c
	@mkdir -p out
	cp index.html blank.html out/
	python3 $(CDOM)/tools/gen_ns2_gifs.py page_data.c out

clean:
	rm -rf out
