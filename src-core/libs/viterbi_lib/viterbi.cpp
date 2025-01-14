/*
 * Copyright 1995 Phil Karn, KA9Q
 * Copyright 2008 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/* 
 * Viterbi decoder for K=7 rate=1/2 convolutional code
 * Some modifications from original Karn code by Matt Ettus
 */

#include "viterbi.h"
#include <stdio.h>

//define DEBUG0

/* The two generator polynomials for the NASA Standard K=7 code.
 * Since these polynomials are known to be optimal for this constraint
 * length there is not much point in changing them. But if you do, you
 * will have to regenerate the BUTTERFLY macro calls in viterbi()
 */
#define POLYA 0x4f
#define POLYB 0x6d

/* The basic Viterbi decoder operation, called a "butterfly"
 * operation because of the way it looks on a trellis diagram. Each
 * butterfly involves an Add-Compare-Select (ACS) operation on the two nodes
 * where the 0 and 1 paths from the current node merge at the next step of
 * the trellis.
 *
 * The code polynomials are assumed to have 1's on both ends. Given a
 * function encode_state() that returns the two symbols for a given
 * encoder state in the low two bits, such a code will have the following
 * identities for even 'n' < 64:
 *
 * 	encode_state(n) = encode_state(n+65)
 *	encode_state(n+1) = encode_state(n+64) = (3 ^ encode_state(n))
 *
 * Any convolutional code you would actually want to use will have
 * these properties, so these assumptions aren't too limiting.
 *
 * Doing this as a macro lets the compiler evaluate at compile time the
 * many expressions that depend on the loop index and encoder state and
 * emit them as immediate arguments.
 * This makes an enormous difference on register-starved machines such
 * as the Intel x86 family where evaluating these expressions at runtime
 * would spill over into memory.
 */
#define BUTTERFLY(i, sym)                                     \
  {                                                           \
    int m0, m1;                                               \
                                                              \
    /* ACS for 0 branch */                                    \
    m0 = state[i].metric + mets[sym];          /* 2*i */      \
    m1 = state[i + 32].metric + mets[3 ^ sym]; /* 2*i + 64 */ \
    if (m0 > m1)                                              \
    {                                                         \
      next[2 * i].metric = m0;                                \
      next[2 * i].path = state[i].path << 1;                  \
    }                                                         \
    else                                                      \
    {                                                         \
      next[2 * i].metric = m1;                                \
      next[2 * i].path = (state[i + 32].path << 1) | 1;       \
    }                                                         \
    /* ACS for 1 branch */                                    \
    m0 = state[i].metric + mets[3 ^ sym];  /* 2*i + 1 */      \
    m1 = state[i + 32].metric + mets[sym]; /* 2*i + 65 */     \
    if (m0 > m1)                                              \
    {                                                         \
      next[2 * i + 1].metric = m0;                            \
      next[2 * i + 1].path = state[i].path << 1;              \
    }                                                         \
    else                                                      \
    {                                                         \
      next[2 * i + 1].metric = m1;                            \
      next[2 * i + 1].path = (state[i + 32].path << 1) | 1;   \
    }                                                         \
  }

extern unsigned char Partab[]; /* Parity lookup table */

/* Convolutionally encode data into binary symbols */
unsigned char encode(unsigned char *symbols,
                     unsigned char *data,
                     unsigned int nbytes,
                     unsigned char encstate)
{
  int i;

  while (nbytes-- != 0)
  {
    for (i = 7; i >= 0; i--)
    {
      encstate = (encstate << 1) | ((*data >> i) & 1);
      *symbols++ = Partab[encstate & POLYA];
      *symbols++ = Partab[encstate & POLYB];
    }
    data++;
  }

  return encstate;
}

