/* Routines for writing PPH data.
   Copyright (C) 2011 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@google.com>.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "langhooks.h"
#include "tree-iterator.h"
#include "tree-pretty-print.h"
#include "lto-streamer.h"
#include "pph-streamer.h"
#include "pph.h"
#include "tree-pass.h"
#include "version.h"
#include "cppbuiltin.h"

/* FIXME pph.  This holds the FILE handle for the current PPH file
   that we are writing.  It is necessary because the LTO callbacks do
   not allow passing a FILE handle to them.  */
static FILE *current_pph_file = NULL;


/* Callback for packing value fields in ASTs.  BP is the bitpack 
   we are packing into.  EXPR is the tree to pack.  */

void
pph_stream_pack_value_fields (struct bitpack_d *bp ATTRIBUTE_UNUSED,
			      tree expr ATTRIBUTE_UNUSED)
{
  /* Do nothing for now.  */
}


/* Initialize buffers and tables in STREAM for writing.  */

void
pph_stream_init_write (pph_stream *stream)
{
  lto_writer_init ();
  stream->out_state = lto_new_out_decl_state ();
  lto_push_out_decl_state (stream->out_state);
  stream->decl_state_stream = XCNEW (struct lto_output_stream);
  stream->ob = create_output_block (LTO_section_decls);

  /* Associate STREAM with STREAM->OB so we can recover it from the
     streamer hooks.  */
  stream->ob->sdata = (void *) stream;
}


/* Callback for lang_hooks.lto.begin_section.  Open file NAME.  */

static void
pph_stream_begin_section (const char *name ATTRIBUTE_UNUSED)
{
}


/* Callback for lang_hooks.lto.append_data.  Write LEN bytes from DATA
   into current_pph_file.  BLOCK is currently unused.  */

static void
pph_stream_write (const void *data, size_t len, void *block ATTRIBUTE_UNUSED)
{
  if (data)
    fwrite (data, len, 1, current_pph_file);
}


/* Callback for lang_hooks.lto.end_section.  */

static void
pph_stream_end_section (void)
{
}


/* Write the header for the PPH file represented by STREAM.  */

static void
pph_stream_write_header (pph_stream *stream)
{
  pph_file_header header;
  struct lto_output_stream header_stream;
  int major, minor, patchlevel;

  /* Collect version information.  */
  parse_basever (&major, &minor, &patchlevel);
  gcc_assert (major == (char) major);
  gcc_assert (minor == (char) minor);
  gcc_assert (patchlevel == (char) patchlevel);

  /* Write the header for the PPH file.  */
  memset (&header, 0, sizeof (header));
  strcpy (header.id_str, pph_id_str);
  header.major_version = (char) major;
  header.minor_version = (char) minor;
  header.patchlevel = (char) patchlevel;
  header.strtab_size = stream->ob->string_stream->total_size;

  memset (&header_stream, 0, sizeof (header_stream));
  lto_output_data_stream (&header_stream, &header, sizeof (header));
  lto_write_stream (&header_stream);
}


/* Write the body of the PPH file represented by STREAM.  */

static void
pph_stream_write_body (pph_stream *stream)
{
  /* Write the string table.  */
  lto_write_stream (stream->ob->string_stream);

  /* Write out the physical representation for every AST in all the
     streams in STREAM->OUT_STATE.  */
  lto_output_decl_state_streams (stream->ob, stream->out_state);

  /* Now write the vector of all AST references.  */
  lto_output_decl_state_refs (stream->ob, stream->decl_state_stream,
			      stream->out_state);

  /* Finally, physically write all the streams.  */
  lto_write_stream (stream->ob->main_stream);
}


/* Flush all the in-memory buffers for STREAM to disk.  */

void
pph_stream_flush_buffers (pph_stream *stream)
{
  gcc_assert (current_pph_file == NULL);
  current_pph_file = stream->file;

  /* Redirect the LTO basic I/O langhooks.  */
  lang_hooks.lto.begin_section = pph_stream_begin_section;
  lang_hooks.lto.append_data = pph_stream_write;
  lang_hooks.lto.end_section = pph_stream_end_section;

  /* Write the state buffers built by pph_output_*() calls.  */
  lto_begin_section (stream->name, false);
  pph_stream_write_header (stream);
  pph_stream_write_body (stream);
  lto_end_section ();
  current_pph_file = NULL;
}


