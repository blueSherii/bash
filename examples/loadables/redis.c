/* redis - send commands to a Redis server via hiredis */

/*
   Copyright (C) 2025 Free Software Foundation, Inc.

   This file is part of GNU Bash.
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

#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <hiredis/hiredis.h>

#include "loadables.h"

static redisContext *redis_ctx = NULL;
static char redis_saved_host[256] = "127.0.0.1";
static int redis_saved_port = 6379;

static void
redis_do_disconnect (void)
{
  if (redis_ctx)
    {
      redisFree (redis_ctx);
      redis_ctx = NULL;
    }
}

static int
redis_do_connect (const char *host, int port)
{
  char lhost[256];
  redisContext *c;

  strncpy (lhost, host, sizeof (lhost) - 1);
  lhost[sizeof (lhost) - 1] = '\0';

  c = redisConnect (lhost, port);
  if (c == NULL)
    {
      builtin_error ("redis: cannot allocate context");
      return -1;
    }
  if (c->err)
    {
      builtin_error ("redis: %s", c->errstr);
      redisFree (c);
      return -1;
    }

  redis_do_disconnect ();
  redis_ctx = c;
  strncpy (redis_saved_host, lhost, sizeof (redis_saved_host) - 1);
  redis_saved_host[sizeof (redis_saved_host) - 1] = '\0';
  redis_saved_port = port;
  return 0;
}

static void
print_reply (redisReply *reply)
{
  size_t i;

  if (!reply)
    return;

  switch (reply->type)
    {
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_STATUS:
      printf ("%s\n", reply->str);
      break;
    case REDIS_REPLY_INTEGER:
      printf ("%lld\n", reply->integer);
      break;
    case REDIS_REPLY_NIL:
      break;
    case REDIS_REPLY_ERROR:
      builtin_error ("redis: %s", reply->str);
      break;
    case REDIS_REPLY_ARRAY:
      for (i = 0; i < reply->elements; i++)
        print_reply (reply->element[i]);
      break;
#ifdef REDIS_REPLY_DOUBLE
    case REDIS_REPLY_DOUBLE:
      printf ("%g\n", reply->dval);
      break;
#endif
#ifdef REDIS_REPLY_BOOL
    case REDIS_REPLY_BOOL:
      printf ("%d\n", reply->integer ? 1 : 0);
      break;
#endif
#ifdef REDIS_REPLY_BIGNUM
    case REDIS_REPLY_BIGNUM:
      printf ("%s\n", reply->str);
      break;
#endif
#ifdef REDIS_REPLY_VERB
    case REDIS_REPLY_VERB:
      printf ("%s\n", reply->str);
      break;
#endif
#ifdef REDIS_REPLY_MAP
    case REDIS_REPLY_MAP:
      for (i = 0; i < reply->elements; i++)
        print_reply (reply->element[i]);
      break;
#endif
#ifdef REDIS_REPLY_SET
    case REDIS_REPLY_SET:
      for (i = 0; i < reply->elements; i++)
        print_reply (reply->element[i]);
      break;
#endif
    default:
      break;
    }
}

/* Returns a malloc'd string for scalar replies; NULL for arrays/nil/error. */
static char *
scalar_reply_string (redisReply *reply)
{
  char *buf;

  switch (reply->type)
    {
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_STATUS:
      return savestring (reply->str);
    case REDIS_REPLY_INTEGER:
      return itos ((intmax_t)reply->integer);
    case REDIS_REPLY_NIL:
      return savestring ("");
#ifdef REDIS_REPLY_DOUBLE
    case REDIS_REPLY_DOUBLE:
      buf = (char *)xmalloc (32);
      snprintf (buf, 32, "%g", reply->dval);
      return buf;
#endif
#ifdef REDIS_REPLY_BOOL
    case REDIS_REPLY_BOOL:
      return savestring (reply->integer ? "1" : "0");
#endif
#ifdef REDIS_REPLY_BIGNUM
    case REDIS_REPLY_BIGNUM:
      return savestring (reply->str);
#endif
#ifdef REDIS_REPLY_VERB
    case REDIS_REPLY_VERB:
      return savestring (reply->str);
#endif
    default:
      return (char *)NULL;
    }
}

