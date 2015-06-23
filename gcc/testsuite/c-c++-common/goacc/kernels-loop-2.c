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
  unsigned int *__restrict a;
  unsigned int *__restrict b;
  unsigned int *__restrict c;

  a = (unsigned int *)malloc (N * sizeof (unsigned int));
  b = (unsigned int *)malloc (N * sizeof (unsigned int));
  c = (unsigned int *)malloc (N * sizeof (unsigned int));

#pragma acc kernels copyout (a[0:N])
  {
    #pragma ACC_LOOP
    for (COUNTERTYPE i = 0; i < N; i++)
      a[i] = i * 2;
  }

#pragma acc kernels copyout (b[0:N])
  {
    #pragma ACC_LOOP
    for (COUNTERTYPE i = 0; i < N; i++)
      b[i] = i * 4;
  }

#pragma acc kernels copyin (a[0:N], b[0:N]) copyout (c[0:N])
  {
    #pragma ACC_LOOP
    for (COUNTERTYPE ii = 0; ii < N; ii++)
      c[ii] = a[ii] + b[ii];
  }

  for (COUNTERTYPE i = 0; i < N; i++)
    if (c[i] != a[i] + b[i])
      abort ();

  free (a);
  free (b);
  free (c);

  return 0;
}

/* Check that only three loops are analyzed, and that all can be
   parallelized.  */
/* { dg-final { scan-tree-dump-times "SUCCESS: may be parallelized" 3 "parloops_oacc_kernels" } } */
/* { dg-final { scan-tree-dump-not "FAILED:" "parloops_oacc_kernels" } } */

/* Check that the loop has been split off into a function.  */
/* { dg-final { scan-tree-dump-times "(?n);; Function .*main._omp_fn.0" 1 "optimized" } } */
/* { dg-final { scan-tree-dump-times "(?n);; Function .*main._omp_fn.1" 1 "optimized" } } */
/* { dg-final { scan-tree-dump-times "(?n);; Function .*main._omp_fn.2" 1 "optimized" } } */

/* { dg-final { scan-tree-dump-times "(?n)pragma omp target oacc_parallel.*num_gangs\\(32\\)" 3 "parloops_oacc_kernels" } } */

/* { dg-final { cleanup-tree-dump "parloops_oacc_kernels" } } */
/* { dg-final { cleanup-tree-dump "optimized" } } */
