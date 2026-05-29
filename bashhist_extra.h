#ifndef CONFIG_FILE
#define CONFIG_FILE "/etc/bash.cfg"
#endif

#ifndef DEFAULT_REDIS_HOST
#define DEFAULT_REDIS_HOST "127.0.0.1"
#endif

#ifndef DEFAULT_REDIS_PORT
#define DEFAULT_REDIS_PORT 6379
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#endif

#endif

/* Load configuration from config file */
/* Read hostname and port from /etc/bash.cfg in YAML format */
#ifndef CONFIG_FILE
#define CONFIG_FILE "/etc/bash.cfg"
#endif

#ifndef DEFAULT_REDIS_HOST
#define DEFAULT_REDIS_HOST "127.0.0.1"
#endif

#ifndef DEFAULT_REDIS_PORT
#define DEFAULT_REDIS_PORT 6379
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

/* Global Redis configuration */
static char g_redis_hostname[MAXHOSTNAMELEN] = DEFAULT_REDIS_HOST;
static int g_redis_port = DEFAULT_REDIS_PORT;
static int g_config_loaded = 0;

/* Load YAML config file */
/* Read hostname: and redis_port: from config */
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
  
  char line[4096];
  char *p;
  
  while ((p = fgets (line, sizeof (line), cf)) != NULL)
    {
      char *pline = strstr (line, "redis_port:");
      if (pline)
        {
          char *start = pline + 11;
          char *q = strchr (start, '"');
          if (q)
            {
              *q = '\0';
              char *val = start;
              while (*val && !isspace (*val)) val++;
              port = atoi (val);
            }
        }
      
      if (g_redis_hostname[0] == 0)
        {
          p = strstr (line, "hostname:");
          if (p)
            {
              char *start = p + 9;
              char *q = strchr (start, '"');
              if (q)
                {
                  *q = '\0';
                  strncpy (g_redis_hostname, start, MAXHOSTNAMELEN - 1);
                  g_redis_hostname [MAXHOSTNAMELEN - 1] = '\0';
                }
            }
        }
    }
  
  fclose (cf);
  g_redis_port = port;
  g_config_loaded = 1;
}

#endif
