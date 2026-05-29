/* bashhist.c -- bash interface to the GNU history library with config file support
 * Reads Redis settings from /etc/bash.cfg (YAML format)
 * Falls back to defaults if config doesn't exist
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

/* Configuration file path and defaults */
#ifndef CONFIG_FILE
#define CONFIG_FILE "/etc/bash.cfg"
#endif

#ifndef DEFAULT_REDIS_HOST
#define DEFAULT_REDIS_HOST "127.0.0.1"
#endif

#ifndef DEFAULT_REDIS_PORT
#define DEFAULT_REDIS_PORT 6379
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

/* Global Redis configuration - loaded from config file */
static char g_redis_hostname[MAXHOSTNAMELEN] = DEFAULT_REDIS_HOST;
static int g_redis_port = DEFAULT_REDIS_PORT;
static int g_config_loaded = 0;

/* Load YAML configuration file for Redis settings */
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
  
  char line[BASH_LINE_MAX];
  char *p;
  size_t len;
  
  while ((p = fgets (line, sizeof (line), cf)) != NULL)
    {
      /* Parse redis_port field from YAML */
      char *pline = strstr (line, "redis_port:");
      if (pline)
        {
          char *start = pline + 11; /* skip "redis_port:" */
          char *q = strchr (start, '"');
          /* Also check for : or newline after number */
          char *n = strchr (start, '\n');
          char *end = q ? q : n;
          
          if (end)
            {
              *end = '\0';
              char *val = start;
              while (*val && !isblank (*val)) val++;
              port = atoi (val);
            }
        }
      
      /* Parse hostname field from YAML - only if not set */
      if (g_redis_hostname[0] == 0)
        {
          p = strstr (line, "hostname:");
          if (p)
            {
              char *start = p + 9; /* skip "hostname:" */
              char *q = strchr (start, '"');
              
              if (q)
                {
                  *q = '\0';
                  strncpy (g_redis_hostname, start, MAXHOSTNAMELEN - 1);
                }
              else
                {
                  char *n = strchr (start, '\n');
                  if (n)
                    {
                      *n = '\0';
                      strncpy (g_redis_hostname, start, MAXHOSTNAMELEN - 1);
                    }
                }
            }
        }
    }
  
  fclose (cf);
  g_redis_port = port;
  g_config_loaded = 1;
}

static void