/* Copyright (C) 2015 Free Software Foundation, Inc.

   This file is part of the GNU Offloading and Multi Processing Library
   (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* This file handles the maintainence of threads on NVPTX.  */

#if defined __nvptx_softstack__ && defined __nvptx_unisimt__

#include "libgomp.h"
#include <stdlib.h>

struct gomp_thread *nvptx_thrs __attribute__((shared));

static void gomp_thread_start (struct gomp_thread_pool *);

static void __attribute__((noinline))
gomp_nvptx_main_1 (void (*fn) (void *), void *fn_data, int ntids, int tid)
{
  if (tid == 0)
    {
      gomp_global_icv.nthreads_var = ntids;
      /* Starting additional threads is not supported.  */
      gomp_global_icv.dyn_var = true;

      nvptx_thrs = gomp_malloc_cleared (ntids * sizeof (*nvptx_thrs));

      struct gomp_thread_pool *pool = gomp_malloc (sizeof (*pool));
      pool->threads = gomp_malloc (ntids * sizeof (*pool->threads));
      for (tid = 0; tid < ntids; tid++)
	pool->threads[tid] = nvptx_thrs + tid;
      pool->threads_size = ntids;
      pool->threads_used = ntids;
      pool->threads_busy = 1;
      pool->last_team = NULL;
      gomp_simple_barrier_init (&pool->threads_dock, ntids);

      nvptx_thrs[0].thread_pool = pool;
      asm ("bar.sync 0;");
      fn (fn_data);

      gomp_free_thread (nvptx_thrs);
      free (nvptx_thrs);
    }
  else
    {
      asm ("bar.sync 0;");
      gomp_thread_start (nvptx_thrs[0].thread_pool);
    }
}

void
gomp_nvptx_main (void (*fn) (void *), void *fn_data)
{
  int tid, ntids;
  asm ("mov.u32 %0, %%tid.y;" : "=r" (tid));
  asm ("mov.u32 %0, %%ntid.y;" : "=r" (ntids));
  char *stacks = 0;
  int *__nvptx_uni;
  asm ("cvta.shared.u64 %0, __nvptx_uni;" : "=r" (__nvptx_uni));
  __nvptx_uni[tid] = 0;
  if (tid == 0)
    {
      size_t stacksize = 131072;
      stacks = gomp_malloc (stacksize * ntids);
      char **__nvptx_stacks = 0;
      asm ("cvta.shared.u64 %0, __nvptx_stacks;" : "=r" (__nvptx_stacks));
      for (int i = 0; i < ntids; i++)
	__nvptx_stacks[i] = stacks + stacksize * (i + 1);
    }
  asm ("bar.sync 0;");
  gomp_nvptx_main_1 (fn, fn_data, ntids, tid);
  free (stacks);
}

/* This function is a pthread_create entry point.  This contains the idle
   loop in which a thread waits to be called up to become part of a team.  */

static void
gomp_thread_start (struct gomp_thread_pool *pool)
{
  struct gomp_thread *thr = gomp_thread ();

  gomp_sem_init (&thr->release, 0);
  thr->thread_pool = pool;

  for (;;)
    {
      gomp_simple_barrier_wait (&pool->threads_dock);
      if (!thr->fn)
	continue;
      thr->fn (thr->data);
      thr->fn = NULL;

      struct gomp_task *task = thr->task;
      gomp_team_barrier_wait_final (&thr->ts.team->barrier);
      gomp_finish_task (task);
    }
}

/* Launch a team.  */

void
gomp_team_start (void (*fn) (void *), void *data, unsigned nthreads,
		 unsigned flags, struct gomp_team *team)
{
  struct gomp_thread *thr, *nthr;
  struct gomp_task *task;
  struct gomp_task_icv *icv;
  struct gomp_thread_pool *pool;
  unsigned long nthreads_var;

  thr = gomp_thread ();
  pool = thr->thread_pool;
  task = thr->task;
  icv = task ? &task->icv : &gomp_global_icv;

  /* Always save the previous state, even if this isn't a nested team.
     In particular, we should save any work share state from an outer
     orphaned work share construct.  */
  team->prev_ts = thr->ts;

  thr->ts.team = team;
  thr->ts.team_id = 0;
  ++thr->ts.level;
  if (nthreads > 1)
    ++thr->ts.active_level;
  thr->ts.work_share = &team->work_shares[0];
  thr->ts.last_work_share = NULL;
  thr->ts.single_count = 0;
  thr->ts.static_trip = 0;
  thr->task = &team->implicit_task[0];
  nthreads_var = icv->nthreads_var;
  gomp_init_task (thr->task, task, icv);
  team->implicit_task[0].icv.nthreads_var = nthreads_var;

  if (nthreads == 1)
    return;

  /* Release existing idle threads.  */
  for (unsigned i = 1; i < nthreads; ++i)
    {
      nthr = pool->threads[i];
      nthr->ts.team = team;
      nthr->ts.work_share = &team->work_shares[0];
      nthr->ts.last_work_share = NULL;
      nthr->ts.team_id = i;
      nthr->ts.level = team->prev_ts.level + 1;
      nthr->ts.active_level = thr->ts.active_level;
      nthr->ts.single_count = 0;
      nthr->ts.static_trip = 0;
      nthr->task = &team->implicit_task[i];
      gomp_init_task (nthr->task, task, icv);
      team->implicit_task[i].icv.nthreads_var = nthreads_var;
      nthr->fn = fn;
      nthr->data = data;
      team->ordered_release[i] = &nthr->release;
    }

  gomp_simple_barrier_wait (&pool->threads_dock);
}

#include "../../team.c"
#endif