/* Viterbi decoder */
int viterbi(unsigned long *metric,  /* Final path metric (returned value) */
            unsigned char *data,    /* Decoded output data */
            unsigned char *symbols, /* Raw deinterleaved input symbols */
            unsigned int nbits,     /* Number of output bits */
            int mettab[2][256]      /* Metric table, [sent sym][rx symbol] */
)
{
  unsigned int bitcnt = 0;
  int mets[4];
  long bestmetric;
  int beststate, i;
  struct viterbi_state state0[64], state1[64], *state, *next;

  state = state0;
  next = state1;

  /* Initialize starting metrics to prefer 0 state */
  state[0].metric = 0;
  for (i = 1; i < 64; i++)
    state[i].metric = -999999;
  state[0].path = 0;

  for (bitcnt = 0; bitcnt < nbits; bitcnt++)
  {
    /* Read input symbol pair and compute all possible branch
     * metrics
     */
    mets[0] = mettab[0][symbols[0]] + mettab[0][symbols[1]];
    mets[1] = mettab[0][symbols[0]] + mettab[1][symbols[1]];
    mets[2] = mettab[1][symbols[0]] + mettab[0][symbols[1]];
    mets[3] = mettab[1][symbols[0]] + mettab[1][symbols[1]];
    symbols += 2;

    /* These macro calls were generated by genbut.c 
   
    BUTTERFLY(0,0);
    BUTTERFLY(1,1);
    BUTTERFLY(2,3);
    BUTTERFLY(3,2);
    BUTTERFLY(4,3);
    BUTTERFLY(5,2);
    BUTTERFLY(6,0);
    BUTTERFLY(7,1);
    BUTTERFLY(8,0);
    BUTTERFLY(9,1);
    BUTTERFLY(10,3);
    BUTTERFLY(11,2);
    BUTTERFLY(12,3);
    BUTTERFLY(13,2);
    BUTTERFLY(14,0);
    BUTTERFLY(15,1);
    BUTTERFLY(16,2);
    BUTTERFLY(17,3);
    BUTTERFLY(18,1);
    BUTTERFLY(19,0);
    BUTTERFLY(20,1);
    BUTTERFLY(21,0);
    BUTTERFLY(22,2);
    BUTTERFLY(23,3);
    BUTTERFLY(24,2);
    BUTTERFLY(25,3);
    BUTTERFLY(26,1);
    BUTTERFLY(27,0);
    BUTTERFLY(28,1);
    BUTTERFLY(29,0);
    BUTTERFLY(30,2);
    BUTTERFLY(31,3);  */

    BUTTERFLY(0, 0);
    BUTTERFLY(1, 2);
    BUTTERFLY(2, 3);
    BUTTERFLY(3, 1);
    BUTTERFLY(4, 3);
    BUTTERFLY(5, 1);
    BUTTERFLY(6, 0);
    BUTTERFLY(7, 2);
    BUTTERFLY(8, 0);
    BUTTERFLY(9, 2);
    BUTTERFLY(10, 3);
    BUTTERFLY(11, 1);
    BUTTERFLY(12, 3);
    BUTTERFLY(13, 1);
    BUTTERFLY(14, 0);
    BUTTERFLY(15, 2);
    BUTTERFLY(16, 1);
    BUTTERFLY(17, 3);
    BUTTERFLY(18, 2);
    BUTTERFLY(19, 0);
    BUTTERFLY(20, 2);
    BUTTERFLY(21, 0);
    BUTTERFLY(22, 1);
    BUTTERFLY(23, 3);
    BUTTERFLY(24, 1);
    BUTTERFLY(25, 3);
    BUTTERFLY(26, 2);
    BUTTERFLY(27, 0);
    BUTTERFLY(28, 2);
    BUTTERFLY(29, 0);
    BUTTERFLY(30, 1);
    BUTTERFLY(31, 3);

    /* Swap current and next states */
    if (bitcnt & 1)
    {
      state = state0;
      next = state1;
    }
    else
    {
      state = state1;
      next = state0;
    }
    // ETTUS
    //if(bitcnt > nbits-7){
    /* In tail, poison non-zero nodes */
    //for(i=1;i<64;i += 2)
    //	state[i].metric = -9999999;
    //}
    /* Produce output every 8 bits once path memory is full */
    if ((bitcnt % 8) == 5 && bitcnt > 32)
    {
      /* Find current best path */
      bestmetric = state[0].metric;
      beststate = 0;
      for (i = 1; i < 64; i++)
      {
        if (state[i].metric > bestmetric)
        {
          bestmetric = state[i].metric;
          beststate = i;
        }
      }
#ifdef notdef
      printf("metrics[%d] = %d state = %lx\n", beststate,
             state[beststate].metric, state[beststate].path);
#endif
      *data++ = state[beststate].path >> 24;
    }
  }
  /* Output remaining bits from 0 state */
  // ETTUS  Find best state instead
  bestmetric = state[0].metric;
  beststate = 0;
  for (i = 1; i < 64; i++)
  {
    if (state[i].metric > bestmetric)
    {
      bestmetric = state[i].metric;
      beststate = i;
    }
  }
  if ((i = bitcnt % 8) != 6)
    state[beststate].path <<= 6 - i;

  *data++ = state[beststate].path >> 24;
  *data++ = state[beststate].path >> 16;
  *data++ = state[beststate].path >> 8;
  *data = state[beststate].path;
  //printf ("BS = %d\tBSM = %d\tM0 = %d\n",beststate,state[beststate].metric,state[0].metric);
  *metric = state[beststate].metric;
  return 0;
}

