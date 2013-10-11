/*
 * main.c: P4 Deamon
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <p4d.h>

char* confDir = (char*)confDirDefault;

// defaults 

char dbHost[100+TB] = "localhost";
int  dbPort;
char dbName[100+TB] = "p4";
char dbUser[100+TB] = "p4";
char dbPass[100+TB] = "p4";

char ttyDevice[100+TB] = "/dev/ttyUSB0";
int  interval = 120;

int  mail = no;
char mailScript[200+TB] = "/usr/local/bin/p4d-mail.sh";
char stateMailTo[200+TB] = "";
char errorMailTo[200+TB] = "";

//***************************************************************************
// Configuration
//***************************************************************************

int atConfigItem(const char* Name, const char* Value)
{
   // Parse setup parameters and store values.
   
   if      (!strcasecmp(Name, "DbHost"))      sstrcpy(dbHost, Value, sizeof(dbHost));
   else if (!strcasecmp(Name, "DbPort"))      dbPort = atoi(Value);
   else if (!strcasecmp(Name, "DbName"))      sstrcpy(dbName, Value, sizeof(dbName));
   else if (!strcasecmp(Name, "DbUser"))      sstrcpy(dbUser, Value, sizeof(dbUser));
   else if (!strcasecmp(Name, "DbPass"))      sstrcpy(dbPass, Value, sizeof(dbPass));
   
   else if (!strcasecmp(Name, "LogLevel"))    loglevel = atoi(Value);
   else if (!strcasecmp(Name, "Interval"))    interval = atoi(Value);
   else if (!strcasecmp(Name, "ttyDevice"))   sstrcpy(ttyDevice, Value, sizeof(ttyDevice));

   else if (!strcasecmp(Name, "mail"))        mail = atoi(Value);
   else if (!strcasecmp(Name, "mailScript"))  sstrcpy(mailScript, Value, sizeof(mailScript));
   else if (!strcasecmp(Name, "stateMailTo")) sstrcpy(stateMailTo, Value, sizeof(stateMailTo));
   else if (!strcasecmp(Name, "errorMailTo")) sstrcpy(errorMailTo, Value, sizeof(errorMailTo));
   
   else
      return fail;
   
   return success;
}

//***************************************************************************
// Read Config
//***************************************************************************

int readConfig()
{
   int count = 0;
   FILE* f;
   char* line = 0;
   size_t size = 0;
   char* value;
   char* name;
   char* fileName;

   asprintf(&fileName, "%s/p4d.conf", confDir);

   if (!fileName || access(fileName, F_OK) != 0)
   {
      printf("Cannot access configuration file '%s'\n", fileName ? fileName : "<null>");
      free(fileName);
      return fail;
   }

   f = fopen(fileName, "r");

   while (getline(&line, &size, f) > 0)
   {
      char* p = strchr(line, '#');
      if (p) *p = 0;

      allTrim(line);

      if (isEmpty(line))
         continue;

      if (!(value = strchr(line, '=')))
         continue;
      
      *value = 0;
      value++;
      lTrim(value);
      name = line;
      allTrim(name);

      if (atConfigItem(name, value) != success)
      {
         printf("Found unexpected parameter '%s', aborting\n", name);
         free(fileName);
         return fail;
      }

      count++;
   }

   free(line);
   fclose(f);

   tell(0, "Read %d option from %s", count , fileName);

   free(fileName);

   return success;
}

void showUsage(const char* bin)
{
   printf("Usage: %s [-n][-c <config-dir>][-l <log-level>][-t]\n", bin);
   printf("    -n              don't daemonize\n");
   printf("    -t              log to stdout\n");
   printf("    -v              show version\n");
   printf("    -c <config-dir> use config in <config-dir>\n");
   printf("    -l <log-level>  set log level\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   Linpellet* job;
   int nofork = no;
   int pid;
   int _stdout = na;
   int _level = na;

   // Usage ..

   if (argc > 1 && (argv[1][0] == '?' || (strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
   {
      showUsage(argv[0]);
      return 0;
   }

   // Parse command line

   for (int i = 0; argv[i]; i++)
   {
      if (argv[i][0] != '-' || strlen(argv[i]) != 2)
         continue;

      switch (argv[i][1])
      {
         case 'l': if (argv[i+1]) _level = atoi(argv[i+1]); break;
         case 't': _stdout = yes;                           break;
         case 'n': nofork = yes;                            break;
         case 'c': if (argv[i+1]) confDir = argv[i+1];      break;
         case 'v': printf("Version %s\n", VERSION);  return 1;
      }
   }

   // read configuration ..

   if (readConfig() != success)
      return 1;

   if (_stdout != na) logstdout = _stdout;
   if (_level != na)  loglevel = _level;

   job = new Linpellet();

   // fork daemon

   if (!nofork)
   {
      if ((pid = fork()) < 0)
      {
         printf("Can't fork daemon, %s\n", strerror(errno));
         return 1;
      }
      
      if (pid != 0)
         return 0;
   }

   // register SIGINT

   ::signal(SIGINT, Linpellet::downF);
   ::signal(SIGTERM, Linpellet::downF);
   // ::signal(SIGHUP, Linpellet::triggerF);

   // do work ...

   job->loop();

   // shutdown

   tell(0, "shutdown");

   delete job;

   return 1;
}