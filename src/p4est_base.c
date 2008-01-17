/*
  This file is part of p4est.
  p4est is a C library to manage a parallel collection of quadtrees and/or
  octrees.

  Copyright (C) 2007,2008 Carsten Burstedde, Lucas Wilcox.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <p4est_base.h>

#ifdef P4EST_BACKTRACE
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

/* *INDENT-OFF* */

const int p4est_log_lookup_table[256] =
{ -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

/* *INDENT-ON* */

static int          malloc_count = 0;
static int          free_count = 0;

static int          p4est_base_identifier = -1;
static p4est_handler_t p4est_abort_handler = NULL;
static void        *p4est_abort_data = NULL;

static int          signals_caught = 0;
static sig_t        system_int_handler = NULL;
static sig_t        system_segv_handler = NULL;
static sig_t        system_usr2_handler = NULL;

static void
p4est_signal_handler (int sig)
{
  char                prefix[BUFSIZ];
  char               *sigstr;

  if (p4est_base_identifier >= 0) {
    snprintf (prefix, BUFSIZ, "[%d] ", p4est_base_identifier);
  }
  else {
    prefix[0] = '\0';
  }

  switch (sig) {
  case SIGINT:
    sigstr = "INT";
    break;
  case SIGSEGV:
    sigstr = "SEGV";
    break;
  case SIGUSR2:
    sigstr = "USR2";
    break;
  default:
    sigstr = "<unknown>";
    break;
  }
  fprintf (stderr, "%sAbort: Signal %s\n", prefix, sigstr);

  p4est_abort ();
}

int
p4est_int32_compare (const void *v1, const void *v2)
{
  return (int32_t) v1 - (int32_t) v2;
}

void               *
p4est_malloc (size_t size)
{
  void               *ret;

  ret = malloc (size);

  if (size > 0) {
    ++malloc_count;
  }
  else {
    malloc_count += ((ret == NULL) ? 0 : 1);
  }

  return ret;
}

void               *
p4est_calloc (size_t nmemb, size_t size)
{
  void               *ret;

  ret = calloc (nmemb, size);

  if (nmemb * size > 0) {
    ++malloc_count;
  }
  else {
    malloc_count += ((ret == NULL) ? 0 : 1);
  }

  return ret;
}

void               *
p4est_realloc (void *ptr, size_t size)
{
  void               *ret;

  ret = realloc (ptr, size);

  if (ptr == NULL) {
    if (size > 0) {
      ++malloc_count;
    }
    else {
      malloc_count += ((ret == NULL) ? 0 : 1);
    }
  }
  else if (size == 0) {
    free_count += ((ret == NULL) ? 1 : 0);
  }

  return ret;
}

void
p4est_free (void *ptr)
{
  if (ptr != NULL) {
    ++free_count;
    free (ptr);
  }
}

void
p4est_memory_check (void)
{
  P4EST_CHECK_ABORT (malloc_count == free_count, "Memory balance");
}

void
p4est_set_linebuffered (FILE * stream)
{
  setvbuf (stream, NULL, _IOLBF, 0);
}

void
p4est_set_abort_handler (int identifier, p4est_handler_t handler, void *data)
{
  p4est_base_identifier = identifier;
  p4est_abort_handler = handler;
  p4est_abort_data = data;

  if (handler != NULL && !signals_caught) {
    system_int_handler = signal (SIGINT, p4est_signal_handler);
    P4EST_CHECK_ABORT (system_int_handler != SIG_ERR, "catching INT");
    system_segv_handler = signal (SIGSEGV, p4est_signal_handler);
    P4EST_CHECK_ABORT (system_segv_handler != SIG_ERR, "catching SEGV");
    system_usr2_handler = signal (SIGUSR2, p4est_signal_handler);
    P4EST_CHECK_ABORT (system_usr2_handler != SIG_ERR, "catching USR2");
    signals_caught = 1;
  }
  else if (handler == NULL && signals_caught) {
    signal (SIGINT, system_int_handler);
    system_int_handler = NULL;
    signal (SIGSEGV, system_segv_handler);
    system_segv_handler = NULL;
    signal (SIGUSR2, system_usr2_handler);
    system_usr2_handler = NULL;
    signals_caught = 0;
  }
}

void
p4est_abort (void)
{
  char                prefix[BUFSIZ];
#ifdef P4EST_BACKTRACE
  int                 i;
  size_t              bt_size;
  void               *bt_buffer[64];
  char              **bt_strings;
  const char         *str;
#endif

  if (p4est_base_identifier >= 0) {
    snprintf (prefix, BUFSIZ, "[%d] ", p4est_base_identifier);
  }
  else {
    prefix[0] = '\0';
  }

#ifdef P4EST_BACKTRACE
  bt_size = backtrace (bt_buffer, 64);
  bt_strings = backtrace_symbols (bt_buffer, bt_size);

  fprintf (stderr, "%sAbort: Obtained %ld stack frames\n",
           prefix, (long int) bt_size);

#ifdef P4EST_ADDRTOLINE
  /* implement pipe connection to addr2line */
#endif

  for (i = 0; i < bt_size; i++) {
    str = strrchr (bt_strings[i], '/');
    if (str != NULL) {
      ++str;
    }
    else {
      str = bt_strings[i];
    }
    /* fprintf (stderr, "   %p %s\n", bt_buffer[i], str); */
    fprintf (stderr, "%s   %s\n", prefix, str);
  }
  free (bt_strings);
#endif /* P4EST_BACKTRACE */

  fflush (stdout);
  fflush (stderr);

  if (p4est_abort_handler != NULL) {
    p4est_abort_handler (p4est_abort_data);
  }

  abort ();
}

/* EOF p4est_base.c */
