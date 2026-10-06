#define ENABLE_LCD 0
#define ENABLE_SOUND 0
#include "peanut_gb.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Run one crypto_test.gb test as fast as the host CPU allows.
 *
 * SRAM byte 0 is the test id. SRAM byte 1 is 0x5A so the ROM can tell
 * the battery RAM window is actually readable.
 *
 * stdout is one machine-readable record. Progress goes to stderr.
 */

#define SRAM_SIZE 0x2000
#define DMG_CYCLES_PER_FRAME 70224.0
#define GBC_DOUBLE_HZ 8388608.0

struct ctx {
    uint8_t *rom;
    size_t rom_size;
    uint8_t sram[SRAM_SIZE];
    int opcode_errors;
};

static double seconds_since(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) +
           (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    struct ctx *ctx = gb->direct.priv;
    if (addr >= ctx->rom_size) {
        return 0xFF;
    }
    return ctx->rom[addr];
}

static uint8_t ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    struct ctx *ctx = gb->direct.priv;
    if (addr >= SRAM_SIZE) {
        return 0xFF;
    }
    return ctx->sram[addr];
}

static void ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
    struct ctx *ctx = gb->direct.priv;
    if (addr < SRAM_SIZE) {
        ctx->sram[addr] = val;
    }
}

static void on_error(struct gb_s *gb, const enum gb_error_e code, const uint16_t addr) {
    struct ctx *ctx = gb->direct.priv;
    if (code == GB_INVALID_OPCODE) {
        ctx->opcode_errors++;
    }
    fprintf(stderr, "emu error %d at %04X pc %04X bank %u\n",
            (int)code, addr, gb->cpu_reg.pc.reg, gb->selected_rom_bank);
}

static int load_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *buf;

    if (!f) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    len = ftell(f);
    if (len < 0x150) {
        fprintf(stderr, "%s is too small to be a ROM\n", path);
        fclose(f);
        return -1;
    }
    rewind(f);
    buf = malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "short read %s\n", path);
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)len;
    return 0;
}

/* GBDK writes `DEF _result 0xC0B1` into the .noi file. */
static int find_symbol(const char *path, const char *name, unsigned *addr_out) {
    FILE *f = fopen(path, "r");
    char line[256];
    char sym[128];
    unsigned addr;

    if (!f) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "DEF %127s %x", sym, &addr) == 2 && strcmp(sym, name) == 0) {
            fclose(f);
            *addr_out = addr;
            return 0;
        }
    }

    fclose(f);
    fprintf(stderr, "symbol %s not in %s\n", name, path);
    return -1;
}

static uint8_t read8(struct gb_s *gb, unsigned addr) {
    return __gb_read(gb, (uint16_t)addr);
}

static uint16_t read16(struct gb_s *gb, unsigned addr) {
    return (uint16_t)read8(gb, addr) | ((uint16_t)read8(gb, addr + 1) << 8);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s <crypto_test.gb> <symbols.noi> <test_id 1-4> [timeout_sec]\n"
            "  1  SHA-512(\"abc\")\n"
            "  2  secp256k1 Gx^2 mod p\n"
            "  3  PBKDF2-HMAC-SHA512, 1 iteration\n"
            "  4  full abandon mnemonic to three addresses\n",
            argv0);
}

