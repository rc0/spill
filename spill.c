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

/*
 * Naming :
 * "source" means the directory tree where the package is actually installed.
 * "destination" means the directory where the symbolic links will be which
 *  point into "source"
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "memory.h"
#include "version.h"

#define RECORD_DIR ".spill"

struct string_node {/*{{{*/
  struct string_node *next;
  char *string;
};
/*}}}*/

static struct string_node *ignores = NULL;
static FILE *conflict_file = NULL;
  
static char *caten(const char *s1, const char *s2)/*{{{*/
{ 
  int n1, n2, n;
  char *res;
  
  n1 = strlen(s1);
  n2 = strlen(s2);
  n = n1 + n2;

  res = new_array(char, n + 1);
  if (n1 > 0) memcpy(res, s1, n1);
  if (n2 > 0) memcpy(res+n1, s2, n2);
  res[n1+n2] = 0;
  return res;
}
/*}}}*/
static char *dfcaten(const char *d, const char *f)/*{{{*/
{ 
  int nd, nf, n;
  char *res;
  
  nd = strlen(d);
  nf = strlen(f);
  n = nd + nf;

  res = new_array(char, n + 2);
  if (nd > 0) memcpy(res, d, nd);
  res[nd] = '/';
  if (nf > 0) memcpy(res+nd+1, f, nf);
  res[nd+nf+1] = 0;
  return res;
}
/*}}}*/
static char *dfcaten3(const char *x, const char *y, const char *z)/*{{{*/

{ 
  int nx, ny, nz, n;
  char *res;
  
  nx = strlen(x);
  ny = strlen(y);
  nz = strlen(z);
  n = nx + ny + nz;

  res = new_array(char, n + 3);
  if (nx > 0) memcpy(res, x, nx);
  res[nx] = '/';
  if (ny > 0) memcpy(res+nx+1, y, ny);
  res[nx+ny+1] = '/';
  if (nz > 0) memcpy(res+nx+ny+2, z, nz);
  res[nx+ny+nz+2] = 0;
  return res;
}
/*}}}*/
static char *normalise_dir(const char *dir)/*{{{*/
{
  /* Given a relative path, find its absolute path when starting from pwd */
  char startdir[PATH_MAX];
  char srcdir[PATH_MAX];

  if (getcwd(startdir, PATH_MAX) == NULL) {
    fprintf(stderr, "Couldn't get start directory!\n");
    exit(1);
  }

  if (chdir(dir) < 0) {
    fprintf(stderr, "Couldn't cd to %s : %s!\n", dir, strerror(errno));
    exit(1);
  }

  if (getcwd(srcdir, PATH_MAX) == NULL) {
    fprintf(stderr, "Couldn't read absolute src directory!\n");
    exit(1);
  }

  if (chdir(startdir) < 0) {
    fprintf(stderr, "Couldn't cd back to starting directory %s!\n", startdir);
    exit(1);
  }

  return new_string(srcdir);

}
/*}}}*/
static char *cleanup_dir(const char *dir)/*{{{*/
{
  /* Remove doubled / and trailing /, unless the / was the only char (i.e.
   * install into /bin, /sbin etc) */
  int len;
  char *res;
  int slash;
  char *p;
  const char *q;
  len = strlen(dir);
  res = new_array(char, len + 1);
  p = res;
  q = dir;
  slash = 0;
  while (*q) {
    if (*q == '/') {
      if (!slash) *p++ = *q;
      slash = 1;
    } else {
      *p++ = *q;
      slash = 0;
    }
    q++;
  }
  if (slash) p--;
  *p = 0;
  return res;
}
/*}}}*/
static int check_sane(const char *tag, const char *dir)/*{{{*/
{
  /* Look at contents of directory, make sure the expected directories are present. */
  DIR *d;
  struct dirent *de;
  struct stat sb;
  int has_bindir = 0;
  int has_sbindir = 0;
  int has_libdir = 0;

  if (!strcmp(dir, "")) d = opendir("/");
  else                  d = opendir(dir);
  if (d) {
    while ((de = readdir(d))) {
      char *full_path = dfcaten(dir, de->d_name);
      if (stat(full_path, &sb) < 0) {
        fprintf(stderr, "Couldn't stat %s", full_path);
      } else {
        if (S_ISDIR(sb.st_mode)) {
          has_bindir |= !strcmp(de->d_name, "bin");
          has_sbindir |= !strcmp(de->d_name, "sbin");
          has_libdir |= !strcmp(de->d_name, "lib");
        }
      }
      free(full_path);
    }
    closedir(d);
  } else {
    fprintf(stderr, "%s directory %s coudn't be opened.\n", tag, dir);
    return 0;
    exit(1);
  }

  if (!has_bindir && !has_libdir && !has_sbindir) {
    fprintf(stderr, "%s directory %s doesn't have bin, sbin or lib subdirectory\n", tag, dir);
    return 0;
  }
  return 1;
}
/*}}}*/
static void extract_package_details(const char *src, char **pkg, char **version)/*{{{*/
{
  /* Given the path to the installation area, pull the final two pathname
   * components out as the package and version+build */

  const char *end, *s1, *s2;
  end = src;
  while (*end) end++;
  s1 = end;
  while ((s1 >= src) && (*s1 != '/')) s1--;
  s2 = s1 - 1;
  while ((s2 >= src) && (*s2 != '/')) s2--;

  *pkg = new_array(char, (s1 - s2));
  *version = new_array(char, (end - s1));

  memcpy(*pkg, s2+1, s1-s2-1);
  (*pkg)[s1-s2-1] = 0;

  memcpy(*version, s1+1, end-s1);
  return;

}
/*}}}*/

static void add_ignore(char *path)/*{{{*/
{
  struct string_node *n;
  n = new(struct string_node);
  n->string = new_string(path);
  n->next = ignores;
  ignores = n;
}
/*}}}*/
static int check_ignore(const char *head, const char *tail)/*{{{*/
{
  int len;
  char *path;
  struct string_node *a;
  int result;
  
  /* Early out */
  if (!ignores) return 0;
  
  /* 'head' starts with a '/' that we ignore.  We put a '/' between head and tail.  +1 for the terminating null. */
  len = strlen(head) + strlen(tail) + 1;
  path = new_array(char, len);
  strcpy(path, head + 1);
  strcat(path, "/");
  strcat(path, tail);
  result = 0;
  a = ignores;
  do {
    if (!strcmp(a->string, path)) {
      result = 1;
      break;
    }
    a = a->next;
  } while (a);
  
  free(path);
  return result;
}
/*}}}*/

