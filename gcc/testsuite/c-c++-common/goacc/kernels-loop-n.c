/* { dg-additional-options "-O2" } */
/* { dg-additional-options "-ftree-parallelize-loops=32" } */
/* { dg-additional-options "-fdump-tree-parloops_oacc_kernels-all" } */
/* { dg-additional-options "-fdump-tree-optimized" } */

#include <stdlib.h>

#define N ((1024 * 512) + 1)
#define COUNTERTYPE unsigned int

#ifndef ACC_LOOP
#define ACC_LOOP
#endif

int
foo (COUNTERTYPE n)
{
  unsigned int *__restrict a;
  unsigned int *__restrict b;
  unsigned int *__restrict c;

  a = (unsigned int *__restrict)malloc (n * sizeof (unsigned int));
  b = (unsigned int *__restrict)malloc (n * sizeof (unsigned int));
  c = (unsigned int *__restrict)malloc (n * sizeof (unsigned int));

  for (COUNTERTYPE i = 0; i < n; i++)
    a[i] = i * 2;

  for (COUNTERTYPE i = 0; i < n; i++)
    b[i] = i * 4;

#pragma acc kernels copyin (a[0:n], b[0:n]) copyout (c[0:n])
  {
    #pragma ACC_LOOP
    for (COUNTERTYPE ii = 0; ii < n; ii++)
      c[ii] = a[ii] + b[ii];
  }

  for (COUNTERTYPE i = 0; i < n; i++)
    if (c[i] != a[i] + b[i])
      abort ();

  free (a);
  free (b);
  free (c);

  return 0;
}

/* Check that only one loop is analyzed, and that it can be parallelized.  */
/* { dg-final { scan-tree-dump-times "SUCCESS: may be parallelized" 1 "parloops_oacc_kernels" } } */
/* { dg-final { scan-tree-dump-not "FAILED:" "parloops_oacc_kernels" } } */

/* Check that the loop has been split off into a function.  */
/* { dg-final { scan-tree-dump-times "(?n);; Function .*foo.*._omp_fn.0" 1 "optimized" } } */

/* { dg-final { scan-tree-dump-times "(?n)pragma omp target oacc_parallel.*num_gangs\\(32\\)" 1 "parloops_oacc_kernels" } } */

/* { dg-final { cleanup-tree-dump "parloops_oacc_kernels" } } */
/* { dg-final { cleanup-tree-dump "optimized" } } */