void viterbi_chunks_init(struct viterbi_state *state)
{
  // Initialize starting metrics to prefer 0 state
  int i;
  state[0].metric = 0;
  state[0].path = 0;
  for (i = 1; i < 64; i++)
    state[i].metric = -999999;
}

void viterbi_butterfly8(unsigned char *symbols, int mettab[2][256], struct viterbi_state *state0, struct viterbi_state *state1)
{
  unsigned int bitcnt;
  int mets[4];

  struct viterbi_state *state, *next;
  state = state0;
  next = state1;
  // Operate on 16 symbols (8 bits) at a time
  for (bitcnt = 0; bitcnt < 8; bitcnt++)
  {
    // Read input symbol pair and compute all possible branch metrics
    mets[0] = mettab[0][symbols[0]] + mettab[0][symbols[1]];
    mets[1] = mettab[0][symbols[0]] + mettab[1][symbols[1]];
    mets[2] = mettab[1][symbols[0]] + mettab[0][symbols[1]];
    mets[3] = mettab[1][symbols[0]] + mettab[1][symbols[1]];
    symbols += 2;

    // These macro calls were generated by genbut.c
    /*    BUTTERFLY(0,0);BUTTERFLY(1,1);BUTTERFLY(2,3);BUTTERFLY(3,2);
    BUTTERFLY(4,3);BUTTERFLY(5,2);BUTTERFLY(6,0);BUTTERFLY(7,1);
    BUTTERFLY(8,0);BUTTERFLY(9,1);BUTTERFLY(10,3);BUTTERFLY(11,2);
    BUTTERFLY(12,3);BUTTERFLY(13,2);BUTTERFLY(14,0);BUTTERFLY(15,1);
    BUTTERFLY(16,2);BUTTERFLY(17,3);BUTTERFLY(18,1);BUTTERFLY(19,0);
    BUTTERFLY(20,1);BUTTERFLY(21,0);BUTTERFLY(22,2);BUTTERFLY(23,3);
    BUTTERFLY(24,2);BUTTERFLY(25,3);BUTTERFLY(26,1);BUTTERFLY(27,0);
    BUTTERFLY(28,1);BUTTERFLY(29,0);BUTTERFLY(30,2);BUTTERFLY(31,3);  */

    BUTTERFLY(0, 0);
    BUTTERFLY(1, 2);
    BUTTERFLY(2, 3);
    BUTTERFLY(3, 1);
    BUTTERFLY(4, 3);
    BUTTERFLY(5, 1);
    BUTTERFLY(6, 0);
    BUTTERFLY(7, 2);
    BUTTERFLY(8, 0);
    BUTTERFLY(9, 2);
    BUTTERFLY(10, 3);
    BUTTERFLY(11, 1);
    BUTTERFLY(12, 3);
    BUTTERFLY(13, 1);
    BUTTERFLY(14, 0);
    BUTTERFLY(15, 2);
    BUTTERFLY(16, 1);
    BUTTERFLY(17, 3);
    BUTTERFLY(18, 2);
    BUTTERFLY(19, 0);
    BUTTERFLY(20, 2);
    BUTTERFLY(21, 0);
    BUTTERFLY(22, 1);
    BUTTERFLY(23, 3);
    BUTTERFLY(24, 1);
    BUTTERFLY(25, 3);
    BUTTERFLY(26, 2);
    BUTTERFLY(27, 0);
    BUTTERFLY(28, 2);
    BUTTERFLY(29, 0);
    BUTTERFLY(30, 1);
    BUTTERFLY(31, 3);
    // Swap current and next states
    if (bitcnt & 1)
    {
      state = state0;
      next = state1;
    }
    else
    {
      state = state1;
      next = state0;
    }
  }
}

