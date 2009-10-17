/* Interchange heuristics and transform for loop interchange on
   polyhedral representation.

   Copyright (C) 2009 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <sebastian.pop@amd.com> and
   Harsha Jagasia <harsha.jagasia@amd.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "rtl.h"
#include "output.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "toplev.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-chrec.h"
#include "tree-scalar-evolution.h"
#include "tree-data-ref.h"
#include "tree-pass.h"
#include "domwalk.h"
#include "value-prof.h"
#include "pointer-set.h"
#include "gimple.h"
#include "params.h"

#ifdef HAVE_cloog
#include "cloog/cloog.h"
#include "ppl_c.h"
#include "sese.h"
#include "graphite-ppl.h"
#include "graphite.h"
#include "graphite-poly.h"

/* Builds a linear expression, of dimension DIM, representing PDR's
   memory access:

   L = r_{n}*r_{n-1}*...*r_{1}*s_{0} + ... + r_{n}*s_{n-1} + s_{n}.

   For an array A[10][20] with two subscript locations s0 and s1, the
   linear memory access is 20 * s0 + s1: a stride of 1 in subscript s0
   corresponds to a memory stride of 20.

   OFFSET is a number of dimensions to prepend before the
   subscript dimensions: s_0, s_1, ..., s_n.

   Thus, the final linear expression has the following format:
   0 .. 0_{offset} | 0 .. 0_{nit} | 0 .. 0_{gd} | 0 | c_0 c_1 ... c_n
   where the expression itself is:
   c_0 * s_0 + c_1 * s_1 + ... c_n * s_n.  */

static ppl_Linear_Expression_t
build_linearized_memory_access (ppl_dimension_type offset, poly_dr_p pdr)
{
  ppl_Linear_Expression_t res;
  ppl_Linear_Expression_t le;
  ppl_dimension_type i;
  ppl_dimension_type first = pdr_subscript_dim (pdr, 0);
  ppl_dimension_type last = pdr_subscript_dim (pdr, PDR_NB_SUBSCRIPTS (pdr));
  Value size, sub_size;
  graphite_dim_t dim = offset + pdr_dim (pdr);

  ppl_new_Linear_Expression_with_dimension (&res, dim);

  value_init (size);
  value_set_si (size, 1);
  value_init (sub_size);
  value_set_si (sub_size, 1);

  for (i = last - 1; i >= first; i--)
    {
      ppl_set_coef_gmp (res, i + offset, size);

      ppl_new_Linear_Expression_with_dimension (&le, dim - offset);
      ppl_set_coef (le, i, 1);
      ppl_max_for_le_pointset (PDR_ACCESSES (pdr), le, sub_size);
      value_multiply (size, size, sub_size);
      ppl_delete_Linear_Expression (le);
    }

  value_clear (sub_size);
  value_clear (size);
  return res;
}

/* Set STRIDE to the stride of PDR in memory by advancing by one in
   time dimension DEPTH.  */

