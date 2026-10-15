/*
 * bp.h - min-sum belief propagation decoder, plain C, no heap.
 *
 * C port of bp_cpp (BP_Decoder in src/bp.cpp). The arithmetic and the order
 * of every floating-point operation are kept the same as the C++ version so
 * that both produce the same numbers.
 *
 * The parity check matrix is not passed in at run time. It is compiled in
 * from pcm_data.h, which make_header.py generates from H_matrix.txt.
 */
#ifndef BP_H
#define BP_H
 
#include <stdint.h>
#include "pcm_data.h"
 
/* Scaling factor applied to check-to-bit messages. 1.0f = plain min-sum,
 * which is what the C++ version uses. */
#ifndef BP_ALPHA
#define BP_ALPHA 1.0f
#endif
 
typedef struct {
    int converged;   /* 1 if the estimated error reproduces the syndrome */
    int iterations;  /* iterations actually run                          */
} bp_result_t;
 
/*
 * Run BP for one syndrome.
 *
 *   syndrome        PCM_NUM_CHECKS entries, each 0 or 1
 *   channel_llr     PCM_NUM_BITS entries, log((1-p)/p) for each bit
 *                   (pcm_channel_llr from pcm_data.h is the usual argument)
 *   max_iterations  iteration limit
 *
 * Results are left in bp_llrs[] and bp_error[] until the next call.
 */
bp_result_t bp_decode(const uint8_t *syndrome,
                      const float *channel_llr,
                      int max_iterations);
 
/* Outputs of the most recent bp_decode() call. */
extern float   bp_llrs[PCM_NUM_BITS];   /* posterior LLR per bit  (final_llrs) */
extern uint8_t bp_error[PCM_NUM_BITS];  /* hard decision per bit  (e_BP)       */
 
#endif /* BP_H */