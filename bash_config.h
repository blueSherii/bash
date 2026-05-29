/* bashhist.c -- bash interface to the GNU history library with config file support.
 * 
 * Reads /etc/bash.cfg for Redis hostname and port settings
 * Falls back to defaults (localhost:6379) if config file doesn't exist
 * 
 * Config file format (YAML-like):
 * bash_history_redis:
 *   hostname: your-hostname
 *   port: 6379
 *   username: ""
 *   password: ""
 *   db: 0
 */

/* Global Redis configuration from config file */
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

/* Load configuration from YAML config file */
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
      /* Parse redis_port field */
      char *pline = strstr (line, "redis_port:");
      if (pline)
        {
          char *start = pline + 11; /* skip "redis_port:" */
          char *end = strchr (start, ':');
          char *q = strchr (start, '"');
          char *s = strchr (start, '\'');
          char *n = strchr (start, '\n');
          
          if (q)
            {
              *q = '\0';
              port = atoi (start);
            }
          else if (end)
            port = atoi (start);
        }
      
      /* Parse hostname field */
      if (g_redis_hostname[0] == 0)
        {
          p = strstr (line, "hostname:");
          if (p)
            {
              char *start = p + strlen ("hostname:");
              char *q = strchr (start, '"');
              if (q)
                {
                  *q = '\0';
                  strncpy (g_redis_hostname, start, 255);
                }
            }
        }
    }
  
  fclose (cf);
  g_redis_port = port;
  g_config_loaded = 1;
}