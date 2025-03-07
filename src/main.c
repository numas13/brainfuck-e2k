// SPDX-License-Identifier: GPL-3.0-only

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>
#include <libgen.h>
#include <getopt.h>
#include <time.h>
#include "bc.h"

#define TAPE_SIZE 30000
#define MAX_NESTING 100

typedef struct Stats {
    uint64_t adds;
    uint64_t movs;
    uint64_t beqz;
    uint64_t beqz_taken;
    uint64_t bnez;
    uint64_t bnez_taken;
    uint64_t calls;
    uint64_t cpu_cycles;
} Stats;

typedef enum Mode {
    MODE_ASM,
    MODE_BC,
    MODE_C,
    MODE_NAIVE,
} Mode;

typedef struct Options {
    Mode mode;
    bool dump;
    bool dump_only;
    bool time;
    bool stats;
    char **files;
} Options;

static void printf_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool parse_opts(int argc, char *argv[], Options *opts) {
    bool ret = true;

    memset(opts, 0, sizeof(*opts));

    for (;;) {
        int option_index = 0;
        static struct option long_options[] = {
            {"dump",        no_argument,       0, 'd'},
            {"dump-only",   no_argument,       0, 'D'},
            {"mode",        required_argument, 0, 'm'},
            {"stats",       no_argument,       0, 's'},
            {0,             0,                 0,  0 }
        };
        int c = getopt_long(argc, argv, "dDm:ts",
                long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 0:
            abort();
        case 'D':
            opts->dump_only = true;
            /* fallthrough */
        case 'd':
            opts->dump = true;
            break;
        case 't':
            opts->time = true;
            break;
        case 's':
            opts->stats = true;
            break;
        case 'm':
            if (strcmp(optarg, "asm") == 0) {
                opts->mode = MODE_ASM;
            } else if (strcmp(optarg, "bc") == 0) {
                opts->mode = MODE_BC;
            } else if (strcmp(optarg, "c") == 0) {
                opts->mode = MODE_C;
            } else if (strcmp(optarg, "naive") == 0) {
                opts->mode = MODE_NAIVE;
            } else {
                printf_err("invalid mode \"%s\"\n", optarg);
                ret = false;
            }
            break;
        default:
            ret = false;
            break;
        }
    }

    if (optind < argc) {
        opts->files = &argv[optind];
    } else {
        printf_err("usage: %s program.bf\n", argv[0]);
        ret = false;
    }

    return ret;
}

#ifdef __e2k__
void run_program_e2k(const int32_t *code, uint8_t *tape,
        size_t tape_size, Stats *stats);
#endif

static inline int32_t make_insn(uint8_t op, int32_t n) {
    return ((uint32_t) n << 6) | op;
}

static inline int32_t insn_imm(int32_t insn) {
    return insn >> 6;
}