/* Start a new record in STREAM for data in DATA.  If DATA is NULL,
   write an end-of-record marker and return false.  Otherwise, write a
   start-of-record marker and return true.  */

static inline bool
pph_start_record (pph_stream *stream, void *data)
{
  if (data)
    {
      pph_output_uchar (stream, PPH_RECORD_START);
      return true;
    }
  else
    {
      pph_output_uchar (stream, PPH_RECORD_END);
      return false;
    }
}


/* Write all the fields in lang_decl_base instance LDB to OB.  */

static void
pph_stream_write_ld_base (pph_stream *stream, struct lang_decl_base *ldb)
{
  struct bitpack_d bp;

  if (!pph_start_record (stream, ldb))
    return;

  bp = bitpack_create (stream->ob->main_stream);
  bp_pack_value (&bp, ldb->selector, 16);
  bp_pack_value (&bp, ldb->language, 4);
  bp_pack_value (&bp, ldb->use_template, 2);
  bp_pack_value (&bp, ldb->not_really_extern, 1);
  bp_pack_value (&bp, ldb->initialized_in_class, 1);
  bp_pack_value (&bp, ldb->repo_available_p, 1);
  bp_pack_value (&bp, ldb->threadprivate_or_deleted_p, 1);
  bp_pack_value (&bp, ldb->anticipated_p, 1);
  bp_pack_value (&bp, ldb->friend_attr, 1);
  bp_pack_value (&bp, ldb->template_conv_p, 1);
  bp_pack_value (&bp, ldb->odr_used, 1);
  bp_pack_value (&bp, ldb->u2sel, 1);
  pph_output_bitpack (stream, &bp);
}


/* Write all the fields in lang_decl_min instance LDM to STREAM.  If REF_P
   is true, all tree fields should be written as references.  */

static void
pph_stream_write_ld_min (pph_stream *stream, struct lang_decl_min *ldm,
		         bool ref_p)
{
  if (!pph_start_record (stream, ldm))
    return;

  gcc_assert (ldm->base.selector == 0);

  pph_output_tree_or_ref (stream, ldm->template_info, ref_p);
  if (ldm->base.u2sel == 0)
    pph_output_tree_or_ref (stream, ldm->u2.access, ref_p);
  else if (ldm->base.u2sel == 1)
    pph_output_uint (stream, ldm->u2.discriminator);
  else
    gcc_unreachable ();
}


/* Write all the trees in VEC V to STREAM.  REF_P is true if the trees should
   be written as references.  */

static void
pph_stream_write_tree_vec (pph_stream *stream, VEC(tree,gc) *v, bool ref_p)
{
  unsigned i;
  tree t;

  pph_output_uint (stream, VEC_length (tree, v));
  for (i = 0; VEC_iterate (tree, v, i, t); i++)
    pph_output_tree_or_ref (stream, t, ref_p);
}

/* Forward declaration to break cyclic dependencies.  */
static void pph_stream_write_binding_level (pph_stream *,
					    struct cp_binding_level *, bool);


/* Helper for pph_stream_write_cxx_binding.  STREAM, CB and REF_P are as in
   pph_stream_write_cxx_binding.  */

static void
pph_stream_write_cxx_binding_1 (pph_stream *stream, cxx_binding *cb, bool ref_p)
{
  struct bitpack_d bp;

  if (!pph_start_record (stream, cb))
    return;

  pph_output_tree_or_ref (stream, cb->value, ref_p);
  pph_output_tree_or_ref (stream, cb->type, ref_p);
  pph_stream_write_binding_level (stream, cb->scope, ref_p);
  bp = bitpack_create (stream->ob->main_stream);
  bp_pack_value (&bp, cb->value_is_inherited, 1);
  bp_pack_value (&bp, cb->is_local, 1);
  pph_output_bitpack (stream, &bp);
}


/* Write all the fields of cxx_binding instance CB to STREAM.  REF_P is
   true if the tree fields should be written as references.  */

