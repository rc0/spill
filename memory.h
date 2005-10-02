/*
  spill - segregated package install logical linker

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2003, 2004
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * 
 **********************************************************************
 */

#ifndef MEMORY_H
#define MEMORY_H

/*{{{ Safe alloc helpers (GCC extensions) */

/* Poor man's implementation for now. */
#define out_of_mem(x, y, z) _exit(1)

static __inline__ void* safe_malloc(char *file, int line, size_t s)/*{{{*/
{
  void *x = malloc(s);
#ifdef TEST_OOM
  total_bytes += s;
  if (total_bytes > 131072) x = NULL;
#endif
  if (!x) out_of_mem(file, line, s);
  return x;
}
/*}}}*/
static __inline__ void* safe_realloc(char *file, int line, void *old_ptr, size_t s)/*{{{*/
{
  void *x = realloc(old_ptr, s);
  if (!x) out_of_mem(file, line, s);
  return x;
}
/*}}}*/
#define Malloc(s) safe_malloc(__FILE__, __LINE__, s)
#define Realloc(xx,s) safe_realloc(__FILE__, __LINE__,xx,s)
/*}}}*/

/*{{{  Memory macros*/
#define new_string(s) strcpy((char *) Malloc(1+strlen(s)), (s))
#define extend_string(x,s) (strcat(Realloc(x, (strlen(x)+strlen(s)+1)), s))
#define new(T) (T *) Malloc(sizeof(T))
#define new_array(T, n) (T *) Malloc(sizeof(T) * (n))
#define grow_array(T, n, oldX) (T *) ((oldX) ? Realloc(oldX, (sizeof(T) * (n))) : Malloc(sizeof(T) * (n)))
#define EMPTY(x) {&(x), &(x)}
/*}}}*/

#endif /* MEMMAC_H */

