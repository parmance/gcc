/* { dg-do compile } */

#include <objc/objc.h>

@interface MyRootClass
{
  Class isa;
}
@property int name __attribute__((deprecated));
@property int table __attribute__((xxx));       /* { dg-warning ".xxx. attribute directive ignored" } */
/* FIXME: the test below should not ICE.
@property void function (void);                  { dg-error "can.t make .function. into a method" } */
@property typedef int j;                        /*  { dg-error "invalid type for property" } */
@end
