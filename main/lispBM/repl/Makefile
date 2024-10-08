
LISPBM := ../

include $(LISPBM)/lispbm.mk

PLATFORM_INCLUDE = -I$(LISPBM)/platform/linux/include
PLATFORM_SRC     = $(LISPBM)/platform/linux/src/platform_mutex.c

CCFLAGS =  -g -O2 -Wall -Wconversion -Wsign-compare -pedantic -std=c11 

PICCFLAGS = -O2 -Wall -Wconversion -pedantic -std=c11

LISPBM_SRC += $(LISPBM_EVAL_CPS_SRC)

REPL_SRC= repl.c \
	  repl_exts.c


ifdef HEAP_VIS
	CCFLAGS += -DVISUALIZE_HEAP
endif

improved_closures: CCFLAGS += -m32 -DCLEAN_UP_CLOSURES
improved_closures: repl clean_cl.h

CCFLAGS_COV = -g -coverage -O0 -Wall -Wconversion -Wsign-compare -pedantic -std=c11 -m32

LIBS = -lpthread -lreadline -lhistory -lpng

all: CCFLAGS += -m32 
all: repl repl_cov

all64: 	CCFLAGS += -DLBM64
all64: repl

sdl: CCFLAGS += -DLBM64 -DWITH_SDL
sdl: LISPBM_FLAGS += -I/usr/include/SDL2 -lSDL2_image -lSDL2
sdl: LISPBM_SRC += lbm_sdl.c
sdl: repl

clean_cl.h: ./scripts/clean.lisp
	./repl --store_env="clean_cl.env" --src=./scripts/clean.lisp --terminate
	xxd -i clean_cl.env clean_cl.h

install: all
	mkdir -p ~/.local/bin
	cp repl ~/.local/bin/lbm

debug: CCFLAGS += -g
debug: repl

repl: $(REPL_SRC) $(LISPBM_SRC) $(LISPBM_DEPS)
	gcc $(CCFLAGS) $(LISPBM_SRC) $(PLATFORM_SRC) $(LISPBM_FLAGS) $(REPL_SRC) -o repl $(LISPBM_INC) $(PLATFORM_INCLUDE) $(LIBS)

repl_cov: $(REPL_SRC) $(LISPBM_SRC) $(LISPBM_DEPS)
	gcc $(CCFLAGS_COV) $(LISPBM_SRC) $(PLATFORM_SRC) $(LISPBM_FLAGS) $(REPL_SRC) -o repl_cov $(LISPBM_INC) $(PLATFORM_INCLUDE) $(LIBS)

pirepl: $(REPL_SRC) $(LISPBM_SRC) $(LISPBM_DEPS)
	gcc $(PICCFLAGS) $(LISPBM_SRC) $(PLATFORM_SRC) $(LISPBM_FLAGS) $(REPL_SRC) -o repl $(LISPBM_INC) $(PLATFORM_INCLUDE) $(LIBS)

clean:
	rm -f repl
	rm -f repl_cov
	rm -f *.gcda
	rm -f *.gcno
