/* { dg-do assemble } */
/* { dg-options "-O2 -ftree-vectorize -march=armv8-a+sve -fno-math-errno --save-temps" } */

#define DO_OPS(TYPE, OP)			\
void						\
vsqrt_##TYPE (TYPE *dst, TYPE *src, int count)	\
{						\
  for (int i = 0; i < count; ++i)		\
    dst[i] = __builtin_##OP (src[i]);		\
}


DO_OPS (float, sqrtf)
DO_OPS (double, sqrt)

/* { dg-final { scan-assembler-times {\tfsqrt\tz[0-9]+\.s, p[0-7]/m, z[0-9]+\.s\n} 1 } } */
/* { dg-final { scan-assembler-times {\tfsqrt\tz[0-9]+\.d, p[0-7]/m, z[0-9]+\.d\n} 1 } } */
