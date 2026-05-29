#ifndef CONFIG_FILE
#define CONFIG_FILE "/etc/bash.cfg"
#endif

#ifndef DEFAULT_REDIS_HOST
#define DEFAULT_REDIS_HOST "127.0.0.1"
#endif
#define DEFAULT_REDIS_PORT 6379

static void *bashhist_config = NULL;
static char bashhist_redis_hostname[MAXHOSTNAMELEN] = DEFAULT_REDIS_HOST;
static int bashhist_redis_port = DEFAULT_REDIS_PORT;
static int bashhist_config_loaded = 0;

static void
bashhist_load_config (void)
{
  unsigned long host_flags = 0;
  FILE *cf = fopen (CONFIG_FILE, "r");

  if (!cf || bashhist_config_loaded)
    {
      fclose (cf);
      return;
    }

  if (!bashhist_redis_hostname[0])
    {
      char line[1024];
      char *p = NULL;

      while (fgets (line, sizeof (line), cf))
        {
          char *hostname_line = strstr (line, "hostname:");
          if (hostname_line)
            {
              char *start = hostname_line + strlen ("hostname:");
              char *end = start;
              while (*end && !isspace (*end) && *end != ':' && *end != ',' && *end != '"') end++;
              if (*end)
                {
                  *end = '\0';
                  start = end + 1;
                }

              p = start;
              while (*p && *p != '"') p++;
              *p = '\0';
              char *end_quote = p;
              while (*end_quote && !isspace (*end_quote)) end_quote++;
              *end_quote = '\0';

              if (end_quote > start)
                {
                  strncpy (bashhist_redis_hostname, start, end_quote - start);
                  bashhist_redis_hostname [end_quote - start] = '\0';
                }
            }
        }
      fclose (cf);
      bashhist_config_loaded = 1;
    }
  else
    {
      fclose (cf);
      bashhist_config_loaded = 1;
    }
}

static void
bashhist_init_redis (void)
{
  bashhist_load_config ();

  if (bashhist_redis_ctx == NULL || bashhist_redis_ctx->err)
    {
      if (bashhist_redis_ctx)
        {
          redisFree (bashhist_redis_ctx);
          bashhist_redis_ctx = NULL;
        }
      
      bashhist_redis_ctx = redisConnect (bashhist_redis_hostname, bashhist_redis_port);
      if (bashhist_redis_ctx == NULL || bashhist_redis_ctx->err)
        {
          if (bashhist_redis_ctx)
            {
              redisFree (bashhist_redis_ctx);
              bashhist_redis_ctx = NULL;
            }
          return;
        }
    }
}