static void
memory_stride_in_loop (Value stride, graphite_dim_t depth, poly_dr_p pdr)
{
  ppl_dimension_type time_depth;
  ppl_Linear_Expression_t le, lma;
  ppl_Constraint_t new_cstr;
  ppl_dimension_type i, *map;
  ppl_Pointset_Powerset_C_Polyhedron_t p1, p2, sctr;
  graphite_dim_t nb_subscripts = PDR_NB_SUBSCRIPTS (pdr) + 1;
  poly_bb_p pbb = PDR_PBB (pdr);
  ppl_dimension_type offset = pbb_nb_scattering_transform (pbb)
                              + pbb_nb_local_vars (pbb)
                              + pbb_dim_iter_domain (pbb);
  ppl_dimension_type offsetg = offset + pbb_nb_params (pbb);
  ppl_dimension_type dim_sctr = pbb_nb_scattering_transform (pbb)
                                + pbb_nb_local_vars (pbb);
  ppl_dimension_type dim_L1 = offset + offsetg + 2 * nb_subscripts;
  ppl_dimension_type dim_L2 = offset + offsetg + 2 * nb_subscripts + 1;
  ppl_dimension_type new_dim = offset + offsetg + 2 * nb_subscripts + 2;

  /* The resulting polyhedron should have the following format:
     T|I|T'|I'|G|S|S'|l1|l2
     where:
     | T = t_1..t_{dim_sctr}
     | I = i_1..i_{dim_iter_domain}
     | T'= t'_1..t'_{dim_sctr}
     | I'= i'_1..i'_{dim_iter_domain}
     | G = g_1..g_{nb_params}
     | S = s_1..s_{nb_subscripts}
     | S'= s'_1..s'_{nb_subscripts}
     | l1 and l2 are scalars.

     Some invariants:
     offset = dim_sctr + dim_iter_domain + nb_local_vars
     offsetg = dim_sctr + dim_iter_domain + nb_local_vars + nb_params.  */

  /* Construct the T|I|0|0|G|0|0|0|0 part.  */
  {
    ppl_new_Pointset_Powerset_C_Polyhedron_from_C_Polyhedron
      (&sctr, PBB_TRANSFORMED_SCATTERING (pbb));
    ppl_Pointset_Powerset_C_Polyhedron_add_space_dimensions_and_embed
      (sctr, 2 * nb_subscripts + 2);
    ppl_insert_dimensions_pointset (sctr, offset, offset);
  }

  /* Construct the 0|I|0|0|G|S|0|0|0 part.  */
  {
    ppl_new_Pointset_Powerset_C_Polyhedron_from_Pointset_Powerset_C_Polyhedron
      (&p1, PDR_ACCESSES (pdr));
    ppl_Pointset_Powerset_C_Polyhedron_add_space_dimensions_and_embed
      (p1, nb_subscripts + 2);
    ppl_insert_dimensions_pointset (p1, 0, dim_sctr);
    ppl_insert_dimensions_pointset (p1, offset, offset);
  }

  /* Construct the 0|0|0|0|0|S|0|l1|0 part.  */
  {
    lma = build_linearized_memory_access (offset + dim_sctr, pdr);
    ppl_set_coef (lma, dim_L1, -1);
    ppl_new_Constraint (&new_cstr, lma, PPL_CONSTRAINT_TYPE_EQUAL);
    ppl_Pointset_Powerset_C_Polyhedron_add_constraint (p1, new_cstr);
    ppl_delete_Linear_Expression (lma);
    ppl_delete_Constraint (new_cstr);
  }

  /* Now intersect all the parts to get the polyhedron P1:
     T|I|0|0|G|0|0|0 |0
     0|I|0|0|G|S|0|0 |0
     0|0|0|0|0|S|0|l1|0
     ------------------
     T|I|0|0|G|S|0|l1|0.  */

  ppl_Pointset_Powerset_C_Polyhedron_intersection_assign (p1, sctr);
  ppl_delete_Pointset_Powerset_C_Polyhedron (sctr);

  /* Build P2, which would have the following form:
     0|0|T'|I'|G|0|S'|0|l2

     P2 is built, by remapping the P1 polyhedron:
     T|I|0|0|G|S|0|l1|0

     using the following mapping:
     T->T'
     I->I'
     S->S'
     l1->l2.  */
  {
    ppl_new_Pointset_Powerset_C_Polyhedron_from_Pointset_Powerset_C_Polyhedron
      (&p2, p1);

    map = ppl_new_id_map (new_dim);

    /* TI -> T'I'.  */
    for (i = 0; i < offset; i++)
      ppl_interchange (map, i, i + offset);

    /* l1 -> l2.  */
    ppl_interchange (map, dim_L1, dim_L2);

    /* S -> S'.  */
    for (i = 0; i < nb_subscripts; i++)
      ppl_interchange (map, offset + offsetg + i,
		       offset + offsetg + nb_subscripts + i);

    ppl_Pointset_Powerset_C_Polyhedron_map_space_dimensions (p2, map, new_dim);
    free (map);
  }

  /* Add equalities:
     | t1 = t1'
     | ...
     | t_{depth-1} = t'_{depth-1}
     | t_{depth+1} = t'_{depth+1}
     | ...
     | t_{dim_sctr} = t'_{dim_sctr}

     This means that all the time dimensions are equal except for
     depth, where we will add t_{depth} = t'_{depth} + 1 in the next
     step.  */

  time_depth = psct_dynamic_dim (pbb, depth);
  for (i = 0; i < dim_sctr; i++)
    if (i != time_depth)
      {
        ppl_new_Linear_Expression_with_dimension (&le, new_dim);
        ppl_set_coef (le, i, 1);
        ppl_set_coef (le, i + offset, -1);
        ppl_new_Constraint (&new_cstr, le, PPL_CONSTRAINT_TYPE_EQUAL);
        ppl_Pointset_Powerset_C_Polyhedron_add_constraint (p2, new_cstr);
        ppl_delete_Linear_Expression (le);
        ppl_delete_Constraint (new_cstr);
      }

  /* Add equality : t_{depth} = t'_{depth} + 1.
     This is the core part of this alogrithm, since this
     constraint asks for the memory access stride (difference)
     between two consecutive points in time dimensions.  */
  {
    ppl_new_Linear_Expression_with_dimension (&le, new_dim);
    ppl_set_coef (le, time_depth, 1);
    ppl_set_coef (le, time_depth + offset, -1);
    ppl_set_inhomogeneous (le, 1);
    ppl_new_Constraint (&new_cstr, le, PPL_CONSTRAINT_TYPE_EQUAL);
    ppl_Pointset_Powerset_C_Polyhedron_add_constraint (p2, new_cstr);
    ppl_delete_Linear_Expression (le);
    ppl_delete_Constraint (new_cstr);
  }

  /* P1 = P1 inter P2.  */
  ppl_Pointset_Powerset_C_Polyhedron_intersection_assign (p1, p2);

  /* Maximise the expression L2 - L1.  */
  {
    ppl_new_Linear_Expression_with_dimension (&le, new_dim);
    ppl_set_coef (le, dim_L2, 1);
    ppl_set_coef (le, dim_L1, -1);
    ppl_max_for_le_pointset (p1, le, stride);
  }

  ppl_delete_Pointset_Powerset_C_Polyhedron (p1);
  ppl_delete_Pointset_Powerset_C_Polyhedron (p2);
  ppl_delete_Linear_Expression (le);
}