struct options {/*{{{*/
  unsigned quiet:1;
  unsigned dry_run:1;
  unsigned expand:1;
  unsigned force:1;
  unsigned override:1;
};
/*}}}*/
enum source_type {/*{{{*/
  ST_ERROR,
  ST_DIR,
  ST_OTHER
};
/*}}}*/
enum dest_type {/*{{{*/
  DT_VOID,              /* nothing there */
  DT_ERROR,             /* some error examining it */
  DT_LINK_EXACT,        /* readlink(dest) identical with source */
  DT_LINK_SAME_SAME,    /* readlink(dest) appears to link to same version of
                           same package as source, but installed elsewhere (or
                           with a different representation of the same path.)
                           NOTE .. don't check (dev,inode) to see if it's
                           LINK_EXACT, because the user might want to replace
                           the package with relative links to replace absolute
                           etc. 
                           */
  DT_LINK_SAME_OTHER,   /* link to same package, other version */
  DT_LINK_OTHER_DIR,    /* link to directory in something else */
  DT_LINK_OTHER_FILE,   /* link to file/dev/fifo/socket in something else */
  DT_LINK_UNKNOWN,      /* link to something, readlink() doesn't have right format. */
  DT_DIRECTORY,         /* directory */
  DT_OTHER              /* file, device, fifo, socket etc */
};
/*}}}*/
typedef int (*action_fn)/*{{{*/
(enum source_type src_type,
 enum dest_type dest_type,

 const char *relative_path,

 const char *full_src_path,
 const char *full_dest_path,
            
 const char *src,
 const char *dest,
 const char *taildir,
 const char *tailfile,

 const char *pkg,
 const char *version,

 const char *other_pkg,
 const char *other_version,

 struct options *opt
);
/*}}}*/

static int
traverse_action(const char *rel_path,
                const char *src,
                const char *dest,
                const char *pkg,
                const char *version,
                const char *tail,
                struct options *opt,
                action_fn fn);

/*{{{ static enum dest_type find_dest_type*/
static enum dest_type 
find_dest_type(const char *full_dest_path,
               const char *full_src_path,
               const char *relative_path,
               const char *taildir,
               const char *tailfile,
               const char *pkg,
               const char *version,
               char **res_other_pkg,
               char **res_other_version
               )
{
  struct stat dsb;
  enum dest_type result;
  char *src_path;
  if (relative_path) {
    char *tail = dfcaten(taildir, tailfile);
    src_path = caten(relative_path, tail);
    free(tail);
  } else {
    src_path = new_string(full_src_path);
  }
  
  if (lstat(full_dest_path, &dsb) < 0) {
    if (errno == ENOENT) {
      result = DT_VOID;
    } else {
      fprintf(stderr, "Couldn't stat <%s> : %s!\n", full_dest_path, strerror(errno));
      result = DT_ERROR;
    }
  } else {
    /* stat ok */
    if (S_ISDIR(dsb.st_mode)) {
      result = DT_DIRECTORY;
    } else if (S_ISLNK(dsb.st_mode)) {
      /* Decide whether the link is to the same package or not. */
      char linkbuf[PATH_MAX];
      int link_len;
      link_len = readlink(full_dest_path, linkbuf, PATH_MAX);
      if (link_len < 0) {
        fprintf(stderr, "Couldn't readlink on <%s> : %s!\n", full_dest_path, strerror(errno));
        result = DT_ERROR;
      } else {
        /* linkbuf is where the link points to. The tail part of it
         * should be '/tail/de->d_name', the part before this should be
         * the path to the package base for which the link has already
         * been installed. */
        char *tail_part;
        int tail_len;
        
        linkbuf[link_len] = 0;
        tail_part = dfcaten(taildir, tailfile);
        tail_len = strlen(tail_part);
        if (tail_len > link_len) {
          result = DT_ERROR;
          /* Obviously can't be a link pointing to an install area like the
             sort spill creates itself, it must be a link pointing to something
             else.  No point reporting this specifically, it's just an
             uncorrectable error. */
        } else {
          if (!strcmp(tail_part, linkbuf + link_len - tail_len)) {
            /* Matched, deal with prefix. */
            char *link_prefix;
            char *other_pkg, *other_version;
            int prefix_len;
            prefix_len = link_len - tail_len;
            link_prefix = new_array(char, 1 + prefix_len);
            memcpy(link_prefix, linkbuf, prefix_len);
            link_prefix[prefix_len] = 0;
            extract_package_details(link_prefix, &other_pkg, &other_version);
            if (res_other_pkg) *res_other_pkg = new_string(other_pkg);
            if (res_other_version) *res_other_version = new_string(other_version);
            if (!strcmp(linkbuf, src_path)) {
              result = DT_LINK_EXACT;
            } else if (!strcmp(other_pkg, pkg)) {
              if (!strcmp(other_version, version)) {
                /* Same version */
                /* FIXME : what if the links point to another place where the package is installed,
                 * insead of the "source" we're doing now?  Ought to fix this. */
                result = DT_LINK_SAME_SAME;
              } else {
                result = DT_LINK_SAME_OTHER;
              }
            } else {
              /* Links to another package. */
              /* Check if link is to a directory, then explode it and retry. */
              struct stat lsb;
              if (stat(linkbuf, &lsb) < 0) {
                fprintf(stderr, "** ERROR, link at <%s> is stale, remove this and retry!\n", full_dest_path);
                result = DT_ERROR;
              } else {
                if (S_ISDIR(lsb.st_mode)) {
                  result = DT_LINK_OTHER_DIR;
                } else {
                  result = DT_LINK_OTHER_FILE;
                }
              }
            }

            free(link_prefix);
            free(other_pkg);
            free(other_version);
          } else {
            result = DT_LINK_UNKNOWN;
          }
        }

        free(tail_part);
      }
      
    } else {
      result = DT_OTHER;
    }
  }

  free(src_path);
  return result;
}
/*}}}*/