static void
pph_stream_write_cxx_binding (pph_stream *stream, cxx_binding *cb, bool ref_p)
{
  unsigned num_bindings;
  cxx_binding *prev;

  if (!pph_start_record (stream, cb))
    return;

  for (num_bindings = 0, prev = cb->previous; prev; prev = prev->previous)
    num_bindings++;

  /* Write the list of previous bindings.  */
  pph_output_uint (stream, num_bindings);
  for (prev = cb->previous; prev; prev = prev->previous)
    pph_stream_write_cxx_binding_1 (stream, prev, ref_p);

  /* Write the current binding at the end.  */
  pph_stream_write_cxx_binding_1 (stream, cb, ref_p);
}


/* Write all the fields of cp_class_binding instance CB to STREAM.  REF_P
   is true if the tree fields should be written as references.  */

static void
pph_stream_write_class_binding (pph_stream *stream, cp_class_binding *cb,
			        bool ref_p)
{
  if (!pph_start_record (stream, cb))
    return;

  pph_stream_write_cxx_binding (stream, &cb->base, ref_p);
  pph_output_tree_or_ref (stream, cb->identifier, ref_p);
}


/* Write all the fields of cp_label_binding instance LB to STREAM.  If
   REF_P is true, tree fields will be written as references.  */

static void
pph_stream_write_label_binding (pph_stream *stream, cp_label_binding *lb,
				bool ref_p)
{
  if (!pph_start_record (stream, lb))
    return;

  pph_output_tree_or_ref (stream, lb->label, ref_p);
  pph_output_tree_or_ref (stream, lb->prev_value, ref_p);
}


/* Write all the fields of cp_binding_level instance BL to STREAM.  If
   REF_P is true, tree fields will be written as references.  */

static void
pph_stream_write_binding_level (pph_stream *stream, struct cp_binding_level *bl,
				bool ref_p)
{
  unsigned i;
  cp_class_binding *cs;
  cp_label_binding *sl;
  struct bitpack_d bp;

  if (!pph_start_record (stream, bl))
    return;

  pph_output_chain (stream, bl->names, ref_p);
  pph_output_uint (stream, bl->names_size);
  pph_output_chain (stream, bl->namespaces, ref_p);

  pph_stream_write_tree_vec (stream, bl->static_decls, ref_p);

  pph_output_chain (stream, bl->usings, ref_p);
  pph_output_chain (stream, bl->using_directives, ref_p);

  pph_output_uint (stream, VEC_length (cp_class_binding, bl->class_shadowed));
  for (i = 0; VEC_iterate (cp_class_binding, bl->class_shadowed, i, cs); i++)
    pph_stream_write_class_binding (stream, cs, ref_p);

  pph_output_tree_or_ref (stream, bl->type_shadowed, ref_p);

  pph_output_uint (stream, VEC_length (cp_label_binding, bl->shadowed_labels));
  for (i = 0; VEC_iterate (cp_label_binding, bl->shadowed_labels, i, sl); i++)
    pph_stream_write_label_binding (stream, sl, ref_p);

  pph_output_chain (stream, bl->blocks, ref_p);
  pph_output_tree_or_ref (stream, bl->this_entity, ref_p);
  pph_stream_write_binding_level (stream, bl->level_chain, ref_p);
  pph_stream_write_tree_vec (stream, bl->dead_vars_from_for, ref_p);
  pph_output_chain (stream, bl->statement_list, ref_p);
  pph_output_uint (stream, bl->binding_depth);

  bp = bitpack_create (stream->ob->main_stream);
  bp_pack_value (&bp, bl->kind, 4);
  bp_pack_value (&bp, bl->keep, 1);
  bp_pack_value (&bp, bl->more_cleanups_ok, 1);
  bp_pack_value (&bp, bl->have_cleanups, 1);
  pph_output_bitpack (stream, &bp);
}


/* Write all the fields of c_language_function instance CLF to STREAM.  If
   REF_P is true, all tree fields should be written as references.  */

static void
pph_stream_write_c_language_function (pph_stream *stream,
				      struct c_language_function *clf,
				      bool ref_p)
{
  if (!pph_start_record (stream, clf))
    return;

  pph_output_tree_or_ref (stream, clf->x_stmt_tree.x_cur_stmt_list, ref_p);
  pph_output_uint (stream, clf->x_stmt_tree.stmts_are_full_exprs_p);
}


