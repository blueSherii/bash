/* bashhist.c -- bash interface to the GNU history library. */

/* Copyright (C) 1993-2024 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Bash is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#if defined (HISTORY)

#if defined (HAVE_UNISTD_H)
#  ifdef _MINIX
 #    include <sys/types.h>
#  endif
#  include <unistd.h>
#endif

#include "bashtypes.h"
#include <stdio.h>
#include <errno.h>
#include "bashansi.h"
#include "posixstat.h"
#include "filecntl.h"

#include "bashintl.h"

#if defined (SYSLOG_HISTORY)
#  include <syslog.h>
#endif

#include <time.h>
#include <hiredis/hiredis.h>

#include "shell.h"
#include "flags.h"
#include "parser.h"
#include "input.h"
#include "parser.h"	/* for the struct dstack stuff. */
#include "pathexp.h"	/* for the struct ignorevar stuff */
#include "bashhist.h"	/* matching prototypes and declarations */
#include "builtins/common.h"

#include <readline/history.h>
#include <glob/glob.h>
#include <glob/strmatch.h>

#if defined (READLINE)
#  include "bashline.h"
extern int rl_done, rl_dispatching;	/* should really include readline.h */
#endif

#ifndef HISTSIZE_DEFAULT
#  define HISTSIZE_DEFAULT "500"
#endif

/* Configuration file and defaults */
#ifndef CONFIG_FILE
#  define CONFIG_FILE "/etc/bash.cfg"
#endif

#ifndef DEFAULT_REDIS_HOST
#  define DEFAULT_REDIS_HOST "127.0.0.1"
#endif

#ifndef DEFAULT_REDIS_PORT
#  define DEFAULT_REDIS_PORT 6379
#endif

#if !defined (errno)
extern int errno;
#endif

static int histignore_item_func (struct ign *);
static int check_history_control (char *);
static void hc_erasedups (char *);
static void really_add_history (char *);

static struct ignorevar histignore =
{
  "HISTIGNORE",
  (struct ign *)0,
  0,
  (char *)0,
  (sh_iv_item_func_t *)histignore_item_func,
};

#define HIGN_EXPAND 0x01

/* Global Redis configuration from config file */
static char g_redis_hostname[MAXHOSTNAMELEN] = DEFAULT_REDIS_HOST;
static int g_redis_port = DEFAULT_REDIS_PORT;
static int g_config_loaded = 0;

/* Load configuration from YAML file */
static void
bashhist_load_config (void)
{
  FILE *cf = fopen (CONFIG_FILE, "r");
  int port = g_redis_port;
  
  if (!cf || g_config_loaded)
    {
      if (cf) fclose (cf);
      return;
    }
  
  char line[512];
  char *p;
  
  while ((p = fgets (line, sizeof (line), cf)) != NULL)
    {
      /* Look for redis_port */
      if (strstr (line, "redis_port:") && strstr (line, "redis_port:"))
        {
          char *start = line + 11; /* skip "redis_port:" */
          char *end = strchr (start, ':');
          char *q = strchr (start, '"');
          char *s = strchr (start, '\'');
          char *n = strchr (start, '\n');
          
          char *q_ptr = q ? q : (s ? s : (n ? n : end));
          if (q_ptr)
            {
              *q_ptr = '\0';
              port = atoi (start);
            }
          else if (end)
            port = atoi (start);
        }
      
      /* Look for hostname */
      if (g_redis_hostname[0] == 0)
        {
          p = strstr (line, "hostname:");
          if (p)
            {
              char *start = p + strlen ("hostname:");
              char *end = strchr (start, ':');
              char *q = strchr (start, '"');
              
              if (q)
                {
                  *q = '\0';
                  strncpy (g_redis_hostname, start, 255);
                }
              else if (end)
                strncpy (g_redis_hostname, start, 255);
            }
        }
    }
  
  fclose (cf);
  g_redis_port = port;
  g_config_loaded = 1;
}

#define HIGN_EXPAND 0x01