static size_t load_program(const char *path, char **program) {
    FILE *f;
    size_t size;

    if (!(f = fopen(path, "r"))) {
        perror(path);
        exit(EXIT_FAILURE);
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    *program = (char *) malloc(size + 16);
    fseek(f, 0, SEEK_SET);
    size_t n = fread(*program, 1, size, f);
    (*program)[n] = 0;
    fclose(f);

    return size;
}

static int32_t* translate_program(char *p, size_t size) {
    int32_t loops[MAX_NESTING];
    int32_t *o;
    int32_t l, i = 0, nloops = 0;

    o = malloc(size * sizeof(o[0]));

    while (*p) {
        int32_t n = 0;
        char c_inc = *p, c_dec;
        int op;

        switch (c_inc) {
        case '[':
            loops[nloops++] = i++;
            ++p;
            break;
        case ']':
            l = loops[--nloops];
            o[l] = make_insn(OP_BEQZ, (i - l) * 4);
            o[i] = make_insn(OP_BNEZ, (l - i) * 4);
            ++i;
            ++p;
            break;
        case '-':
            c_inc = '+';
            /* fallthrough */
        case '+':
            c_dec = '-';
            op = OP_ADD;
            goto count_dec_inc;
        case '<':
            c_inc = '>';
            /* fallthrough */
        case '>':
            c_dec = '<';
            op = OP_MOV;
        count_dec_inc:
            for (; *p == c_inc || *p == c_dec || isspace(*p); ++p) {
                if (!isspace(*p)) {
                    n += *p == c_inc ? 1 : -1;
                }
            }
            if (n != 0) {
                o[i++] = make_insn(op, n);
            }
            break;
        case ',':
            o[i++] = make_insn(OP_CALL, FUNC_GETC);
            ++p;
            break;
        case '.':
            o[i++] = make_insn(OP_CALL, FUNC_PUTC);
            ++p;
            break;
#ifdef FUNC_DEBUG
        case '?':
            o[i++] = make_insn(OP_CALL, FUNC_DEBUG);
            ++p;
            break;
#endif
        default:
            ++p;
            break;
        }
    }

    o[i] = OP_END;
    return o;
}

static void dump_program(const char *path, const int32_t *code) {
    uint32_t pc = 0;

    printf_err("  Bytecode:\n");
    for (; code[pc]; ++pc) {
        int32_t n = insn_imm(code[pc]);

        printf_err(" %4u: ", pc);
        switch (code[pc] & OP_MASK) {
        case OP_BEQZ:
            printf_err("[%d", pc + n / 4 + 1);
            break;
        case OP_BNEZ:
            printf_err("]%d", pc + n / 4 + 1);
            break;
        case OP_ADD:
            printf_err("%c%d", n > 0 ? '+' : '-', abs(n));
            break;
        case OP_MOV:
            printf_err("%c%d", n > 0 ? '>' : '<', abs(n));
            break;
        case OP_CALL:
            switch (n) {
            case FUNC_GETC: printf_err(","); break;
            case FUNC_PUTC: printf_err("."); break;
#ifdef FUNC_DEBUG
            case FUNC_DEBUG: printf_err("?"); break;
#endif
            default: abort();
            }
            break;
        default:
            abort();
            break;
        }
        printf_err("\n");
    }
    printf_err("\n");
}

static void run_program_bc(const int32_t *code, uint8_t *tape,
        size_t tape_size, Stats *stats)
{
    uint32_t pc = 0, i = 0;
    uint8_t cur = tape[i];

    for (; code[pc]; ++pc) {
        int32_t n = insn_imm(code[pc]);

        switch (code[pc] & OP_MASK) {
        case OP_BEQZ:
            stats->beqz += 1;
            if (cur == 0) {
                pc += n / 4;
                stats->beqz_taken += 1;
            }
            break;
        case OP_BNEZ:
            stats->bnez += 1;
            if (cur != 0) {
                pc += n / 4;
                stats->bnez_taken += 1;
            }
            break;
        case OP_ADD:
            stats->adds += 1;
            cur += n;
            break;
        case OP_MOV:
            stats->movs += 1;
            tape[i] = cur;
            i += n;
            cur = tape[i];
            break;
        case OP_CALL:
            stats->calls += 1;
            switch (n) {
            case FUNC_GETC:
                cur = getchar();
                break;
            case FUNC_PUTC:
                putchar(cur);
                break;
#ifdef FUNC_DEBUG
            case FUNC_DEBUG:
                break;
#endif
            default:
                abort();
            }
            break;
        default:
            abort();
            break;
        }
    }
}

static void falign(FILE *out, int width) {
    for (int j = 0; j < width; j++) {
        fputc(' ', out);
        fputc(' ', out);
    }
}

#define afprintf(out, pad, ...) do { \
        falign(out, pad); \
        fprintf(out, __VA_ARGS__); \
    } while(0)

static void c_add(FILE *out, int pad, const char *v, int n) {
    if (n >= 0) {
        afprintf(out, pad, "%s += %d;\n", v, n);
    } else {
        afprintf(out, pad, "%s -= %d;\n", v, -n);
    }
}

static int strip_extension(char *s) {
    int size = strlen(s);
    for (int i = size; i--;) {
        if (s[i] == '.') {
            s[i] = 0;
            return i;
        }
    }
    return size;
}

static void run_program_c(const char *path, const int32_t *code, size_t tape_size)
{
    FILE *out;
    char *path_tmp, *name, *output, *cmd;
    int n, name_size, pad = 0;

    path_tmp = strdup(path);
    name = basename(path_tmp);
    name_size = strip_extension(name);
    n = name_size + 64;
    output = malloc(n);
    snprintf(output, n, "%s.c", name);
    out = fopen(output, "w");
    if (!out) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    afprintf(out, pad, "#include <stdio.h>\n");
    afprintf(out, pad, "#include <stdint.h>\n");
    afprintf(out,   0, "\n");
    afprintf(out, pad, "int main() {\n");
    pad += 1;
    afprintf(out, pad, "uint8_t tape[%lu] = { 0 };\n", tape_size);
    afprintf(out, pad, "int i = 0;\n");
    afprintf(out,   0, "\n");

    for (int pc = 0; code[pc]; ++pc) {
        int32_t n = insn_imm(code[pc]);

        switch (code[pc] & OP_MASK) {
        case OP_BEQZ:
            afprintf(out, pad, "while (tape[i] != 0) {\n");
            pad += 1;
            break;
        case OP_BNEZ:
            pad -= 1;
            afprintf(out, pad, "}\n");
            break;
        case OP_ADD:
            c_add(out, pad, "tape[i]", n);
            break;
        case OP_MOV:
            c_add(out, pad, "i", n);
            break;
        case OP_CALL:
            switch (n) {
            case FUNC_GETC:
                afprintf(out, pad, "tape[i] = getchar();\n");
                break;
            case FUNC_PUTC:
                afprintf(out, pad, "putchar(tape[i]);\n");
                break;
#ifdef FUNC_DEBUG
            case FUNC_DEBUG:
                break;
#endif
            default:
                abort();
            }
            break;
        default:
            abort();
            break;
        }
    }

    afprintf(out, pad, "return 0;\n");
    pad -= 1;
    afprintf(out, pad, "}\n");
    fclose(out);

    n = strlen(output) + strlen(name) + 128;
    cmd = malloc(n);
    snprintf(cmd, n, "cc -march=native -O3 %s -o %s", output, name);
    system(cmd);

    free(cmd);
    free(output);
    free(path_tmp);
}