struct string_list {/*{{{*/
  struct string_list *next;
  struct string_list *prev;
  char *string;
};
/*}}}*/
static struct string_list *new_string_list(void)/*{{{*/
{
  struct string_list *res;
  res = new(struct string_list);
  res->next = res->prev = res;
  res->string = NULL;
  return res;
}
/*}}}*/
static void append(struct string_list *head, char *string) {/*{{{*/
  struct string_list *new_cell;
  new_cell = new_string_list();
  new_cell->string = new_string(string);
  new_cell->prev = head->prev;
  new_cell->next = head;
  head->prev->next = new_cell;
  head->prev = new_cell;
}
  /*}}}*/
static void free_string_list(struct string_list *head)/*{{{*/
{
  struct string_list *x, *next;
  for (x=head->next; x!=head; x=next) {
    next = x->next;
    if (x->string) free(x->string);
    free(x);
  }
}
/*}}}*/
static void emit_conflict(const char *full_dest_path)/*{{{*/
{
  if (conflict_file) {
    fprintf(conflict_file, "%s\n", full_dest_path);
  }
  return;
}
/*}}}*/
static int files_differ(const char *name1, const char *name2)/*{{{*/
{
  /* Return 0 if files match, 1 if they differ, 2 if there was a problem. */
  FILE *x1, *x2;
  struct stat sb;

  if ((stat(name1, &sb) < 0) || (!S_ISREG(sb.st_mode))) return 2;
  if ((stat(name2, &sb) < 0) || (!S_ISREG(sb.st_mode))) return 2;

  x1 = fopen(name1, "rb");
  x2 = fopen(name2, "rb");
  if (x1 && x2) {
    int result = 0;
    int c1, c2;
    do {
      c1 = getc(x1);
      c2 = getc(x2);
      if (c1 == EOF) {
        if (c2 == EOF) {
          result = 0;
          break;
        } else {
          result = 1;
          break;
        }
      } else if (c2 == EOF) {
        result = 1;
        break;
      } else if (c1 != c2) {
        result = 1;
        break;
      }
    } while (1);
    fclose(x1);
    fclose(x2);
    return result;
  } else {
    if (x1) fclose(x1);
    if (x2) fclose(x2);
    return 2;
  }
}
/*}}}*/

