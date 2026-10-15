/*
 * main.c - run the BP decoder once, print the result, then time it.
 *
 * Builds two ways from the same source:
 *
 *   On the MicroBlaze V (Vitis application project): add bp.c, bp.h, main.c
 *   and pcm_data.h to the project's src folder and build. No extra settings.
 *   Output goes to the UART through xil_printf.
 *
 *   On a laptop, to check the numbers before going near the board:
 *       gcc -O2 -DBP_HOST -o bp_host main.c bp.c && ./bp_host
 *
 * Everything except the "timing" lines should be identical between the two.
 * The "checksum" line covers every output bit, so comparing that one number
 * is enough to know the board computed exactly what the laptop did.
 */
#include <stdint.h>
#include <string.h>
 
#include "bp.h"
#include "pcm_data.h"
 
/* How many times to repeat the decode for the timing average. */
#ifndef BP_TIMING_RUNS
#define BP_TIMING_RUNS 10
#endif
 
/* How many leading values of each output array to print. */
#ifndef BP_PRINT_VALUES
#define BP_PRINT_VALUES 10
#endif
 
/* ------------------------------------------------------------------------
 * Platform layer: printing and a tick counter.
 * ---------------------------------------------------------------------- */
#ifdef BP_HOST
 
#include <stdio.h>
#include <time.h>
 
#define PRINT printf
#define NL "\n"
#define PLATFORM_NAME "laptop build"
#define TICK_NAME "ns"
#define TICKS_PER_SEC 1000000000ull
#define TICK_MASK 0xFFFFFFFFFFFFFFFFull
#define TICK_RATE_NOTE ""
 
static uint64_t now_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
 
#elif defined(__riscv)
 
#include "xil_printf.h"
#include "xparameters.h"
 
#define PRINT xil_printf
#define NL "\r\n"
#define PLATFORM_NAME "RISC-V bare-metal build"
#define TICK_NAME "clock cycles"
#define TICK_MASK 0xFFFFFFFFull
 
/* The RISC-V "time" counter. On MicroBlaze V it counts processor clock
 * cycles; AMD's own sleep() for MicroBlaze V is built on the same counter.
 * Only the low 32 bits are read, so one measured interval must be shorter
 * than 2^32 cycles (about 43 s at 100 MHz). */
static uint64_t now_ticks(void)
{
    unsigned long v;
    __asm__ __volatile__("rdtime %0" : "=r"(v));
    return (uint64_t)(uint32_t)v;
}
 
/* Processor clock in Hz, used only to convert cycles to microseconds. */
#if defined(BP_CPU_HZ)
#define TICKS_PER_SEC ((uint64_t)(BP_CPU_HZ))
#define TICK_RATE_NOTE ""
#elif defined(XPAR_CPU_CORE_CLOCK_FREQ_HZ)
#define TICKS_PER_SEC ((uint64_t)(XPAR_CPU_CORE_CLOCK_FREQ_HZ))
#define TICK_RATE_NOTE ""
#else
#define TICKS_PER_SEC 100000000ull
#define TICK_RATE_NOTE " (clock ASSUMED 100 MHz: define BP_CPU_HZ to your clock)"
#endif
 
#else
#error "No tick counter for this processor. Build with -DBP_HOST on a laptop, or add one here."
#endif
 
/* ------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
 
/* Print a float with three decimals using integer formatting only, because
 * xil_printf has no floating-point support. */
static void print_fixed3(float x)
{
    int neg;
    float a;
    uint32_t twice, milli;
 
    if (x != x) {
        PRINT("nan");
        return;
    }
    neg = (x < 0.0f);
    a = neg ? -x : x;
    if (a > 1.0e6f) {
        PRINT("%s", neg ? "-big" : "+big");
        return;
    }
    twice = (uint32_t)(a * 2000.0f);
    milli = (twice + 1u) / 2u;
    PRINT("%s%u.%03u", (neg && milli != 0u) ? "-" : "",
          (unsigned)(milli / 1000u), (unsigned)(milli % 1000u));
}
 
static uint32_t fnv1a_byte(uint32_t h, uint32_t byte)
{
    return (h ^ (byte & 0xFFu)) * 16777619u;
}
 
