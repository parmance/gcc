/* { dg-do compile } */
/* { dg-options "-O -fprofile-generate -fprofile-dir=. -fdump-ipa-cgraph" } */
/* { dg-final { scan-ipa-dump " ./profile-dir-3.gcda" "cgraph" } } */

int
main(void)
{
  return 0;
}

/* { dg-final { cleanup-ipa-dump "cgraph" } } */
