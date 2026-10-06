# Peanut-GB is pinned at d0bcca771c83a2638c93a9ae61f3d51f226dc905.
# LCD and audio are compiled out so the CPU runs unthrottled.
CC := cc
CFLAGS := -O3 -std=c11 -Wall -Wno-unused-function -Wno-unused-parameter -Ithird_party
OUTDIR := build

.PHONY: all clean

all: $(OUTDIR)/run_crypto_test

$(OUTDIR)/run_crypto_test: run.c third_party/peanut_gb.h
	mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ run.c

clean:
	rm -rf $(OUTDIR)