static void run_program_naive(const char *program, uint8_t *tape, size_t tape_size) {
    // TODO:
}

int main(int argc, char *argv[], char *envp[]) {
    Options opts = { 0 };
    uint8_t tape[TAPE_SIZE];

    if (!parse_opts(argc, argv, &opts)) {
        exit(EXIT_FAILURE);
    }

    for (int i = 0; opts.files[i]; ++i) {
        char *path = opts.files[i];
        char *program = NULL;
        int32_t *code = NULL;
        size_t size;

        if (opts.dump || opts.time) {
            printf_err("%s\n", path);
        }

        size = load_program(path, &program);
        switch (opts.mode) {
        case MODE_ASM:
        case MODE_BC:
        case MODE_C:
            code = translate_program(program, size);
            if (opts.dump) {
                dump_program(path, code);
            }
            break;
        default:
            if (opts.dump) {
                printf_err("%s\n", program);
            }
            break;
        }

        if (!opts.dump_only) {
            Stats stats = { 0 };
            struct timespec start;

            memset(tape, 0, TAPE_SIZE);

            if (opts.time) {
                clock_gettime(CLOCK_MONOTONIC, &start);
            }

            switch (opts.mode) {
            case MODE_ASM:
            case MODE_BC:
#if __e2k__
                if (opts.mode == MODE_ASM) {
                    run_program_e2k(code, tape, TAPE_SIZE, &stats);
                } else
#endif
                {
                    run_program_bc(code, tape, TAPE_SIZE, &stats);
                }

                break;
            case MODE_C:
                run_program_c(path, code, TAPE_SIZE);
            case MODE_NAIVE:
                run_program_naive(program, tape, TAPE_SIZE);
                break;
            }

            if (opts.time) {
                struct timespec end;
                double time;
                const char *units;

                clock_gettime(CLOCK_MONOTONIC, &end);
                time = (unsigned long long) (end.tv_sec - start.tv_sec) * 1000000000ULL
                     + (end.tv_nsec - start.tv_nsec);
                if (time > 9000.0e6) {
                    time /= 1e9;
                    units = "s";
                } else {
                    time /= 1e6;
                    units = "ms";
                }

                printf_err("  Time: %.2f%s\n", time, units);
            }

            if (opts.stats) {
                uint64_t total = stats.adds + stats.movs + stats.beqz + stats.bnez + stats.calls;
                uint64_t branches = stats.beqz + stats.bnez;
                uint64_t branches_taken = stats.beqz_taken + stats.bnez_taken;

                printf_err("  Stats\n");
                printf_err("         ops: %" PRIu64 "\n", total);
                printf_err("        adds: %" PRIu64 "\n", stats.adds);
                printf_err("        movs: %" PRIu64 "\n", stats.movs);
                printf_err("        beqz: %" PRIu64 " (%.1f%% taken %" PRIu64 ")\n",
                        stats.beqz,
                        (double) stats.beqz_taken / stats.beqz * 100,
                        stats.beqz_taken);
                printf_err("        bnez: %" PRIu64 " (%.1f%% taken %" PRIu64 ")\n",
                        stats.bnez,
                        (double) stats.bnez_taken / stats.bnez * 100,
                        stats.bnez_taken);
                printf_err("    branches: %" PRIu64 " (%.1f%% taken %" PRIu64 ")\n",
                        branches,
                        (double) branches_taken / branches * 100,
                        branches_taken);
                printf_err("       calls: %" PRIu64 "\n", stats.calls);
                if (stats.cpu_cycles) {
                    printf_err("  cpu cycles: %" PRIu64 "\n", stats.cpu_cycles);
                }
            }
        }

        if (opts.dump || opts.time) {
            printf_err("\n");
        }

        free(code);
        free(program);
    }

    return EXIT_SUCCESS;
}

#ifdef FUNC_DEBUG
void debug(const int32_t *code, uint64_t pc, const uint8_t *tape,
        const uint8_t *cur, uint64_t acc)
{
    printf_err(" %4lu: acc=%lu, i=%lu\n", pc / 4, acc, (intptr_t) cur - (intptr_t) tape);
}
#endif
