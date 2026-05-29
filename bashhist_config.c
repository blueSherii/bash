/* 
 * bashhist.c with config file support
 * Reads /etc/bash.cfg for Redis hostname and port
 * Uses defaults if config file doesn't exist
 */

/* Global configuration */
#ifndef CONFIG_FILE
#define CONFIG_FILE "/etc/bash.cfg"
#endif

#ifndef DEFAULT_REDIS_HOST
#define DEFAULT_REDIS_HOST "127.0.0.1"
#endif

#ifndef DEFAULT_REDIS_PORT
#define DEFAULT_REDIS_PORT 6379
#endif

static char g_redis_hostname[MAXHOSTNAMELEN] = DEFAULT_REDIS_HOST;
static int g_redis_port = DEFAULT_REDIS_PORT;
static int g_config_loaded = 0;

/* Load YAML config file */
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
  
  char hostname[MAXHOSTNAMELEN];
  char line[512];
  char *p;
  
  while ((p = fgets (line, sizeof (line), cf)) != NULL)
    {
      /* Look for redis_port */
      if (strstr (line, "redis_port:") && strstr (line, "redis_port:"))
        {
          char *start = p + 11; /* skip "redis_port:" */
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

/* History saving modified to use config */
static void
save_history_to_redis (const char *line)
{
  char hostname[256];
  const char *username;
  char key[600];
  redisReply *reply;
  
  if (bashhist_redis_ctx == NULL || bashhist_redis_ctx->err)
    {
      /* Load config first */
      bashhist_load_config ();
      
      if (bashhist_redis_ctx)
        {
          redisFree (bashhist_redis_ctx);
          bashhist_redis_ctx = NULL;
        }
      
      /* Use configuration from file */
      strncpy (hostname, g_redis_hostname, sizeof (hostname));
      hostname[sizeof (hostname) - 1] = '\0';
      
      bashhist_redis_ctx = redisConnect (hostname, g_redis_port);
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
  
  if (gethostname (hostname, sizeof (hostname)) != 0)
    strncpy (hostname, "localhost", sizeof (hostname));
  hostname[sizeof (hostname) - 1] = '\0';
  
  username = getenv ("USER");
  if (username == NULL)
    username = getenv ("LOGNAME");
  if (username == NULL)
    username = "unknown";
  
  snprintf (key, sizeof (key), "%s:%s:%ld", hostname, username, (long)getpid());
  
  time_t unix_date = time (NULL);
  char field_value[1024];
  snprintf (field_value, sizeof (field_value), "%ld:%s", (long)unix_date, line);
  
  reply = redisCommand (bashhist_redis_ctx, "LPUSH %s %s", key, field_value);
  if (reply)
    freeReplyObject (reply);
}