int main(int argc, char **argv) {
    struct ctx ctx;
    struct gb_s gb;
    enum gb_init_error_e init_err;
    unsigned result_addr = 0;
    unsigned ticks_addr = 0;
    unsigned test_id;
    unsigned timeout_sec;
    uint64_t frames = 0;
    struct timespec start;
    double last_report;
    double host_sec;
    double gbc_sec;
    uint8_t magic;
    uint8_t test;
    unsigned len;
    int status = 0;
    char *end = NULL;

    if (argc < 4 || argc > 5) {
        usage(argv[0]);
        return 1;
    }

    test_id = (unsigned)strtoul(argv[3], &end, 10);
    if (!end || *end || test_id < 1 || test_id > 4) {
        usage(argv[0]);
        return 1;
    }
    timeout_sec = (argc == 5) ? (unsigned)strtoul(argv[4], NULL, 10) : 60;

    memset(&ctx, 0, sizeof ctx);
    if (load_file(argv[1], &ctx.rom, &ctx.rom_size) != 0) {
        return 1;
    }
    if (find_symbol(argv[2], "_result", &result_addr) != 0 ||
        find_symbol(argv[2], "_ticks", &ticks_addr) != 0) {
        free(ctx.rom);
        return 1;
    }
    if (result_addr < 0xC000 || result_addr >= 0xE000) {
        fprintf(stderr, "_result at %04X is outside WRAM\n", result_addr);
        free(ctx.rom);
        return 1;
    }

    ctx.sram[0] = (uint8_t)test_id;
    ctx.sram[1] = 0x5A;

    init_err = gb_init(&gb, rom_read, ram_read, ram_write, on_error, &ctx);
    if (init_err != GB_INIT_NO_ERROR) {
        fprintf(stderr, "gb_init failed (%d). cart %02X romsize %02X ramsize %02X\n",
                (int)init_err, ctx.rom[0x147], ctx.rom[0x148], ctx.rom[0x149]);
        free(ctx.rom);
        return 1;
    }

    fprintf(stderr, "test %u  _result=%04X _ticks=%04X  timeout %us\n",
            test_id, result_addr, ticks_addr, timeout_sec);

    clock_gettime(CLOCK_MONOTONIC, &start);
    last_report = 0.0;
    magic = 0;

    while (magic != 0xA5) {
        gb_run_frame(&gb);
        frames++;
        magic = read8(&gb, result_addr);

        if (ctx.opcode_errors) {
            status = 4;
            break;
        }

        host_sec = seconds_since(&start);
        if (host_sec >= (double)timeout_sec) {
            status = 2;
            break;
        }
        if (host_sec - last_report >= 2.0) {
            gbc_sec = ((double)frames * DMG_CYCLES_PER_FRAME) / GBC_DOUBLE_HZ;
            fprintf(stderr,
                    "frames %llu  ticks %u  magic %02X  gbc_est %.1fs  host %.1fs\n",
                    (unsigned long long)frames,
                    read16(&gb, ticks_addr),
                    magic,
                    gbc_sec,
                    host_sec);
            last_report = host_sec;
        }
    }

    host_sec = seconds_since(&start);
    gbc_sec = ((double)frames * DMG_CYCLES_PER_FRAME) / GBC_DOUBLE_HZ;
    test = read8(&gb, result_addr + 1);
    len = read16(&gb, result_addr + 2);

    if (magic != 0xA5) {
        fprintf(stderr, "stopped magic %02X pc %04X bank %u\n",
                magic, gb.cpu_reg.pc.reg, gb.selected_rom_bank);
        if (status == 0) {
            status = 2;
        }
    } else if (test == 0xEE) {
        fprintf(stderr, "ROM could not read SRAM\n");
        status = 3;
    } else if (test != test_id) {
        fprintf(stderr, "ROM ran test %u, wanted %u\n", test, test_id);
        status = 3;
    }

    if (len > 250) {
        len = 250;
    }

    printf("ok %d\n", status == 0 ? 1 : 0);
    printf("status %d\n", status);
    printf("test %u\n", test);
    printf("frames %llu\n", (unsigned long long)frames);
    printf("host_sec %.3f\n", host_sec);
    printf("gbc_sec %.3f\n", gbc_sec);
    printf("ticks %u\n", read16(&gb, ticks_addr));
    printf("len %u\n", len);
    printf("hex ");
    for (unsigned i = 0; i < len; i++) {
        printf("%02x", read8(&gb, result_addr + 4 + i));
    }
    printf("\n");

    free(ctx.rom);
    return status;
}