/* Returns true when it is profitable to interchange time dimensions DEPTH1
   and DEPTH2 with DEPTH1 < DEPTH2 for PBB.

   Example:

   | int a[100][100];
   |
   | int
   | foo (int N)
   | {
   |   int j;
   |   int i;
   |
   |   for (i = 0; i < N; i++)
   |     for (j = 0; j < N; j++)
   |       a[j][2 * i] += 1;
   |
   |   return a[N][12];
   | }

   The data access A[j][i] is described like this:

   | i   j   N   a  s0  s1   1
   | 0   0   0   1   0   0  -5    = 0
   | 0  -1   0   0   1   0   0    = 0
   |-2   0   0   0   0   1   0    = 0
   | 0   0   0   0   1   0   0   >= 0
   | 0   0   0   0   0   1   0   >= 0
   | 0   0   0   0  -1   0 100   >= 0
   | 0   0   0   0   0  -1 100   >= 0

   The linearized memory access L to A[100][100] is:

   | i   j   N   a  s0  s1   1
   | 0   0   0   0 100   1   0

   TODO: the shown format is not valid as it does not show the fact
   that the iteration domain "i j" is transformed using the scattering.

   Next, to measure the impact of iterating once in loop "i", we build
   a maximization problem: first, we add to DR accesses the dimensions
   k, s2, s3, L1 = 100 * s0 + s1, L2, and D1: this is the polyhedron P1.
   L1 and L2 are the linearized memory access functions.

   | i   j   N   a  s0  s1   k  s2  s3  L1  L2  D1   1
   | 0   0   0   1   0   0   0   0   0   0   0   0  -5    = 0  alias = 5
   | 0  -1   0   0   1   0   0   0   0   0   0   0   0    = 0  s0 = j
   |-2   0   0   0   0   1   0   0   0   0   0   0   0    = 0  s1 = 2 * i
   | 0   0   0   0   1   0   0   0   0   0   0   0   0   >= 0
   | 0   0   0   0   0   1   0   0   0   0   0   0   0   >= 0
   | 0   0   0   0  -1   0   0   0   0   0   0   0 100   >= 0
   | 0   0   0   0   0  -1   0   0   0   0   0   0 100   >= 0
   | 0   0   0   0 100   1   0   0   0  -1   0   0   0    = 0  L1 = 100 * s0 + s1

   Then, we generate the polyhedron P2 by interchanging the dimensions
   (s0, s2), (s1, s3), (L1, L2), (k, i)

   | i   j   N   a  s0  s1   k  s2  s3  L1  L2  D1   1
   | 0   0   0   1   0   0   0   0   0   0   0   0  -5    = 0  alias = 5
   | 0  -1   0   0   0   0   0   1   0   0   0   0   0    = 0  s2 = j
   | 0   0   0   0   0   0  -2   0   1   0   0   0   0    = 0  s3 = 2 * k
   | 0   0   0   0   0   0   0   1   0   0   0   0   0   >= 0
   | 0   0   0   0   0   0   0   0   1   0   0   0   0   >= 0
   | 0   0   0   0   0   0   0  -1   0   0   0   0 100   >= 0
   | 0   0   0   0   0   0   0   0  -1   0   0   0 100   >= 0
   | 0   0   0   0   0   0   0 100   1   0  -1   0   0    = 0  L2 = 100 * s2 + s3

   then we add to P2 the equality k = i + 1:

   |-1   0   0   0   0   0   1   0   0   0   0   0  -1    = 0  k = i + 1

   and finally we maximize the expression "D1 = max (P1 inter P2, L2 - L1)".

   Similarly, to determine the impact of one iteration on loop "j", we
   interchange (k, j), we add "k = j + 1", and we compute D2 the
   maximal value of the difference.

   Finally, the profitability test is D1 < D2: if in the outer loop
   the strides are smaller than in the inner loop, then it is
   profitable to interchange the loops at DEPTH1 and DEPTH2.  */