static int do_expand(const char *dir_link, struct options *opt)/*{{{*/
{
  /* Given the path to a symbolic link that points at a directory, replace that
     link by a directory, and inside that new directory create symbolic links
     that point at each entry in the directory to which the removed link used
     to point. 
     
     If the existing link was absolute, create absolute links.  Otherwise,
     create relative links.

     Return 1 if an error occurs, 0 otherwise. 
     */

  char buffer[PATH_MAX];
  int link_len;
  int is_absolute;
  DIR *d;
  struct dirent *de;
  struct stat link_stat;

  link_len = readlink(dir_link, buffer, PATH_MAX);
  if (link_len < 0) {
    printf("!! ERROR Could not expand link <%s> into a directory : %s\n",
           dir_link, strerror(errno));
    return 1;
  }

  buffer[link_len] = '\0';

  /* Get the stat record for the directory that the link points to.  We'll use
     its mode when creating the replacement directory, for want of something
     better. */
  if (stat(buffer, &link_stat) < 0) {
    printf("!! ERROR Could not stat link <%s> : %s\n",
           buffer, strerror(errno));
    return 1;
  }
  
  is_absolute = (buffer[0] == '/') ? 1 : 0;

  /* TODO */
  d = opendir(buffer);
  if (d) {
    /* Build list of directory entries. */
    struct string_list *sl = new_string_list();
    struct string_list *x;
    while ((de = readdir(d))) {
      if (!strcmp(de->d_name, ".")) continue;
      if (!strcmp(de->d_name, "..")) continue;
      append(sl, de->d_name);
    }
    closedir(d);
    /* Now clear the link, put a directory in its place and create a set of
     * links inside. */
    if (unlink(dir_link) < 0) {
      printf("!! ERROR Could not remove the link at <%s> : %s\n",
             dir_link, strerror(errno));
      free_string_list(sl);
      return 1;
    }

    if (mkdir (dir_link, link_stat.st_mode) < 0) {
      printf("!! ERROR Could not create new directory at <%s> : %s\n",
             dir_link, strerror(errno));
      free_string_list(sl);
      return 1;
    }

    /* Now populate it with links */
    for (x=sl->next; x!=sl; x=x->next) {
      char *link_site, *target_site;
      link_site = dfcaten(dir_link, x->string);
      if (is_absolute) {
        target_site = dfcaten(buffer, x->string);
      } else {
        target_site = dfcaten3("..", buffer, x->string);
      }
      if (symlink(target_site, link_site) < 0) {
        printf("!! ERROR Could not create symlink from <%s> to <%s> : %s\n",
               link_site, target_site, strerror(errno));
        free_string_list(sl);
        return 1;
      }
      if (!opt->quiet) {
        printf("** EXPANDDIR Created link from <%s> to <%s>\n",
               link_site, target_site);
      }
      free(link_site);
      free(target_site);
    }
    free_string_list(sl);
    
  } else {
    printf("!! ERROR Could not open directory <%s> to read contents : %s\n",
           dir_link, strerror(errno));
    return 1;
  }

  return 0;

}
/*}}}*/
/*{{{ static int pre_install*/
static int
pre_install(enum source_type src_type,
            enum dest_type dest_type,

            const char *relative_path,

            const char *full_src_path,
            const char *full_dest_path,

            const char *src,
            const char *dest,
            const char *taildir,
            const char *tailfile,

            const char *pkg,
            const char *version,

            const char *other_pkg,
            const char *other_version,

            struct options *opt
            )
{
  char *new_tail;
  int result = -1;
  int verbose = opt->dry_run && !opt->quiet;
  char *src_path;
  char *new_relative_path;

  if (relative_path) {
    char *tail = dfcaten(taildir, tailfile);
    src_path = caten(relative_path, tail);
    free(tail);
  } else {
    src_path = new_string(full_src_path);
  }

  /* (otherwise user gets messages twice.) */
  
  switch (src_type) {
  case ST_ERROR:
    fprintf(stderr, "Could not examine source <%s>!\n", src_path);
    result = 1;
    break;
  case ST_DIR:
    switch (dest_type) {
      case DT_VOID:
        if (verbose) printf("** NEWDIRLINK from <%s> to <%s>\n", full_dest_path, src_path);
        result = 0;
        break;
      case DT_ERROR:
        printf("!! ERROR can't examine path <%s>\n", full_dest_path);
        emit_conflict(full_dest_path);
        result = 1;
        break;
      case DT_LINK_EXACT:
        if (verbose) printf("** OK dir <%s> already linked to the required path\n",
                            full_dest_path);
        result = 0;
        break;
      case DT_LINK_SAME_SAME:
        if (verbose) printf("** REPLACEDIR dir <%s> linked to <%s> through another path\n",
                            full_dest_path, pkg);
        result = 0;
        break;
      case DT_LINK_SAME_OTHER:
        if (verbose) printf("** REPLACEDIR <%s> linked to other version <%s> of package <%s>\n",
                            full_dest_path, other_version, other_pkg);
        result = 0;
        break;
      case DT_LINK_OTHER_DIR:
        if (!opt->expand) {
          printf("!! NEEDEXPN <%s> linked to a directory in version <%s> of package <%s>\n",
                full_dest_path, other_version, other_pkg);
          result = 1; /* User has to manually resolve this one. */
          break;
        } else {
          result = do_expand(full_dest_path, opt);
          if (result) break; /* Error occurred whilst expanding, don't proceed */
          /* OK, expansion worked, now treat as though it's a directory. */
        }
        /* NOTE: DELIBERATE FALL THROUGH FROM ELSE BRANCH */
      case DT_DIRECTORY:
        new_tail = dfcaten(taildir, tailfile);
        new_relative_path = relative_path ? dfcaten("..", relative_path) : NULL;
        result = traverse_action(new_relative_path, src, dest, pkg, version, new_tail, opt, pre_install);
        free(new_tail);
        if (new_relative_path) free(new_relative_path);
        break;
      case DT_LINK_OTHER_FILE:
        if (opt->override) {
          printf("** OVERRIDE <%s> linked to a non-directory in version <%s> of package <%s>\n",
                full_dest_path, other_version, other_pkg);
          result = 0;
        } else {
          printf("!! CONFLICT <%s> linked to a non-directory in version <%s> of package <%s>\n",
                full_dest_path, other_version, other_pkg);
          result = 1; /* User has to manually resolve this one. */
          emit_conflict(full_dest_path);
        }
        break;
      case DT_LINK_UNKNOWN:
        if (opt->override) {
          printf("** OVERWRITE <%s> linked to something I don't understand\n",
                full_dest_path);
          result = 0;
        } else {
          printf("!! CONFLICT <%s> linked to something I don't understand\n",
                full_dest_path);
          result = 1; /* User has to manually resolve this one. */
          emit_conflict(full_dest_path);
        }
        break;
      case DT_OTHER:
        printf("!! CONFLICT <%s> is not a link or directory\n",
               full_dest_path);
        result = 1;
        emit_conflict(full_dest_path);
        break;
    }
    break;

  case ST_OTHER:
    switch (dest_type) {
      case DT_VOID:
        if (verbose) printf("** NEWLINK from <%s> to <%s>\n", full_dest_path, src_path);
        result = 0;
        break;
      case DT_ERROR:
        printf("!! ERROR can't examine path <%s>\n", full_dest_path);
        result = 1;
        emit_conflict(full_dest_path);
        break;
      case DT_LINK_EXACT:
        if (verbose) printf("** OK <%s> already linked to the required path\n",
                            full_dest_path);
        result = 0;
        break;
      case DT_LINK_SAME_SAME:
        if (verbose) printf("** REPLACE <%s> linked to <%s> through another path\n",
                            full_dest_path, pkg);
        result = 0;
        break;
      case DT_LINK_SAME_OTHER:
        if (verbose) printf("** REPLACE <%s> linked to other version <%s> of package <%s>\n",
                            full_dest_path, other_version, other_pkg);
        result = 0;
        break;
      case DT_LINK_OTHER_DIR:
        if (opt->override) {
          printf("** OVERRIDE <%s> linked to a directory in version <%s> of package <%s>\n",
                full_dest_path, other_version, other_pkg);
          result = 0;
        } else {
          printf("!! CONFLICT <%s> linked to a directory in version <%s> of package <%s>\n",
                 full_dest_path, other_version, other_pkg);
          result = 1; /* User has to manually resolve this one. */
          emit_conflict(full_dest_path);
        }
        break;
      case DT_LINK_OTHER_FILE:
        if (opt->override) {
          int d = files_differ(full_dest_path, src_path);
          printf("** OVERRIDE <%s> linked to a non-directory in version <%s> of package <%s>%s\n",
                full_dest_path, other_version, other_pkg,
                (d == 0) ? " (content identical)" : 
                (d == 1) ? " (content differs)" : ""
                );
          result = 0;
        } else {
          printf("!! CONFLICT <%s> linked to a non-directory in version <%s> of package <%s>\n",
                 full_dest_path, other_version, other_pkg);
          result = 1; /* User has to manually resolve this one. */
          emit_conflict(full_dest_path);
        }
        break;
      case DT_LINK_UNKNOWN:
        if (opt->override) {
          int d = files_differ(full_dest_path, src_path);
          printf("** OVERWRITE <%s> linked to something I don't understand%s\n",
                full_dest_path,
                (d == 0) ? " (content identical)" : 
                (d == 1) ? " (content differs)" : ""
                );
          result = 0;
        } else {
          printf("!! CONFLICT <%s> linked to something I don't understand\n",
                 full_dest_path);
          result = 1; /* User has to manually resolve this one. */
          emit_conflict(full_dest_path);
        }
        break;
      case DT_DIRECTORY:
        printf("!! CONFLICT <%s> is a directory, can't link to <%s>\n",
               full_dest_path, src_path);
        result = 1; /* User has to manually resolve this one. */
        emit_conflict(full_dest_path);
        break;
      case DT_OTHER:
        printf("!! CONFLICT <%s> is not a link or directory, can't link to <%s>\n",
               full_dest_path, src_path);
        result = 1;
        emit_conflict(full_dest_path);
        break;
    }
    break;
  }
  free(src_path);
  assert (result >= 0);
  return result;
}
/*}}}*/
/*{{{ static int do_install */
static int
do_install (enum source_type src_type,
            enum dest_type dest_type,

            const char *relative_path,

            const char *full_src_path,
            const char *full_dest_path,

            const char *src,
            const char *dest,
            const char *taildir,
            const char *tailfile,

            const char *pkg,
            const char *version,

            const char *other_pkg,
            const char *other_version,

            struct options *opt
            )
{

  /* FIXME : There's lots of common code here that should be made explicitly common */
  
  char *new_tail;
  char *new_relative_path;
  char *linked_path;
  int result;
  
  if (relative_path) {
    char *tail = dfcaten(taildir, tailfile);
    linked_path = caten(relative_path, tail);
    free(tail);
  } else {
    linked_path = new_string(full_src_path);
  }
  
  switch (src_type) {
  case ST_ERROR:
    fprintf(stderr, "Could not examine source <%s>!\n", full_src_path);
    free(linked_path);
    return 1;
    break;
  case ST_DIR:
    switch (dest_type) {
      case DT_VOID:
        if (symlink(linked_path, full_dest_path) < 0) {
          printf("!! FAILED : can't create symlink from <%s> to <%s> : %s\n",
                 full_dest_path, linked_path, strerror(errno));
          free(linked_path);
          return 1;
        }
        if (!opt->quiet) printf("** NEWDIRLINK from <%s> to <%s>\n", full_dest_path, linked_path);
        free(linked_path);
        return 0;
      case DT_LINK_EXACT:
        /* Link already exists pointing to the right place.  No-op for installing. */
        if (!opt->quiet) printf("** OK dir <%s> already linked to the required path <%s>\n",
                                full_dest_path, linked_path);
        free(linked_path);
        return 0;
      case DT_LINK_SAME_SAME:
      case DT_LINK_SAME_OTHER:
        if (unlink(full_dest_path) < 0) {
          printf("!! FAILED : can't remove old link <%s> : %s\n", full_dest_path, strerror(errno));
          free(linked_path);
          return 1;
        } else {
          if (symlink(linked_path, full_dest_path) < 0) {
            printf("!! FAILED : can't create symlink from <%s> to <%s> : %s\n",
                  full_dest_path, linked_path, strerror(errno));
            free(linked_path);
            return 1;
          } else {
            if (!opt->quiet) {
              printf("** REPLACEDIR <%s> previously linked to version <%s> of package <%s>\n",
                     full_dest_path, other_version, other_pkg);
            }
          }
          free(linked_path);
        }
        return 0;
      case DT_DIRECTORY:
        new_tail = dfcaten(taildir, tailfile);
        new_relative_path = relative_path ? dfcaten("..", relative_path) : NULL;
        result = traverse_action(new_relative_path, src, dest, pkg, version, new_tail, opt, do_install);
        free(new_tail);
        free(new_relative_path);
        free(linked_path);
        return result;
      case DT_LINK_OTHER_DIR:
      case DT_LINK_OTHER_FILE:
      case DT_LINK_UNKNOWN:
        if (opt->override) {
          if (unlink(full_dest_path) < 0) {
            printf("!! FAILED : can't remove old link <%s> : <%s>\n", full_dest_path, strerror(errno));
            free(linked_path);
            return 1;
          } else {
            if (symlink(linked_path, full_dest_path) < 0) {
              printf("!! FAILED : can't create override symlink from <%s> to <%s> : %s\n",
                     full_dest_path, linked_path, strerror(errno));
              free(linked_path);
              return 1;
            }
            if (!opt->quiet) printf("** NEWDIRLINK (OVERRIDE) from <%s> to <%s>\n", full_dest_path, linked_path);
            free(linked_path);
            return 0;
          }
        } else {
          /* No override, fall through */
        }
      case DT_ERROR:
      case DT_OTHER:
        printf("!! CALAMITY : I shouldn't be here, my pre-install check should have failed (problem path=<%s>)!\n",
               full_dest_path);
        free(linked_path);
        return 1;
    }
    break;

  case ST_OTHER:
    switch (dest_type) {
      case DT_VOID:
        if (symlink(linked_path, full_dest_path) < 0) {
          printf("!! FAILED : can't create symlink from <%s> to <%s> : %s\n",
                 full_dest_path, linked_path, strerror(errno));
          free(linked_path);
          return 1;
        }
        if (!opt->quiet) printf("** NEWLINK from <%s> to <%s>\n", full_dest_path, linked_path);
        free(linked_path);
        return 0;
      case DT_LINK_EXACT:
        if (!opt->quiet) printf("** OK <%s> already linked to required path <%s>\n",
                                full_dest_path, linked_path);
        free(linked_path);
        return 0;
      case DT_LINK_SAME_SAME:
      case DT_LINK_SAME_OTHER:
        if (unlink(full_dest_path) < 0) {
          printf("!! FAILED : can't remove old link <%s> : %s\n", full_dest_path, strerror(errno));
          return 1;
        } else {
          if (symlink(linked_path, full_dest_path) < 0) {
            printf("!! FAILED : can't create symlink from <%s> to <%s> : %s\n",
                  full_dest_path, linked_path, strerror(errno));
            free(linked_path);
            return 1;
          } else {
            if (!opt->quiet) printf("** REPLACE <%s> previously linked to other version <%s> of package <%s>\n",
                                    full_dest_path, other_version, other_pkg);
          }
        }
        free(linked_path);
        return 0;
      case DT_LINK_OTHER_DIR:
      case DT_LINK_OTHER_FILE:
      case DT_LINK_UNKNOWN:
        if (opt->override) {
          if (unlink(full_dest_path) < 0) {
            printf("!! FAILED : can't remove old link <%s> : <%s>\n", full_dest_path, strerror(errno));
            free(linked_path);
            return 1;
          } else {
            if (symlink(linked_path, full_dest_path) < 0) {
              printf("!! FAILED : can't create override symlink from <%s> to <%s> : %s\n",
                     full_dest_path, linked_path, strerror(errno));
              free(linked_path);
              return 1;
            }
            if (!opt->quiet) printf("** NEWLINK (OVERRIDE) from <%s> to <%s>\n", full_dest_path, linked_path);
            free(linked_path);
            return 0;
          }
        } else {
          /* No override, fall through */
        }
      case DT_DIRECTORY:
      case DT_ERROR:
      case DT_OTHER:
        printf("!! CALAMITY : I shouldn't be here, my pre-install check should have failed (problem path=<%s>)!\n",
                full_dest_path);
        free(linked_path);
        return 1;
    }
    break;
  }
  assert(0);
  free(linked_path);
  return 1; /* shouldn't get here. */
}
/*}}}*/