/* One number that depends on every output bit of the last decode. */
static uint32_t result_checksum(const bp_result_t *res)
{
    uint32_t h = 2166136261u;
    int b, k;
 
    h = fnv1a_byte(h, (uint32_t)res->converged);
    h = fnv1a_byte(h, (uint32_t)res->iterations);
    h = fnv1a_byte(h, (uint32_t)res->iterations >> 8);
    for (b = 0; b < PCM_NUM_BITS; ++b) {
        uint32_t bits;
        memcpy(&bits, &bp_llrs[b], sizeof bits);
        if (bp_llrs[b] != bp_llrs[b]) {
            bits = 0x7FC00000u;   /* all NaNs count the same on every processor */
        }
        for (k = 0; k < 4; ++k) {
            h = fnv1a_byte(h, bits >> (8 * k));
        }
        h = fnv1a_byte(h, bp_error[b]);
    }
    return h;
}
 
static uint32_t clamp_u32(uint64_t v)
{
    return (v > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)v;
}
 
/* ------------------------------------------------------------------------ */
 
int main(void)
{
    bp_result_t res;
    uint32_t checksum, weight = 0;
    uint64_t t0, t1, total = 0, avg, avg_ns;
    int b, r, n_print, consistent = 1;
 
    PRINT("Hello from the BP decoder (" PLATFORM_NAME ")" NL);
    PRINT("code: %s" NL, PCM_NAME);
    PRINT("size: %d checks, %d bits, %d edges, max %d iterations" NL,
          PCM_NUM_CHECKS, PCM_NUM_BITS, PCM_NUM_EDGES, PCM_MAX_ITER);
 
    /* ---- one decode, results printed ---------------------------------- */
    res = bp_decode(pcm_syndrome, pcm_channel_llr, PCM_MAX_ITER);
    checksum = result_checksum(&res);
 
    for (b = 0; b < PCM_NUM_BITS; ++b) {
        weight += bp_error[b];
    }
    n_print = (PCM_NUM_BITS < BP_PRINT_VALUES) ? PCM_NUM_BITS : BP_PRINT_VALUES;
 
    PRINT("converged: %d" NL, res.converged);
    PRINT("iterations: %d" NL, res.iterations);
    PRINT("error weight: %u" NL, (unsigned)weight);
    PRINT("first %d e_BP values:", n_print);
    for (b = 0; b < n_print; ++b) {
        PRINT(" %d", (int)bp_error[b]);
    }
    PRINT(NL);
    PRINT("first %d final LLRs:", n_print);
    for (b = 0; b < n_print; ++b) {
        PRINT(" ");
        print_fixed3(bp_llrs[b]);
    }
    PRINT(NL);
    PRINT("checksum: 0x%08x" NL, (unsigned)checksum);
 
    /* ---- timing -------------------------------------------------------- */
    for (r = 0; r < BP_TIMING_RUNS; ++r) {
        bp_result_t again;
 
        t0 = now_ticks();
        again = bp_decode(pcm_syndrome, pcm_channel_llr, PCM_MAX_ITER);
        t1 = now_ticks();
        total += (t1 - t0) & TICK_MASK;
 
        if (result_checksum(&again) != checksum) {
            consistent = 0;
        }
    }
 
    avg = total / (uint64_t)BP_TIMING_RUNS;
    avg_ns = (avg * 1000000000ull) / TICKS_PER_SEC;
 
    PRINT("timing: %d runs, average %u " TICK_NAME " per decode" NL,
          BP_TIMING_RUNS, (unsigned)clamp_u32(avg));
    PRINT("timing: %u.%03u us per decode, %u.%03u us per iteration" TICK_RATE_NOTE NL,
          (unsigned)clamp_u32(avg_ns / 1000u), (unsigned)(avg_ns % 1000u),
          (unsigned)clamp_u32(avg_ns / (uint64_t)res.iterations / 1000u),
          (unsigned)((avg_ns / (uint64_t)res.iterations) % 1000u));
    PRINT("repeat runs gave the same result: %s" NL, consistent ? "yes" : "NO");
    PRINT("done" NL);
 
    return 0;
}