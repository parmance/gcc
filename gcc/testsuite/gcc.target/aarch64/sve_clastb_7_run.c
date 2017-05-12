/* { dg-do run { target aarch64_sve_hw } } */
/* { dg-options "-O2 -ftree-vectorize -fno-inline -march=armv8-a+sve" } */

#include "sve_clastb_7.c"

extern void abort (void) __attribute__ ((noreturn));

int
main (void)
{
  double a[N] = {
  11.5, 12.2, 13.22, 14.1, 15.2, 16.3, 17, 18.7, 19, 20,
  1, 2, 3.3, 4.3333, 5.5, 6.23, 7, 8.63, 9, 10.6,
  21, 22.12, 23.55, 24.76, 25, 26, 27.34, 28.765, 29, 30,
  31.111, 32.322
  };

  double ret = condition_reduction (a, 16.7);

  if (ret != (double)10.6)
    abort ();

  return 0;
}