/* Write all the fields of language_function instance LF to STREAM.  If
   REF_P is true, all tree fields should be written as references.  */

static void
pph_stream_write_language_function (pph_stream *stream,
				    struct language_function *lf,
				    bool ref_p)
{
  struct bitpack_d bp;

  if (!pph_start_record (stream, lf))
    return;

  pph_stream_write_c_language_function (stream, &lf->base, ref_p);
  pph_output_tree_or_ref (stream, lf->x_cdtor_label, ref_p);
  pph_output_tree_or_ref (stream, lf->x_current_class_ptr, ref_p);
  pph_output_tree_or_ref (stream, lf->x_current_class_ref, ref_p);
  pph_output_tree_or_ref (stream, lf->x_eh_spec_block, ref_p);
  pph_output_tree_or_ref (stream, lf->x_in_charge_parm, ref_p);
  pph_output_tree_or_ref (stream, lf->x_vtt_parm, ref_p);
  pph_output_tree_or_ref (stream, lf->x_return_value, ref_p);
  bp = bitpack_create (stream->ob->main_stream);
  bp_pack_value (&bp, lf->x_returns_value, 1);
  bp_pack_value (&bp, lf->x_returns_null, 1);
  bp_pack_value (&bp, lf->x_returns_abnormally, 1);
  bp_pack_value (&bp, lf->x_in_function_try_handler, 1);
  bp_pack_value (&bp, lf->x_in_base_initializer, 1);
  bp_pack_value (&bp, lf->can_throw, 1);
  pph_output_bitpack (stream, &bp);

  /* FIXME pph.  We are not writing lf->x_named_labels.  */

  pph_stream_write_binding_level (stream, lf->bindings, ref_p);
  pph_stream_write_tree_vec (stream, lf->x_local_names, ref_p);

  /* FIXME pph.  We are not writing lf->extern_decl_map.  */
}


/* Write all the fields of lang_decl_fn instance LDF to STREAM.  If REF_P
   is true, all tree fields should be written as references.  */

static void
pph_stream_write_ld_fn (pph_stream *stream, struct lang_decl_fn *ldf,
			bool ref_p)
{
  struct bitpack_d bp;

  if (!pph_start_record (stream, ldf))
    return;

  bp = bitpack_create (stream->ob->main_stream);
  bp_pack_value (&bp, ldf->operator_code, 16);
  bp_pack_value (&bp, ldf->global_ctor_p, 1);
  bp_pack_value (&bp, ldf->global_dtor_p, 1);
  bp_pack_value (&bp, ldf->constructor_attr, 1);
  bp_pack_value (&bp, ldf->destructor_attr, 1);
  bp_pack_value (&bp, ldf->assignment_operator_p, 1);
  bp_pack_value (&bp, ldf->static_function, 1);
  bp_pack_value (&bp, ldf->pure_virtual, 1);
  bp_pack_value (&bp, ldf->defaulted_p, 1);
  bp_pack_value (&bp, ldf->has_in_charge_parm_p, 1);
  bp_pack_value (&bp, ldf->has_vtt_parm_p, 1);
  bp_pack_value (&bp, ldf->pending_inline_p, 1);
  bp_pack_value (&bp, ldf->nonconverting, 1);
  bp_pack_value (&bp, ldf->thunk_p, 1);
  bp_pack_value (&bp, ldf->this_thunk_p, 1);
  bp_pack_value (&bp, ldf->hidden_friend_p, 1);
  pph_output_bitpack (stream, &bp);

  pph_output_tree_or_ref (stream, ldf->befriending_classes, ref_p);
  pph_output_tree_or_ref (stream, ldf->context, ref_p);

  if (ldf->thunk_p == 0)
    pph_output_tree_or_ref (stream, ldf->u5.cloned_function, ref_p);
  else if (ldf->thunk_p == 1)
    pph_output_uint (stream, ldf->u5.fixed_offset);
  else
    gcc_unreachable ();

  if (ldf->pending_inline_p == 1)
    pth_save_token_cache (ldf->u.pending_inline_info, stream);
  else if (ldf->pending_inline_p == 0)
    pph_stream_write_language_function (stream, ldf->u.saved_language_function,
					ref_p);
}


