/* { dg-require-effective-target vect_int } */

#include <stdlib.h>
#include <stdarg.h>
#include "tree-vect.h"

#define N 9

struct extraction
{
  int a[N];
  int b[N];
};

static int a[N] = {1,2,3,4,5,6,7,8,9};
static int b[N] = {2,3,4,5,6,7,8,9,9};

__attribute__ ((noinline))
int main1 (int x, int y) {
  int i;
  struct extraction *p;
  p = (struct extraction *) malloc (sizeof (struct extraction));

  for (i = 0; i < N; i++)
    {
      p->a[i] = a[i];
      asm volatile ("" ::: "memory");
    }

  /* Not vectorizable: distance 1.  */
  for (i = 0; i < N - 1; i++)
    {
       p->a[x + i + 1] = p->a[x + i];
    }

  /* check results: */
  for (i = 0; i < N; i++)
    {
       if (p->a[i] != 1)
         abort();
    }
  return 0;
}

int main (void)
{ 
  check_vect ();

  return main1 (0, N);
}

/* { dg-final { scan-tree-dump-times "vectorized 1 loops" 0 "vect" } } */
/* { dg-final { scan-tree-dump-times "possible dependence between data-refs" 1 "vect" { target vect_1_size } } } */
/* { dg-final { scan-tree-dump-times "possible dependence between data-refs" 2 "vect" { target vect_2_sizes } } } */
/* { dg-final { scan-tree-dump-times "possible dependence between data-refs" 3 "vect" { target vect_3_sizes } } } */
