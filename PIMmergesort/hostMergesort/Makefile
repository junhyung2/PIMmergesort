DPU_DIR := dpu
HOST_DIR := host
BUILDDIR ?= bin
NR_TASKLETS ?= 16
BL ?= 6
NR_DPUS ?= 1
VERSION ?= SINGLE
SYNC ?= HAND
TYPE ?= INT32
ENERGY ?= 0
PERF ?= 0

define conf_filename
	${BUILDDIR}/.NR_DPUS_$(1)_NR_TASKLETS_$(2)_BL_$(3)_VERSION_$(4)_SYNC_$(5)_TYPE_$(6).conf
endef
CONF := $(call conf_filename,${NR_DPUS},${NR_TASKLETS},${BL},${VERSION},${SYNC},${TYPE})

HOST_TARGET := ${BUILDDIR}/host_code
DPU_TARGET := ${BUILDDIR}/dpu_code

COMMON_INCLUDES := support
HOST_SOURCES := $(wildcard ${HOST_DIR}/*.c)
DPU_SOURCES := $(wildcard ${DPU_DIR}/*.c)

.PHONY: all clean test

__dirs := $(shell mkdir -p ${BUILDDIR})

COMMON_FLAGS := -w -I${COMMON_INCLUDES}
HOST_FLAGS := ${COMMON_FLAGS} -std=c11 -O3 `dpu-pkg-config --cflags --libs dpu` -DNR_TASKLETS=${NR_TASKLETS} -DNR_DPUS=${NR_DPUS} -DBL=${BL} -D${VERSION} -D${SYNC} -D${TYPE} -DENERGY=${ENERGY} -DPERF=${PERF}
DPU_FLAGS := ${COMMON_FLAGS} -O2 -DNR_TASKLETS=${NR_TASKLETS} -DBL=${BL} -D${VERSION} -D${SYNC} -D${TYPE} -DPERF=${PERF}

all: ${HOST_TARGET} ${DPU_TARGET}

${CONF}:
	$(RM) $(call conf_filename,*,*)
	touch ${CONF}

${HOST_TARGET}: ${HOST_SOURCES} ${COMMON_INCLUDES} ${CONF}
	$(CC) -o $@ ${HOST_SOURCES} ${HOST_FLAGS}
	$(CC) -S -o ${HOST_TARGET}.S ${HOST_SOURCES} ${HOST_FLAGS}

${DPU_TARGET}: ${DPU_SOURCES} ${COMMON_INCLUDES} ${CONF}
	dpu-upmem-dpurte-clang ${DPU_FLAGS} -o $@ ${DPU_SOURCES}
	dpu-upmem-dpurte-clang -S ${DPU_FLAGS} -o ${DPU_TARGET}.S ${DPU_SOURCES}

clean:
	$(RM) -r $(BUILDDIR)

test: all
	./${HOST_TARGET} -w 0 -e 1 -i 512 -x 1
