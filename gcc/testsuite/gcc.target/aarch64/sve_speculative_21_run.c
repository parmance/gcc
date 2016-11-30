/* { dg-do run { target { aarch64_sve_hw } } } */
/* { dg-options "-O3 -fno-common -ffast-math -march=armv8-a+sve" } */

#define STRIDE_LEVEL 4
#define EXIT_CONDITION 72
#define LOOP_COUNTS {21,34,18,55,33,55}

#include "sve_speculative_9_run.c"