static bool
pbb_interchange_profitable_p (graphite_dim_t depth1, graphite_dim_t depth2,
			      poly_bb_p pbb)
{
  int i;
  poly_dr_p pdr;
  Value d1, d2, s, n;
  bool res;

  gcc_assert (depth1 < depth2);

  value_init (d1);
  value_set_si (d1, 0);
  value_init (d2);
  value_set_si (d2, 0);
  value_init (s);
  value_init (n);

  for (i = 0; VEC_iterate (poly_dr_p, PBB_DRS (pbb), i, pdr); i++)
    {
      value_set_si (n, PDR_NB_REFS (pdr));

      memory_stride_in_loop (s, depth1, pdr);
      value_multiply (s, s, n);
      value_addto (d1, d1, s);

      memory_stride_in_loop (s, depth2, pdr);
      value_multiply (s, s, n);
      value_addto (d2, d2, s);
    }

  res = value_lt (d1, d2);

  value_clear (d1);
  value_clear (d2);
  value_clear (s);
  value_clear (n);

  return res;
}

/* Interchanges the loops at DEPTH1 and DEPTH2 of the original
   scattering and assigns the resulting polyhedron to the transformed
   scattering.  */

static void
pbb_interchange_loop_depths (graphite_dim_t depth1, graphite_dim_t depth2,
			     poly_bb_p pbb)
{
  ppl_dimension_type i, dim;
  ppl_dimension_type *map;
  ppl_Polyhedron_t poly = PBB_TRANSFORMED_SCATTERING (pbb);
  ppl_dimension_type dim1 = psct_dynamic_dim (pbb, depth1);
  ppl_dimension_type dim2 = psct_dynamic_dim (pbb, depth2);

  ppl_Polyhedron_space_dimension (poly, &dim);
  map = (ppl_dimension_type *) XNEWVEC (ppl_dimension_type, dim);

  for (i = 0; i < dim; i++)
    map[i] = i;

  map[dim1] = dim2;
  map[dim2] = dim1;

  ppl_Polyhedron_map_space_dimensions (poly, map, dim);
  free (map);
}

