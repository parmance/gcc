/* { dg-do compile } */
/* { dg-options "-O2 -ftree-vectorize -fno-tree-dce -ffast-math -march=armv8-a+sve" } */

#define TEST_LOOP(NAME, OUTTYPE, INTYPE, MASKTYPE)		\
  void								\
  NAME##_4 (OUTTYPE *__restrict dest, INTYPE *__restrict src,	\
	    MASKTYPE *__restrict cond, int n)			\
  {								\
    for (int i = 0; i < n; ++i)					\
      if (cond[i])						\
	dest[i] = src[i * 4] + src[i * 4 + 3];			\
  }

#define TEST2(NAME, OUTTYPE, INTYPE) \
  TEST_LOOP (NAME##_i8, OUTTYPE, INTYPE, signed char) \
  TEST_LOOP (NAME##_i16, OUTTYPE, INTYPE, unsigned short) \
  TEST_LOOP (NAME##_f32, OUTTYPE, INTYPE, float) \
  TEST_LOOP (NAME##_f64, OUTTYPE, INTYPE, double)

#define TEST1(NAME, OUTTYPE) \
  TEST2 (NAME##_i8, OUTTYPE, signed char) \
  TEST2 (NAME##_i16, OUTTYPE, unsigned short) \
  TEST2 (NAME##_i32, OUTTYPE, int) \
  TEST2 (NAME##_i64, OUTTYPE, unsigned long)

#define TEST(NAME) \
  TEST1 (NAME##_i8, signed char) \
  TEST1 (NAME##_i16, unsigned short) \
  TEST1 (NAME##_i32, int) \
  TEST1 (NAME##_i64, unsigned long) \
  TEST2 (NAME##_f32_f32, float, float) \
  TEST2 (NAME##_f64_f64, double, double)

TEST (test)

/*    Mask |  8 16 32 64
    -------+------------
    Out  8 |  1  1  1  1
        16 |  1  1  1  1
        32 |  1  1  1  1
        64 |  1  1  1  1.  */
/* { dg-final { scan-assembler-times {\tld4b\t.z[0-9]} 16 } } */

/*    Mask |  8 16 32 64
    -------+------------
    Out  8 |  2  2  2  2
        16 |  2  1  1  1
        32 |  2  1  1  1
        64 |  2  1  1  1.  */
/* { dg-final { scan-assembler-times {\tld4h\t.z[0-9]} 23 } } */

/*    Mask |  8 16 32 64
    -------+------------
    Out  8 |  4  4  4  4
        16 |  4  2  2  2
        32 |  4  2  1  1 x2 (for float)
        64 |  4  2  1  1.  */
/* { dg-final { scan-assembler-times {\tld4w\t.z[0-9]} 50 } } */

/*    Mask |  8 16 32 64
    -------+------------
    Out  8 |  8  8  8  8
        16 |  8  4  4  4
        32 |  8  4  2  2
        64 |  8  4  2  1 x2 (for double).  */
/* { dg-final { scan-assembler-times {\tld4d\t.z[0-9]} 98 } } */