/*{{{ static int soft_delete */
static int
soft_delete(enum source_type src_type,
            enum dest_type dest_type,

            const char *relative_path,

            const char *full_src_path,
            const char *full_dest_path,

            const char *src,
            const char *dest,
            const char *taildir,
            const char *tailfile,

            const char *pkg,
            const char *version,

            const char *other_pkg,
            const char *other_version,

            struct options *opt
            )
{

  /* Regarding return status:
     1 indicates error : there was a link to be removed, but we couldn't
       remove it.
     0 otherwise. */
  char *new_tail;
  char *new_relative_path;
  char *linked_path;
  int result;
  
  if (relative_path) {
    char *tail = dfcaten(taildir, tailfile);
    linked_path = caten(relative_path, tail);
    free(tail);
  } else {
    linked_path = new_string(full_src_path);
  }
  
  switch (src_type) {
  case ST_ERROR:
    fprintf(stderr, "Could not examine source <%s>!\n", full_src_path);
    free(linked_path);
    return 0;
    break;
  case ST_DIR:
    switch (dest_type) {
      case DT_LINK_EXACT:
        free(linked_path);
        if (unlink(full_dest_path) < 0) {
          printf("!! FAILED : unable to remove link at <%s>\n", full_dest_path);
          return 1;
        } else {
          if (!opt->quiet) printf("** SUCCESS : removed link at <%s>\n", full_dest_path);
          return 0;
        }

      case DT_DIRECTORY:
        new_tail = dfcaten(taildir, tailfile);
        new_relative_path = relative_path ? dfcaten("..", relative_path) : NULL;
        result = traverse_action(new_relative_path, src, dest, pkg, version, new_tail, opt, soft_delete);
        free(new_tail);
        free(new_relative_path);
        free(linked_path);
        return result;

      case DT_VOID:
      case DT_LINK_SAME_SAME:
      case DT_LINK_SAME_OTHER:
      case DT_ERROR:
      case DT_LINK_OTHER_DIR:
      case DT_LINK_OTHER_FILE:
      case DT_LINK_UNKNOWN:
      case DT_OTHER:
        if (!opt->quiet) {
          printf("!! WARNING : expected link not found at <%s>\n", full_dest_path);
        }
        free(linked_path);
        return 0;
        
    }
    break;

  case ST_OTHER:
    switch (dest_type) {
      case DT_LINK_EXACT:
        free(linked_path);
        if (unlink(full_dest_path) < 0) {
          printf("!! FAILED : unable to remove link at <%s>\n", full_dest_path);
          return 1;
        } else {
          if (!opt->quiet) printf("** SUCCESS : removed link at <%s>\n", full_dest_path);
          return 0;
        }

      case DT_VOID:
      case DT_LINK_SAME_SAME:
      case DT_LINK_SAME_OTHER:
      case DT_ERROR:
      case DT_LINK_OTHER_DIR:
      case DT_LINK_OTHER_FILE:
      case DT_LINK_UNKNOWN:
      case DT_DIRECTORY:
      case DT_OTHER:
        if (!opt->quiet) {
          printf("!! WARNING : expected link not found at <%s>\n", full_dest_path);
        }
        free(linked_path);
        return 0;
    }
    break;
  }
  assert(0);
  free(linked_path);
  return 1; /* shouldn't get here. */
}
/*}}}*/
/* {{{ static int traverse_action */
static int
traverse_action(const char *rel_path,
                const char *src,
                const char *dest,
                const char *pkg,
                const char *version,
                const char *tail,
                struct options *opt,
                action_fn fn)
{
  DIR *d;
  struct dirent *de;
  struct stat ssb;
  enum source_type src_type;
  enum dest_type dest_type;
  char *full_src, *full_dest;
  int errors = 0;

  full_src = caten(src, tail);
  full_dest = caten(dest, tail);

  d = opendir(full_src);
  if (d) {
    while ((de = readdir(d))) {
      char *full_src_path;
      char *full_dest_path;
      char *other_pkg, *other_version;
      
      if (!strcmp(de->d_name, ".")) continue;
      if (!strcmp(de->d_name, "..")) continue;
      if (check_ignore(tail, de->d_name)) continue;

      full_src_path = dfcaten(full_src, de->d_name);
      full_dest_path = dfcaten(full_dest, de->d_name);

      /* See what kind of a thing the installed entity is. */
      if (lstat(full_src_path, &ssb) < 0) {
        src_type = ST_ERROR;
        errors++;
      } else {
        src_type = (S_ISDIR(ssb.st_mode)) ? ST_DIR : ST_OTHER;
      }

      other_pkg = other_version = NULL;
      dest_type = find_dest_type(full_dest_path, full_src_path,
                                 rel_path,
                                 tail, de->d_name, pkg, version,
                                 &other_pkg, &other_version);

      errors |= (*fn)(src_type, dest_type,
                      rel_path,
                      full_src_path, full_dest_path,
                      src, dest, tail, de->d_name,
                      pkg, version,
                      other_pkg, other_version,
                      opt);

      if (other_pkg) free(other_pkg);
      if (other_version) free(other_version);

      free(full_src_path);
      free(full_dest_path);
    }
    closedir(d);
  } else {
    fprintf(stderr, "Could not open directory %s!\n", full_src);
    exit(1);
  }

  free(full_src);
  free(full_dest);
  return errors;
}
/*}}}*/
static char *make_rel(const char *src, const char *dest)/*{{{*/
{
  /* Return relative path to get from src to dest. Both are abs. */
  const char *p, *q;
  
  p = src, q = dest;
  while (*p && *q && (*p == *q)) p++, q++;
  if (!*p && !*q) {
    /* Paths are identical. */
    return new_string("");
  } else if (!*p) { /* src is shorter */
    return new_string(q+1);
  } else if (!*q) { /* dest is shorter */
    int slashes, i;
    int len;
    char *res;
    
    while (*--p != '/') ;
    slashes = 0;
    while (*p) {
      if (*p == '/') slashes++;
      p++;
    }
    
    /* And form result. */
    
    slashes--;
    len = 3*slashes - 1;
    res = new_array(char, len+1);
    for (i=0; i<slashes; i++) {
      if (i < (slashes-1)) {
        memcpy(res+(3*i), "../", 3);
      } else {
        strcpy(res+(3*i), "..");
      }
    }
    return res;

  } else {
    /* Backup to previous slash. */
    int slashes, i;
    int len;
    char *res;
    
    while (*--p != '/') ;
    while (*--q != '/') ;
    slashes = 0;
    while (*p) {
      if (*p == '/') slashes++;
      p++;
    }
    
    /* And form result. */
    q++; /* now points to char beyond slash */
    
    len = 3*slashes + strlen(q);
    res = new_array(char, len+1);
    for (i=0; i<slashes; i++) {
      memcpy(res+(3*i), "../", 3);
    }
    strcpy(res+(3*slashes), q);
    return res;
  }
}
/*}}}*/

