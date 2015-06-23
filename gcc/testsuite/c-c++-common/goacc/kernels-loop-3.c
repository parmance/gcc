/* { dg-additional-options "-O2" } */
/* { dg-additional-options "-ftree-parallelize-loops=32" } */
/* { dg-additional-options "-fdump-tree-parloops_oacc_kernels-all" } */
/* { dg-additional-options "-fdump-tree-optimized" } */

#include <stdlib.h>

#define N (1024 * 512)
#define COUNTERTYPE unsigned int

#ifndef ACC_LOOP
#define ACC_LOOP
#endif

int
main (void)
{
  unsigned int i;

  unsigned int *__restrict c;

  c = (unsigned int *__restrict)malloc (N * sizeof (unsigned int));

  for (COUNTERTYPE i = 0; i < N; i++)
    c[i] = i * 2;

#pragma acc kernels copy (c[0:N])
  {
    #pragma ACC_LOOP
    for (COUNTERTYPE ii = 0; ii < N; ii++)
      c[ii] = c[ii] + ii + 1;
  }

  for (COUNTERTYPE i = 0; i < N; i++)
    if (c[i] != i * 2 + i + 1)
      abort ();

  free (c);

  return 0;
}

/* Check that only one loop is analyzed, and that it can be parallelized.  */
/* { dg-final { scan-tree-dump-times "SUCCESS: may be parallelized" 1 "parloops_oacc_kernels" } } */
/* { dg-final { scan-tree-dump-not "FAILED:" "parloops_oacc_kernels" } } */

/* Check that the loop has been split off into a function.  */
/* { dg-final { scan-tree-dump-times "(?n);; Function .*main._omp_fn.0" 1 "optimized" } } */

/* { dg-final { scan-tree-dump-times "(?n)pragma omp target oacc_parallel.*num_gangs\\(32\\)" 1 "parloops_oacc_kernels" } } */

/* { dg-final { cleanup-tree-dump "parloops_oacc_kernels" } } */
/* { dg-final { cleanup-tree-dump "optimized" } } */