/* Apply the interchange of loops at depths DEPTH1 and DEPTH2 to all
   the statements below LST.  */

static void
lst_apply_interchange (lst_p lst, int depth1, int depth2)
{
  if (!lst)
    return;

  if (LST_LOOP_P (lst))
    {
      int i;
      lst_p l;

      for (i = 0; VEC_iterate (lst_p, LST_SEQ (lst), i, l); i++)
	lst_apply_interchange (l, depth1, depth2);
    }
  else
    pbb_interchange_loop_depths (depth1, depth2, LST_PBB (lst));
}

/* Return true when the interchange of loops at depths DEPTH1 and
   DEPTH2 to all the statements below LST is profitable.  */

static bool
lst_interchange_profitable_p (lst_p lst, int depth1, int depth2)
{
  if (!lst)
    return false;

  if (LST_LOOP_P (lst))
    {
      int i;
      lst_p l;
      bool res = false;

      for (i = 0; VEC_iterate (lst_p, LST_SEQ (lst), i, l); i++)
	{
	  bool profitable = lst_interchange_profitable_p (l, depth1, depth2);

	  if (profitable && !LST_LOOP_P (lst)
	      && dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "Interchanging loops at depths %d and %d is profitable for stmt_%d.\n",
		     depth1, depth2, pbb_index (LST_PBB (lst)));

	  res |= profitable;
	}

      return res;
    }
  else
    return pbb_interchange_profitable_p (depth1, depth2, LST_PBB (lst));
}


/* Try to interchange LOOP1 with LOOP2 for all the statements of the
   body of LOOP2.  LOOP1 contains LOOP2.  Return true if it did the
   interchange.  */

static bool
lst_try_interchange_loops (scop_p scop, lst_p loop1, lst_p loop2)
{
  int depth1 = lst_depth (loop1);
  int depth2 = lst_depth (loop2);

  if (!lst_interchange_profitable_p (loop2, depth1, depth2))
    return false;

  lst_apply_interchange (loop2, depth1, depth2);

  if (graphite_legal_transform (scop))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "Loops at depths %d and %d will be interchanged.\n",
		 depth1, depth2);

      return true;
    }

  /* Undo the transform.  */
  lst_apply_interchange (loop2, depth2, depth1);
  return false;
}

/* Try to interchange LOOP with all the loops contained in the body of
   LST.  Return true if it did interchanged some loops.  */

static bool
lst_try_interchange (scop_p scop, lst_p loop, lst_p lst)
{
  if (!lst)
    return false;

  if (LST_LOOP_P (lst))
    {
      int i;
      lst_p l;
      bool res = lst_try_interchange_loops (scop, loop, lst);

      for (i = 0; VEC_iterate (lst_p, LST_SEQ (lst), i, l); i++)
	res |= lst_try_interchange (scop, loop, l);

      return res;
    }

  return false;
}

/* Interchanges all the loops of LST that are considered profitable to
   interchange.  Return true if it did interchanged some loops.  */

static bool
lst_do_interchange (scop_p scop, lst_p lst)
{
  if (!lst)
    return false;

  if (LST_LOOP_P (lst))
    {
      int i;
      lst_p l;
      bool res = false;

      if (lst_depth (lst) >= 0)
	for (i = 0; VEC_iterate (lst_p, LST_SEQ (lst), i, l); i++)
	  res |= lst_try_interchange (scop, lst, l);

      for (i = 0; VEC_iterate (lst_p, LST_SEQ (lst), i, l); i++)
	res |= lst_do_interchange (scop, l);

      return res;
    }

  return false;
}

/* Interchanges all the loop depths that are considered profitable for SCOP.  */

bool
scop_do_interchange (scop_p scop)
{
  bool transform_done = false;

  store_scattering (scop);

  transform_done = lst_do_interchange (scop, SCOP_TRANSFORMED_SCHEDULE (scop));

  if (!transform_done)
    return false;

  if (!graphite_legal_transform (scop))
    {
      restore_scattering (scop);
      return false;
    }

  return transform_done;
}


#endif

