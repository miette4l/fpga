/*
 * bp.c - min-sum belief propagation decoder, plain C, no heap.
 *
 * Serial (layered) schedule: one sweep per iteration, over checks in CSR
 * order. Each check reads and updates the shared bit beliefs in place, so
 * later checks in the same sweep already see earlier checks' new messages.
 * This matches BP_Decoder::run_bp() in the C++ version edge for edge (see
 * bp_cpp/src/bp.cpp), not the two-phase flooding schedule a textbook BP
 * decoder usually uses.
 *
 * See bp.h.
 */
#include "bp.h"

/* Working storage. Fixed size, so nothing is allocated at run time. */
static float msgs[PCM_NUM_EDGES];  /* check-to-bit messages, CSR edge order */

float   bp_llrs[PCM_NUM_BITS];     /* running bit beliefs, then the result */
uint8_t bp_error[PCM_NUM_BITS];

/* |x| without needing the maths library. The compiler emits this inline. */
#define BP_FABSF(x) __builtin_fabsf(x)
#define BP_INFINITY __builtin_inff()

static void hard_decision(void)
{
    int b;

    for (b = 0; b < PCM_NUM_BITS; ++b) {
        /* Non-positive LLR means an error on this bit is likely. */
        bp_error[b] = (bp_llrs[b] <= 0.0f) ? 1u : 0u;
    }
}

/* Returns 1 if H * bp_error (mod 2) equals the syndrome. */
static int termination_check(const uint8_t *s)
{
    int c, edge;

    for (c = 0; c < PCM_NUM_CHECKS; ++c) {
        const int start = pcm_csr_ptr[c];
        const int end   = pcm_csr_ptr[c + 1];
        unsigned parity = 0;

        for (edge = start; edge < end; ++edge) {
            parity ^= bp_error[pcm_csr_idx[edge]];
        }
        if (parity != (unsigned)(s[c] != 0)) {
            return 0;
        }
    }
    return 1;
}

/* One pass over every check, in CSR order. For each check, every incident
 * bit's belief is first reduced to its extrinsic value (removing this
 * check's own last message), the two smallest magnitudes and the combined
 * sign are found over those extrinsic values, and then a new message is
 * sent back to each bit, excluding its own edge, and folded straight into
 * that bit's belief so the next check in this same sweep already sees it. */
static void check_sweep(const uint8_t *s, float alpha)
{
    int c, edge;

    for (c = 0; c < PCM_NUM_CHECKS; ++c) {
        const int start = pcm_csr_ptr[c];
        const int end   = pcm_csr_ptr[c + 1];

        if (end == start) {
            continue;
        }

        /* A check with a single neighbour constrains nothing on its own;
         * clear its message and leave the bit's belief as-is. */
        if (end - start == 1) {
            const int   bit = pcm_csr_idx[start];
            const float t   = bp_llrs[bit] - msgs[start];

            msgs[start]  = 0.0f;
            bp_llrs[bit] = t;
            continue;
        }

        {
            const float syndrome_factor = (s[c] == 0) ? 1.0f : -1.0f;
            float m1 = BP_INFINITY;
            float m2 = BP_INFINITY;
            int   argmin_edge = -1;
            int   neg_count = 0;

            for (edge = start; edge < end; ++edge) {
                const int   bit   = pcm_csr_idx[edge];
                const float t     = bp_llrs[bit] - msgs[edge];
                const float abs_t = BP_FABSF(t);

                if (t < 0.0f) {
                    ++neg_count;
                }
                if (abs_t < m1) {
                    m2 = m1;
                    m1 = abs_t;
                    argmin_edge = edge;
                } else if (abs_t < m2) {
                    m2 = abs_t;
                }
            }

            {
                const float total_sign = (neg_count & 1) ? -1.0f : 1.0f;

                for (edge = start; edge < end; ++edge) {
                    const int   bit        = pcm_csr_idx[edge];
                    const float t          = bp_llrs[bit] - msgs[edge];
                    const float bit_sign   = (t >= 0.0f) ? 1.0f : -1.0f;
                    const float sign_excl  = total_sign * bit_sign;
                    const float min_excl   = (edge == argmin_edge) ? m2 : m1;
                    const float new_msg    = syndrome_factor * alpha * sign_excl * min_excl;

                    msgs[edge]   = new_msg;
                    bp_llrs[bit] = t + new_msg;
                }
            }
        }
    }
}

bp_result_t bp_decode(const uint8_t *syndrome,
                      const float *channel_llr,
                      int max_iterations)
{
    bp_result_t res;
    int iter, b;

    res.converged  = 0;
    res.iterations = max_iterations;

    for (b = 0; b < PCM_NUM_BITS; ++b) {
        bp_llrs[b] = channel_llr[b];
    }
    for (b = 0; b < PCM_NUM_EDGES; ++b) {
        msgs[b] = 0.0f;
    }

    for (iter = 0; iter < max_iterations; ++iter) {
        check_sweep(syndrome, BP_ALPHA);
        hard_decision();

        if (termination_check(syndrome)) {
            res.converged  = 1;
            res.iterations = iter + 1;
            return res;
        }
    }
    return res;
}
