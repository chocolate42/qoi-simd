CC ?= gcc
CFLAGS_BENCH ?= -std=gnu99 -O3 -llz4 -lzstd
LFLAGS_BENCH ?= -lpng $(LDFLAGS)
CFLAGS_CONV ?= -std=c99 -O3 -llz4 -lzstd
LFLAGS_CONV ?= $(LDFLAGS)

TARGET_BENCH ?= qoibench
TARGET_CONV ?= qoiconv
TARGET_BENCH_ROI ?= roibench
TARGET_CONV_ROI ?= roiconv
TARGET_BENCH_SOI ?= soibench
TARGET_CONV_SOI ?= soiconv

all: $(TARGET_BENCH) $(TARGET_CONV) $(TARGET_BENCH_ROI) $(TARGET_CONV_ROI) $(TARGET_BENCH_SOI) $(TARGET_CONV_SOI)

bench: $(TARGET_BENCH)
$(TARGET_BENCH):$(TARGET_BENCH).c qoi.h
	$(CC) $(CFLAGS_BENCH) $(CFLAGS) $(TARGET_BENCH).c -o $(TARGET_BENCH) $(LFLAGS_BENCH)

conv: $(TARGET_CONV)
$(TARGET_CONV):$(TARGET_CONV).c qoi.h
	$(CC) $(CFLAGS_CONV) $(CFLAGS) $(TARGET_CONV).c -o $(TARGET_CONV) $(LFLAGS_CONV)

bench-roi: $(TARGET_BENCH_ROI)
$(TARGET_BENCH_ROI):$(TARGET_BENCH).c roi.h
	$(CC) -DROI $(CFLAGS_BENCH) $(CFLAGS) $(TARGET_BENCH).c -o $(TARGET_BENCH_ROI) $(LFLAGS_BENCH)

conv-roi: $(TARGET_CONV_ROI)
$(TARGET_CONV_ROI):$(TARGET_CONV).c roi.h
	$(CC) -DROI $(CFLAGS_CONV) $(CFLAGS) $(TARGET_CONV).c -o $(TARGET_CONV_ROI) $(LFLAGS_CONV)

bench-soi: $(TARGET_BENCH_SOI)
$(TARGET_BENCH_SOI):$(TARGET_BENCH).c soi.h
	$(CC) -DSOI $(CFLAGS_BENCH) $(CFLAGS) $(TARGET_BENCH).c -o $(TARGET_BENCH_SOI) $(LFLAGS_BENCH)

conv-soi: $(TARGET_CONV_SOI)
$(TARGET_CONV_SOI):$(TARGET_CONV).c soi.h
	$(CC) -DSOI $(CFLAGS_CONV) $(CFLAGS) $(TARGET_CONV).c -o $(TARGET_CONV_SOI) $(LFLAGS_CONV)

.PHONY: clean
clean:
	$(RM) $(TARGET_BENCH) $(TARGET_CONV)