/*{{{ record_install() */
static void record_install(const char *relative_path,
    const char *src_path,
    const char *dest_path,
    const char *pkg,
    const char *version)
{
  char *linkpath;
  char *record_dir;
  struct stat sb;
  int status;

  record_dir = dfcaten(dest_path, RECORD_DIR);
  status = stat(record_dir, &sb);
  if (status < 0) {
    if (mkdir(record_dir, 0755) < 0) {
      fprintf(stderr, "Cannot create %s.\nThe installed version of %s has not been recorded.\n", record_dir, pkg);
      exit(1);
    }
  }
  linkpath = dfcaten3(dest_path, RECORD_DIR, pkg);
  unlink(linkpath);

  if (symlink(relative_path ? relative_path : src_path, linkpath) < 0) {
    fprintf(stderr, "Cannot create %s.\nThe installed version of %s has not been recorded.\n", linkpath, pkg);
  }

  free(record_dir);
  free(linkpath);
  return;
}
/*}}}*/
/*{{{ remove_current_install() */
static void remove_current_install(const char *relative_path,
    const char *src_path, 
    const char *dest_path,
    const char *pkg,
    const char *version,
    const char *tail,
    struct options *opt)
{
  char *linkpath;
  char target[1024];
  int status;
  linkpath = dfcaten3(dest_path, RECORD_DIR, pkg);
  status = readlink(linkpath, target, sizeof(target));
  if (status < 0) {
    fprintf(stderr, "Failed to read target of <%s> : can't remove old version.\n", linkpath);
    goto get_out;
  }
  target[status] = 0; /* Null terminate */
  if (target[0] == '/') {
    /* path is absolute */
    traverse_action(NULL, target, dest_path, pkg, version, tail, opt, soft_delete);
  } else {
    /* path is relative */
    char *install_area;
    install_area = dfcaten(dest_path, target);
    traverse_action(target, install_area, dest_path, pkg, version, tail, opt, soft_delete);
    free(install_area);
  }