void viterbi_butterfly2(unsigned char *symbols, int mettab[2][256], struct viterbi_state *state0, struct viterbi_state *state1)
{
  //unsigned int bitcnt;
  int mets[4];

  struct viterbi_state *state, *next;
  state = state0;
  next = state1;
  // Operate on 4 symbols (2 bits) at a time

  // Read input symbol pair and compute all possible branch metrics
  mets[0] = mettab[0][symbols[0]] + mettab[0][symbols[1]];
  mets[1] = mettab[0][symbols[0]] + mettab[1][symbols[1]];
  mets[2] = mettab[1][symbols[0]] + mettab[0][symbols[1]];
  mets[3] = mettab[1][symbols[0]] + mettab[1][symbols[1]];

  // These macro calls were generated by genbut.c
  /*  BUTTERFLY(0,0);BUTTERFLY(1,1);BUTTERFLY(2,3);BUTTERFLY(3,2);
  BUTTERFLY(4,3);BUTTERFLY(5,2);BUTTERFLY(6,0);BUTTERFLY(7,1);
  BUTTERFLY(8,0);BUTTERFLY(9,1);BUTTERFLY(10,3);BUTTERFLY(11,2);
  BUTTERFLY(12,3);BUTTERFLY(13,2);BUTTERFLY(14,0);BUTTERFLY(15,1);
  BUTTERFLY(16,2);BUTTERFLY(17,3);BUTTERFLY(18,1);BUTTERFLY(19,0);
  BUTTERFLY(20,1);BUTTERFLY(21,0);BUTTERFLY(22,2);BUTTERFLY(23,3);
  BUTTERFLY(24,2);BUTTERFLY(25,3);BUTTERFLY(26,1);BUTTERFLY(27,0);
  BUTTERFLY(28,1);BUTTERFLY(29,0);BUTTERFLY(30,2);BUTTERFLY(31,3); */

  BUTTERFLY(0, 0);
  BUTTERFLY(1, 2);
  BUTTERFLY(2, 3);
  BUTTERFLY(3, 1);
  BUTTERFLY(4, 3);
  BUTTERFLY(5, 1);
  BUTTERFLY(6, 0);
  BUTTERFLY(7, 2);
  BUTTERFLY(8, 0);
  BUTTERFLY(9, 2);
  BUTTERFLY(10, 3);
  BUTTERFLY(11, 1);
  BUTTERFLY(12, 3);
  BUTTERFLY(13, 1);
  BUTTERFLY(14, 0);
  BUTTERFLY(15, 2);
  BUTTERFLY(16, 1);
  BUTTERFLY(17, 3);
  BUTTERFLY(18, 2);
  BUTTERFLY(19, 0);
  BUTTERFLY(20, 2);
  BUTTERFLY(21, 0);
  BUTTERFLY(22, 1);
  BUTTERFLY(23, 3);
  BUTTERFLY(24, 1);
  BUTTERFLY(25, 3);
  BUTTERFLY(26, 2);
  BUTTERFLY(27, 0);
  BUTTERFLY(28, 2);
  BUTTERFLY(29, 0);
  BUTTERFLY(30, 1);
  BUTTERFLY(31, 3);

  state = state1;
  next = state0;

  // Read input symbol pair and compute all possible branch metrics
  mets[0] = mettab[0][symbols[2]] + mettab[0][symbols[3]];
  mets[1] = mettab[0][symbols[2]] + mettab[1][symbols[3]];
  mets[2] = mettab[1][symbols[2]] + mettab[0][symbols[3]];
  mets[3] = mettab[1][symbols[2]] + mettab[1][symbols[3]];

  // These macro calls were generated by genbut.c
  /*  BUTTERFLY(0,0);BUTTERFLY(1,1);BUTTERFLY(2,3);BUTTERFLY(3,2);
  BUTTERFLY(4,3);BUTTERFLY(5,2);BUTTERFLY(6,0);BUTTERFLY(7,1);
  BUTTERFLY(8,0);BUTTERFLY(9,1);BUTTERFLY(10,3);BUTTERFLY(11,2);
  BUTTERFLY(12,3);BUTTERFLY(13,2);BUTTERFLY(14,0);BUTTERFLY(15,1);
  BUTTERFLY(16,2);BUTTERFLY(17,3);BUTTERFLY(18,1);BUTTERFLY(19,0);
  BUTTERFLY(20,1);BUTTERFLY(21,0);BUTTERFLY(22,2);BUTTERFLY(23,3);
  BUTTERFLY(24,2);BUTTERFLY(25,3);BUTTERFLY(26,1);BUTTERFLY(27,0);
  BUTTERFLY(28,1);BUTTERFLY(29,0);BUTTERFLY(30,2);BUTTERFLY(31,3); */

  BUTTERFLY(0, 0);
  BUTTERFLY(1, 2);
  BUTTERFLY(2, 3);
  BUTTERFLY(3, 1);
  BUTTERFLY(4, 3);
  BUTTERFLY(5, 1);
  BUTTERFLY(6, 0);
  BUTTERFLY(7, 2);
  BUTTERFLY(8, 0);
  BUTTERFLY(9, 2);
  BUTTERFLY(10, 3);
  BUTTERFLY(11, 1);
  BUTTERFLY(12, 3);
  BUTTERFLY(13, 1);
  BUTTERFLY(14, 0);
  BUTTERFLY(15, 2);
  BUTTERFLY(16, 1);
  BUTTERFLY(17, 3);
  BUTTERFLY(18, 2);
  BUTTERFLY(19, 0);
  BUTTERFLY(20, 2);
  BUTTERFLY(21, 0);
  BUTTERFLY(22, 1);
  BUTTERFLY(23, 3);
  BUTTERFLY(24, 1);
  BUTTERFLY(25, 3);
  BUTTERFLY(26, 2);
  BUTTERFLY(27, 0);
  BUTTERFLY(28, 2);
  BUTTERFLY(29, 0);
  BUTTERFLY(30, 1);
  BUTTERFLY(31, 3);
}

