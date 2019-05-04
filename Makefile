BUILD_DIR = ./build
TOOLS_DIR = ./tools
BUILD2_DIR = ./build
INCLUDE_DIR = .
CFLAGS = -g -lpthread --std=gnu99 -m64
CC = gcc

TOOLS_SRC = $(wildcard ${TOOLS_DIR}/*.c)
TOOLS_BIN = $(patsubst %.c,${BUILD2_DIR}/%,$(notdir ${TOOLS_SRC})) 

all: create_dir  ${TOOLS_BIN}

create_dir:
	mkdir -p build

${BUILD_DIR}/pcow.o: pcow.c
	$(CC) $(CFLAGS) -c pcow.c -o ${BUILD_DIR}/pcow.o

${BUILD_DIR}/meta.o: meta.c
	$(CC) $(CFLAGS) -c meta.c -o ${BUILD_DIR}/meta.o

${BUILD_DIR}/manager.o: manager.c
	$(CC) $(CFLAGS) -c manager.c -o ${BUILD_DIR}/manager.o

${BUILD_DIR}/profiler.o: profiler.c
	$(CC) $(CFLAGS) -c profiler.c -o ${BUILD_DIR}/profiler.o

${BUILD_DIR}/cow.o: cow.c
	$(CC) $(CFLAGS) -c cow.c -o ${BUILD_DIR}/cow.o


${BUILD2_DIR}/%: ${TOOLS_DIR}/%.c ${BUILD_DIR}/pcow.o ${BUILD_DIR}/meta.o ${BUILD_DIR}/manager.o ${BUILD_DIR}/profiler.o ${BUILD_DIR}/cow.o
	$(CC) $(CFLAGS) -I${INCLUDE_DIR} -o $@ $< ${BUILD_DIR}/pcow.o ${BUILD_DIR}/meta.o ${BUILD_DIR}/manager.o ${BUILD_DIR}/profiler.o ${BUILD_DIR}/cow.o

clean:
	rm -f ${BUILD_DIR}/*
