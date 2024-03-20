CC = emcc

JSDIR = js

GL4ES_DIR ?= /mnt/d/Development/emsdk/gl4es-master

# Emscripten fast linking: https://github.com/emscripten-core/emscripten/issues/17019
BUILD ?= devel

# Generate share/version.h
VERSION := $(shell sh scripts/version.sh)

ifeq ($(BUILD), devel)
#CFLAGS := -O1 -g -fsanitize=undefined -fsanitize=address -std=gnu99 -Wall -Ishare -DNDEBUG -DENABLE_FETCH=1 -I$(GL4ES_DIR)/include -sFULL_ES2=1 -lGL
CFLAGS := -O1 -g -fsanitize=undefined -fsanitize=address -std=gnu99 -Wall -Ishare -DNDEBUG -DENABLE_FETCH=1 -I$(GL4ES_DIR)/include 
else
#CFLAGS := -O3 -std=gnu99 -Wall -Ishare -DNDEBUG -DENABLE_FETCH=1 -I$(GL4ES_DIR)/include -sFULL_ES2=1 -lGL
CFLAGS := -O3 -std=gnu99 -Wall -Ishare -DNDEBUG -DENABLE_FETCH=1 -I$(GL4ES_DIR)/include
endif

EM_CFLAGS := \
	-s USE_SDL=2 \
	-s USE_SDL_TTF=2 \
	-s USE_VORBIS=1 \
	-s USE_LIBPNG=1 \
	-s USE_LIBJPEG=1

DATA_ZIP := data-emscripten.zip

EM_PRELOAD := \
	--preload-file $(DATA_ZIP)@/data/base.zip

# Exclude Neverputt + everything that can be downloaded later.
DATA_EXCLUDE := \
	'set*.txt' \
	'ball/atom/*' \
	'ball/blinky/*' \
	'ball/catseye/*' \
	'ball/cheese-ball/*' \
	'ball/diagonal-ball/*' \
	'ball/earth/*' \
	'ball/eyeball/*' \
	'ball/lava/*' \
	'ball/magic-eightball/*' \
	'ball/melon/*' \
	'ball/octocat/*' \
	'ball/orange/*' \
	'ball/reactor/*' \
	'ball/rift/*' \
	'ball/saturn/*' \
	'ball/snowglobe/*' \
	'ball/sootsprite/*' \
	'ball/ufo/*' \
	'map-easy/*' \
	'map-fwp/*' \
	'map-hard/*' \
	'map-medium/*' \
	'map-misc/*' \
	'map-mym/*' \
	'map-mym2/*' \
	'shot-easy/*' \
	'shot-fwp/*' \
	'shot-hard/*' \
	'shot-medium/*' \
	'shot-misc/*' \
	'shot-mym/*' \
	'shot-mym2/*' \
	'shot-tones/*' \

LDFLAGS := $(GL4ES_DIR)/lib/libGL.a
EM_LDFLAGS := \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s FULL_ES2=1 \
	-s INVOKE_RUN=0 \
	-s NO_EXIT_RUNTIME=1 \
	-s EXPORTED_FUNCTIONS=_main,_push_user_event,_config_set \
	-s EXPORTED_RUNTIME_METHODS=callMain,ccall,cwrap \
	-s HTML5_SUPPORT_DEFERRING_USER_SENSITIVE_REQUESTS=0 \
	-s LLD_REPORT_UNDEFINED \
	-s FETCH=1 \
	-lidbfs.js \
	$(EM_PRELOAD) \
	--use-preload-cache

ifeq ($(BUILD), devel)
EM_LDFLAGS += -s ERROR_ON_WASM_CHANGES_AFTER_LINK -s WASM_BIGINT
endif

putt_SRCS := \
	putt/course.c \
	putt/game.c \
	putt/hole.c \
	putt/hud.c \
	putt/main.c \
	putt/st_all.c \
	putt/st_conf.c \
	share/array.c \
	share/audio.c \
	share/ball.c \
	share/base_config.c \
	share/base_image.c \
	share/binary.c \
	share/cmd.c \
	share/common.c \
	share/config.c \
	share/dir.c \
	share/fetch_emscripten.c \
	share/font.c \
	share/fs_common.c \
	share/fs_jpg.c \
	share/fs_ov.c \
	share/fs_png.c \
	share/fs_stdio.c \
	share/miniz.c \
	share/geom.c \
	share/glext.c \
	share/gui.c \
	share/hmd_null.c \
	share/image.c \
	share/joy.c \
	share/lang.c \
	share/list.c \
	share/log.c \
	share/mtrl.c \
	share/package.c \
	share/part.c \
	share/queue.c \
	share/solid_all.c \
	share/solid_base.c \
	share/solid_draw.c \
	share/solid_sim_sol.c \
	share/solid_vary.c \
	share/st_common.c \
	share/st_package.c \
	share/state.c \
	share/text.c \
	share/theme.c \
	share/tilt_null.c \
	share/vec3.c \
	share/video.c

putt_OBJS := $(putt_SRCS:.c=.emscripten.o)

%.emscripten.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $(EM_CFLAGS) $<

.PHONY: neverputt
neverputt: $(JSDIR)/neverputt.js

$(JSDIR)/neverputt.js: $(putt_OBJS) $(DATA_ZIP)
	$(CC) -o $@ $(putt_OBJS) $(CFLAGS) $(EM_CFLAGS) $(LDFLAGS) $(EM_LDFLAGS)

$(DATA_ZIP):
	cd data && zip -r ../$@ . -x $(DATA_EXCLUDE)

.PHONY: packages
packages: clean-packages
	$(MAKE) -f mk/packages.mk OUTPUT_DIR=$$(pwd)/js/packages

.PHONY: clean-packages
clean-packages:
	rm -rf $$(pwd)/js/packages

.PHONY: clean
clean:
	$(RM) $(putt_OBJS) $(JSDIR)/neverputt.js $(JSDIR)/neverputt.wasm $(JSDIR)/neverputt.data $(DATA_ZIP)

.PHONY: watch
watch:
	while true; do \
		$(MAKE) -f emscripten/putt.mk --no-print-directory --question || ( $(MAKE) -f emscripten/putt.mk --no-print-directory && echo '\e[32mok\e[0m' ); \
		sleep 1; \
	done