int
redis_builtin (WORD_LIST *list)
{
  int opt, port, nargs, i, ret;
  intmax_t iport;
  char *host, *portstr, *passarg, *dbarg, *varname;
  const char *conn_host;
  int conn_port;
  const char **argv;
  WORD_LIST *l;
  redisReply *reply;
  SHELL_VAR *v;
  char *sval;

  host = portstr = passarg = dbarg = varname = NULL;

  reset_internal_getopt ();
  while ((opt = internal_getopt (list, "h:p:a:n:v:")) != -1)
    {
      switch (opt)
        {
        case 'h':
          host = list_optarg;
          break;
        case 'p':
          portstr = list_optarg;
          break;
        case 'a':
          passarg = list_optarg;
          break;
        case 'n':
          dbarg = list_optarg;
          break;
        case 'v':
          varname = list_optarg;
          break;
        CASE_HELPOPT;
        default:
          builtin_usage ();
          return (EX_USAGE);
        }
    }

  list = loptend;

  if (list == NULL)
    {
      builtin_usage ();
      return (EX_USAGE);
    }

  /* Special subcommand: close the current connection. */
  if (list->next == NULL && strcmp (list->word->word, "disconnect") == 0)
    {
      redis_do_disconnect ();
      return (EXECUTION_SUCCESS);
    }

  conn_host = host ? host : redis_saved_host;
  conn_port = redis_saved_port;
  if (portstr)
    {
      if (valid_number (portstr, &iport) == 0 || iport < 1 || iport > 65535)
        {
          builtin_error ("%s: invalid port number", portstr);
          return (EXECUTION_FAILURE);
        }
      conn_port = (int)iport;
    }

  /* Connect (or reconnect when host/port changes or context is broken). */
  if (redis_ctx == NULL || redis_ctx->err ||
      strcmp (conn_host, redis_saved_host) != 0 || conn_port != redis_saved_port)
    {
      if (redis_do_connect (conn_host, conn_port) < 0)
        return (EXECUTION_FAILURE);
    }

  if (passarg)
    {
      reply = redisCommand (redis_ctx, "AUTH %s", passarg);
      if (reply == NULL)
        {
          builtin_error ("redis: AUTH: %s", redis_ctx->errstr);
          redis_do_disconnect ();
          return (EXECUTION_FAILURE);
        }
      if (reply->type == REDIS_REPLY_ERROR)
        {
          builtin_error ("redis: AUTH: %s", reply->str);
          freeReplyObject (reply);
          return (EXECUTION_FAILURE);
        }
      freeReplyObject (reply);
    }

  if (dbarg)
    {
      reply = redisCommand (redis_ctx, "SELECT %s", dbarg);
      if (reply == NULL)
        {
          builtin_error ("redis: SELECT: %s", redis_ctx->errstr);
          redis_do_disconnect ();
          return (EXECUTION_FAILURE);
        }
      if (reply->type == REDIS_REPLY_ERROR)
        {
          builtin_error ("redis: SELECT: %s", reply->str);
          freeReplyObject (reply);
          return (EXECUTION_FAILURE);
        }
      freeReplyObject (reply);
    }

  nargs = 0;
  for (l = list; l; l = l->next)
    nargs++;

  argv = (const char **)xmalloc (nargs * sizeof (char *));
  for (i = 0, l = list; l; l = l->next, i++)
    argv[i] = l->word->word;

  reply = redisCommandArgv (redis_ctx, nargs, argv, NULL);
  free (argv);

  if (reply == NULL)
    {
      builtin_error ("redis: %s", redis_ctx->errstr);
      redis_do_disconnect ();
      return (EXECUTION_FAILURE);
    }

  ret = EXECUTION_SUCCESS;

  if (reply->type == REDIS_REPLY_ERROR)
    {
      builtin_error ("redis: %s", reply->str);
      ret = EXECUTION_FAILURE;
    }
  else if (varname)
    {
      sval = scalar_reply_string (reply);
      if (sval)
        {
          v = builtin_bind_variable (varname, sval, 0);
          if (v == 0 || readonly_p (v) || noassign_p (v))
            builtin_error ("%s: cannot set variable", varname);
          free (sval);
        }
      else
        print_reply (reply);
    }
  else
    print_reply (reply);

  freeReplyObject (reply);
  return ret;
}

int
redis_builtin_load (char *s)
{
  return 1;
}

void
redis_builtin_unload (char *s)
{
  redis_do_disconnect ();
}

char *redis_doc[] = {
  "Send a command to a Redis server.",
  "",
  "Connects to a Redis server and executes COMMAND with any additional",
  "ARGS. The connection is persistent across calls within the same shell",
  "session and is reused automatically unless -h or -p specifies a",
  "different target.",
  "",
  "Options:",
  "  -h HOST   server hostname or IP address (default: 127.0.0.1)",
  "  -p PORT   server port (default: 6379)",
  "  -a PASS   send AUTH PASS before executing COMMAND",
  "  -n DB     send SELECT DB before executing COMMAND",
  "  -v VAR    store a scalar reply in shell variable VAR instead of stdout",
  "",
  "The special command 'disconnect' closes the current connection.",
  "",
  "Reply output: strings and status replies are printed as-is; integers",
  "are printed in decimal; nil replies produce no output; array replies",
  "print one element per line. When -v is given, only scalar replies are",
  "stored; array replies are still sent to stdout.",
  "",
  "Exit status is 0 on success, non-zero on connection or Redis error.",
  (char *)NULL
};

struct builtin redis_struct = {
  "redis",
  redis_builtin,
  BUILTIN_ENABLED,
  redis_doc,
  "redis [-h host] [-p port] [-a pass] [-n db] [-v var] command [args ...]",
  0
};