/* Write all the fields of lang_decl_ns instance LDNS to STREAM.  If REF_P
   is true, all tree fields should be written as references.  */

static void
pph_stream_write_ld_ns (pph_stream *stream, struct lang_decl_ns *ldns,
			bool ref_p)
{
  struct cp_binding_level *level;

  if (!pph_start_record (stream, ldns))
    return;

  level = ldns->level;
  pph_stream_write_binding_level (stream, level, ref_p);
}


/* Write all the fields of lang_decl_parm instance LDP to STREAM.  If REF_P
   is true, all tree fields should be written as references.  */

static void
pph_stream_write_ld_parm (pph_stream *stream, struct lang_decl_parm *ldp)
{
  if (!pph_start_record (stream, ldp))
    return;

  pph_output_uint (stream, ldp->level);
  pph_output_uint (stream, ldp->index);
}


/* Write all the lang-specific data in DECL to STREAM.  REF_P is true if
   the trees referenced in lang-specific fields should be written as
   references.  */

static void
pph_stream_write_lang_specific_data (pph_stream *stream, tree decl, bool ref_p)
{
  struct lang_decl *ld;
  struct lang_decl_base *ldb;

  gcc_assert (DECL_P (decl));

  ld = DECL_LANG_SPECIFIC (decl);
  if (!pph_start_record (stream, ld))
    return;
    
  /* Write all the fields in lang_decl_base.  */
  ldb = &ld->u.base;
  pph_stream_write_ld_base (stream, ldb);

  if (ldb->selector == 0)
    {
      /* Write all the fields in lang_decl_min.  */
      pph_stream_write_ld_min (stream, &ld->u.min, ref_p);
    }
  else if (ldb->selector == 1)
    {
      /* Write all the fields in lang_decl_fn.  */
      pph_stream_write_ld_fn (stream, &ld->u.fn, ref_p);
    }
  else if (ldb->selector == 2)
    {
      /* Write all the fields in lang_decl_ns.  */
      pph_stream_write_ld_ns (stream, &ld->u.ns, ref_p);
    }
  else if (ldb->selector == 3)
    {
      /* Write all the fields in lang_decl_parm.  */
      pph_stream_write_ld_parm (stream, &ld->u.parm);
    }
  else
    gcc_unreachable ();
}


/* Write header information for some AST nodes not handled by the
   common streamer code.  EXPR is the tree to write to output block
   OB.  If EXPR does not need to be handled specially, do nothing.  */

void
pph_stream_output_tree_header (struct output_block *ob, tree expr)
{
  pph_stream *stream = (pph_stream *) ob->sdata;

  if (TREE_CODE (expr) == CALL_EXPR)
    pph_output_uint (stream, call_expr_nargs (expr));
}


/* Callback for writing ASTs to a stream.  This writes all the fields
   that are not processed by default by the common tree pickler.
   OB and REF_P are as in lto_write_tree.  EXPR is the tree to write.  */

void
pph_stream_write_tree (struct output_block *ob, tree expr, bool ref_p)
{
  pph_stream *stream = (pph_stream *) ob->sdata;

  if (DECL_P (expr))
    {
      if (TREE_CODE (expr) == FUNCTION_DECL
	  || TREE_CODE (expr) == NAMESPACE_DECL
	  || TREE_CODE (expr) == PARM_DECL
	  || LANG_DECL_HAS_MIN (expr))
	{
	  pph_stream_write_lang_specific_data (stream, expr, ref_p);

	  if (TREE_CODE (expr) == FUNCTION_DECL)
	    pph_output_tree_aux (stream, DECL_SAVED_TREE (expr), ref_p);
	}
    }
  else if (TREE_CODE (expr) == STATEMENT_LIST)
    {
      tree_stmt_iterator i;
      unsigned num_stmts;

      /* Compute and write the number of statements in the list.  */
      for (num_stmts = 0, i = tsi_start (expr); !tsi_end_p (i); tsi_next (&i))
	num_stmts++;

      pph_output_uint (stream, num_stmts);

      /* Write the statements.  */
      for (i = tsi_start (expr); !tsi_end_p (i); tsi_next (&i))
	pph_output_tree_aux (stream, tsi_stmt (i), ref_p);
    }
}