  unlink(linkpath);
get_out:
  free(linkpath);

}
/*}}}*/
static void usage(char *toolname)/*{{{*/
{
  fprintf(stderr,
    "spill version %s - segregated package install logical linker\n"
    "Copyright (C) Richard P. Curnow  2003, 2004\n"
    "\n"
    "spill comes with ABSOLUTELY NO WARRANTY.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions; see the GNU General Public License for details.\n", PROGRAM_VERSION);
  fprintf(stderr,
    "\n"
    "Printing this help\n"
    "Syntax : spill -h\n"
    "  -h,  --help             Show this help message then exit\n"
    "Showing just the version\n"
    "Syntax : spill -V\n"
    "  -V,  --version          Show the program version then exit\n"
    "\n"
    "Options for package install (default operation)\n"
    "Syntax : spill [-f] [-n] [-q] [-x]\n"
    "               [-l <file> | --conflict-list=<file>\n"
    "               <tool_install_path> [<link_install_path>] [<ignore_path>...]\n"
    "  -f,  --force            Attempt install even if expected subdirectories (bin,sbin,lib) are missing\n"
    "  -u,  --upgrade          Clean out links to older version of the same package before installing\n"
    "  -n,  --dry_run          Don't do install, just report potential link conflicts\n"
    "  -q,  --quiet            Be quiet when installing, only show errors\n"
    "  -x,  --expand           Expand any existing links to directories when needed\n"
    "  -o,  --override         Override any existing links that conflict with the new package\n"
    "  -l <conflict_file>\n"
    "  --conflict-list=<file>  Filename to which conflicting destination paths are written\n"
    "\n"
    "<tool_install_path>       Directory specified as --prefix when package was built\n"
    "                          (relative links are created if this is given as a relative path)\n"
    "<link_install_path>       Base directory where links are created (e.g. /usr) (default is \".\")\n"
    "<ignore_path>...          Space-separated list of relative paths not to be linked\n"
    "\n"
    "Options for package removal\n"
    "Syntax : spill -d [-q] <tool_install_path> [<link_install_path>]\n"
    "  -q,  --quiet            Be quiet, only show errors\n"
    "<tool_install_path> and <link_install_path> as above.\n"
    "\n"

    );
}
/*}}}*/
static void show_version(char *toolname)/*{{{*/
{
  fprintf(stderr, "spill version %s\n", PROGRAM_VERSION);
}
/*}}}*/
int main (int argc, char **argv)/*{{{*/
{

  char *src, *dest;
  int is_rel_src;
  char *clean_src, *clean_dest;
  int sane_src, sane_dest;
  char *pkg, *version;
  struct options opt;
  int bare_args;
  char *argv0 = argv[0];
  char *relative_path;
  char *conflict_list_path = NULL;
  int do_soft_delete;
  int do_upgrade;
  int hard_delete;
  char **next_argv;
  int next_argc;

#ifdef TEST_MAKE_REL
  printf("%s\n", make_rel("/x/y/zoo/foo", "/x/y/zaa/wib/ble"));
  printf("%s\n", make_rel("/x/y/zoo", "/x/y/zaa/wib/ble"));
  printf("%s\n", make_rel("/x/y", "/x/y/zaa/wib/ble"));
  printf("%s\n", make_rel("/x", "/x/y/zaa/wib/ble"));
  printf("%s\n", make_rel("/x/y/zaa/wib/ble", "/x/y/zoo/foo"));
  printf("%s\n", make_rel("/x/y/zaa/wib/ble", "/x/y/zoo"));
  printf("%s\n", make_rel("/x/y/zaa/wib/ble", "/x/y"));
  printf("%s\n", make_rel("/x/y/zaa/wib/ble", "/x"));
  printf("%s\n", make_rel("/x/y/zoo", "/x/y/zaa"));
  printf("%s\n", make_rel("/x/y/zaa", "/x/y/zaa"));
  exit(0);
#endif


  /* Parse arguments. */
  opt.quiet = 0; /* off by default */
  opt.dry_run = 0;
  opt.expand = 0;
  opt.force = 0;
  opt.override = 0;
  src = NULL; /* required. */
  dest = "."; /* pwd by default. */
  bare_args = 0;
  do_soft_delete = 0;
  do_upgrade = 0;
  hard_delete = 0;
  
  ++argv;
  --argc;
  while (argc > 0) {
    /* Handle flags' arguments the same way tar does */
    next_argv = argv + 1;
    next_argc = argc - 1;
    if (!strncmp(*argv, "--", 2)) {
      /* Long argument */
      if (!strcmp(*argv, "--quiet")) {
        opt.quiet = 1;
      } else if (!strcmp(*argv, "--dry_run")) {
        opt.dry_run = 1;
      } else if (!strcmp(*argv, "--expand")) {
        opt.expand = 1;
      } else if (!strcmp(*argv, "--help")) {
        usage(argv0);
        exit(0);
      } else if (!strcmp(*argv, "--version")) {
        show_version(argv0);
        exit(0);
      } else if (!strcmp(*argv, "--force")) {
        opt.force = 1;
      } else if (!strcmp(*argv, "--delete")) {
        do_soft_delete = 1;
      } else if (!strcmp(*argv, "--upgrade")) {
        do_upgrade = 1;
      } else if (!strcmp(*argv, "--override")) {
        opt.override = 1;
      } else if (!strncmp(*argv,"--conflict-list=", 16)) {
        conflict_list_path = new_string(*argv + 16);
      } else {
        fprintf(stderr, "Unrecognized option '%s'\n", *argv);
      }
    } else if ((*argv)[0] == '-') {
      char *p = (*argv) + 1;
      /* Allow more than 1 flag per option starting with '-' */
      while (*p) {
        switch (*p) {
          case 'q':
            opt.quiet = 1;
            break;
          case 'n':
            opt.dry_run = 1;
            break;
          case 'x':
            opt.expand = 1;
            break;
          case 'h':
            usage(argv0);
            exit(0);
          case 'V':
            show_version(argv0);
            exit(0);
          case 'f':
            opt.force = 1;
            break;
          case 'd':
            do_soft_delete = 1;
            break;
          case 'u':
            do_upgrade = 1;
            break;
          case 'o':
            opt.override = 1;
            break;
          case 'l':
            conflict_list_path = new_string(*next_argv);
            next_argv++;
            next_argc--;
            break;
          default:
            fprintf(stderr, "Unrecognized option '-%c'\n", *p);
        }
        p++;
      }
    } else {
      switch (bare_args) {
        case 0: src  = *argv; break;
        case 1: dest = *argv; break;
        default:
          add_ignore(*argv);
          break;
      }
      ++bare_args;
    }

    argc = next_argc;
    argv = next_argv;
  }

  if (!src || !dest) {
    fprintf(stderr, "Missing arguments : need at least <tool_install_path> and <link_install_path>\n");
    usage(argv0);
    exit(1);
  }
  
  /* normalise src. */
  is_rel_src = (src[0] == '/') ? 0 : 1;

  /* Clean up src and dest */
  clean_src = cleanup_dir(src);
  clean_dest = cleanup_dir(dest);

  /* See whether src and dest look reasonable. */
  if (!opt.force) {
    sane_src = check_sane("source", clean_src);
    sane_dest = check_sane("destination", clean_dest);

    if (!sane_src || !sane_dest) {
      exit(1);
    }
  }

  if (is_rel_src) {
    char *canon_src = normalise_dir(src);
    char *canon_dest = normalise_dir(dest);
    relative_path = make_rel(canon_dest, canon_src);
    free(canon_src);
    free(canon_dest);
  } else {
    relative_path = NULL;
  }

  /* Extract package and version for new package. */

  extract_package_details(clean_src, &pkg, &version);

  if (do_soft_delete) {
    /* Delete the links to the 'source' package from the destination tree,
       assuming the 'source' tree still exists intact.  */

    traverse_action(relative_path, clean_src, clean_dest, pkg, version, "", &opt, soft_delete);

  } else {
    /* Normal mode - package installation */
  
    if (!opt.quiet) fprintf(stderr, "Run pre-installing check for package <%s>, version <%s>\n", pkg, version);

    if (conflict_list_path) {
      conflict_file = fopen(conflict_list_path, "w");
      if (!conflict_file) {
        fprintf(stderr, "Could not open %s to write conflict list to\n", conflict_list_path);
      }
    }

    if (traverse_action(relative_path, clean_src, clean_dest, pkg, version, "", &opt, pre_install)) {
      fprintf(stderr, "\nPre-install check found problems, exiting\n\n");
      exit(1);
    }

    if (conflict_file) {
      fclose(conflict_file);
    }

    if (!opt.dry_run) {
      if (do_upgrade) {
        if (!opt.quiet) {
          fprintf(stderr, "\nPre-install checks OK, removing old version\n\n");
        }
        remove_current_install(NULL, clean_src, clean_dest, pkg, version, "", &opt);
      }
      if (!opt.quiet) fprintf(stderr, "\nPre-install checks OK, proceeding to install\n\n");
      if (traverse_action(relative_path, clean_src, clean_dest, pkg, version, "", &opt, do_install)) {
        fprintf(stderr, "\nProblems found whilst installing : package may only be part-installed\n\n");
        exit(1);
      }
      record_install(relative_path, clean_src, clean_dest, pkg, version);
    }
  }

  if (relative_path) free(relative_path);
  free(clean_src);
  free(clean_dest);
  free(pkg);
  free(version);

  return 0;

}
/*}}}*/