long viterbi_get_output(struct viterbi_state *state, unsigned char *outbuf)
{
  // Produce output every 8 bits once path memory is full
  //  if((bitcnt % 8) == 5 && bitcnt > 32) {

  //  Find current best path
  unsigned int i, beststate;
  long bestmetric;

  bestmetric = state[0].metric;
  beststate = 0;
  for (i = 1; i < 64; i++)
    if (state[i].metric > bestmetric)
    {
      bestmetric = state[i].metric;
      beststate = i;
    }
  //Martin Blaho
  //set all state[i].metric to  state[i].metric - bestmetric
  for (i = 0; i < 64; i++)
  {
    (state[i].metric = state[i].metric - bestmetric);
  }

  //*outbuf =  state[beststate].path >> 24;
  *outbuf = state[beststate].path;

#ifdef DEBUG0
  //printout few interesting values for debugging
  printf("char: %c   Decoded word: %d   best state: %i best metric: %i \n", state[beststate].path, state[beststate].path, beststate, bestmetric);
#endif

  return bestmetric;
}

long viterbi_get_output_mar(struct viterbi_state *state, unsigned char *outbuf)
{
  // Produce output every 8 bits once path memory is full
  //  if((bitcnt % 8) == 5 && bitcnt > 32) {

  //  Find current best path
  unsigned int i, beststate;
  long bestmetric;

  bestmetric = state[0].metric;
  beststate = 0;
  for (i = 1; i < 64; i++)
    if (state[i].metric > bestmetric)
    {
      bestmetric = state[i].metric;
      beststate = i;
    }

  //*outbuf =  state[beststate].path >> 24;
  *outbuf = state[beststate].path;

#ifdef DEBUG0
  //printout few interesting values for debugging
  printf("char: %c   Decoded word: %d   best state: %i best metric: %i \n", state[beststate].path, state[beststate].path, beststate, bestmetric);
#endif

  return bestmetric;
}

void viterbi_metric_decrement(struct viterbi_state *state, long decrement)
{
  int i;
  for (i = 1; i < 64; i++)
  {
    (state[i].metric = state[i].metric - decrement);
  }
}