/* Area:	closure_call
   Purpose:	Check return value schar.
   Limitations:	none.
   PR:		none.
   Originator:	<andreast@gcc.gnu.org> 20031108	 */

/* { dg-do run { xfail mips*-*-* arm*-*-* strongarm*-*-* xscale*-*-* } } */
#include "ffitest.h"

static void cls_ret_schar_fn(ffi_cif* cif,void* resp,void** args,
			     void* userdata)
{
  *(ffi_arg*)resp = *(signed char *)args[0];
  printf("%d: %d\n",*(signed char *)args[0],
	 *(ffi_arg*)resp);
}
typedef signed char (*cls_ret_schar)(signed char);

int main (void)
{
  ffi_cif cif;
  static ffi_closure cl;
  ffi_closure *pcl = &cl;
  ffi_type * cl_arg_types[2];
  signed char res;

  cl_arg_types[0] = &ffi_type_schar;
  cl_arg_types[1] = NULL;

  /* Initialize the cif */
  CHECK(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1,
		     &ffi_type_schar, cl_arg_types) == FFI_OK);

  CHECK(ffi_prep_closure(pcl, &cif, cls_ret_schar_fn, NULL)  == FFI_OK);

  res = (*((cls_ret_schar)pcl))(127);
  /* { dg-output "127: 127" } */
  printf("res: %d\n", res);
  /* { dg-output "\nres: 127" } */

  exit(0);
}
