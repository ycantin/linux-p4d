
#include <unistd.h>
#include <p4io.h>
#include <lib/common.h>

enum UserCommand
{
   ucUnknown = na,
   ucGetValue,
   ucGetParameter,
   ucSetParameter,
   ucParameterList,
   ucValueList,
   ucErrorList,
   ucMenuList
};

void showUsage(const char* bin)
{
   printf("Usage: %s <command> [-a <address> [-v <value>]] [-l <log-level>]\n", bin);
   printf("     -a <address>    address of parameter or value\n");
   printf("     -v <value>      new value\n");
   printf("     -l <log-level>  set log level\n");
   printf("  commands:\n");
   printf("     errors   show error buffer\n");
   printf("     values   list value addrsses\n");
   printf("     menu     list menu items (available parameters)\n");
   printf("     getv     show value at <addr>\n");
   printf("     getp     show parameter at <addr>\n");
   printf("     setp     set parameter at <addr> to <value>\n");
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char** argv)
{
   Serial serial;
   int status;
   byte b;
   word addr = Fs::addrUnknown;
   word value = Fs::addrUnknown;
   UserCommand cmd = ucUnknown;

   loglevel = 0;
   logstdout = yes;

   // usage ..

   if (argc <= 1 || (argv[1][0] == '?' || (strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
   {
      showUsage(argv[0]);
      return 0;
   }
 
   // get command
   
   if (strcasecmp(argv[1], "getv") == 0)
      cmd = ucGetValue; 
   else if (strcasecmp(argv[1], "getp") == 0)
      cmd = ucGetParameter; 
   else if (strcasecmp(argv[1], "setp") == 0)
      cmd = ucSetParameter; 
   else if (strcasecmp(argv[1], "values") == 0)
      cmd = ucValueList;
   else if (strcasecmp(argv[1], "errors") == 0)
      cmd = ucErrorList;
   else if (strcasecmp(argv[1], "menu") == 0)
      cmd = ucMenuList;
   else
   {
      showUsage(argv[0]);
      return 0;
   }

   // parse options

   for (int i = 1; argv[i]; i++)
   {
      if (argv[i][0] != '-' || strlen(argv[i]) != 2)
         continue;

      switch (argv[i][1])
      {
         case 'a': if (argv[i+1]) addr = strtol(argv[++i], 0, 0);  break;
         case 'v': if (argv[i+1]) value = strtol(argv[++i], 0, 0); break;
         case 'l': if (argv[i+1]) loglevel = atoi(argv[++i]);      break;
      }
   }

   if (cmd == ucUnknown)
      return 1;

   P4Request request(&serial);

   if (serial.open("/dev/ttyUSB0") != success)
      return 1;

   while (serial.look(b, 100) == success)
      tell(0, "-> 0x%2.2x", b);

   // connection check

   if (request.check() != success)
   {
      serial.close();
      return 1;
   }

   switch (cmd)
   {
      case ucGetParameter:
      {
         Fs::ConfigParameter p(addr);
         
         if (request.getParameter(&p) == success)
         {
            tell(eloAlways, "Address: 0x%4.4x; Unit: %s; Digits: %d; Factor: %d; "
                 "Current: %d; Min: %d; Max: %d; Default: %d", 
                 p.address, p.unit, p.digits, p.factor, p.value, p.min, p.max, p.def);
         }

         break;
      }
      case ucSetParameter:
      {
         Fs::ConfigParameter p(addr);
         
         if (value != Fs::addrUnknown)
         {
            p.value = value;
            
            if ((status = request.setParameter(&p)) == success)
               tell(eloAlways, "Parameter 0x%4.4X changed successfully to %d%s", 
                    p.address, p.value, p.unit);
            else
               tell(eloAlways, "Set of parameter failed, error %d", status);
         }

         break;
      }
      case ucGetValue:
      {
         Fs::Value v(addr);
         
         if ((status = request.getValue(&v)) == success)
            tell(eloAlways, "value 0x%4.4X is %d", v.address, v.value);
         else
            tell(eloAlways, "Getting value failed, error %d", status);

         break;
      }
      case ucValueList:
      {
         int status;
         Fs::ValueSpec v;
         int n = 0;

         for (status = request.getFirstValueSpec(&v); status != Fs::wrnLast; status = request.getNextValueSpec(&v))
         {
            if (status == success)
               tell(eloAlways, "%3d) 0x%04x %4d '%s' (%04d) '%s'",
                    n, v.address, v.factor, v.unit, v.unknown, v.description);
            else
               tell(eloAlways, "DATA: %3d) <empty>", n);

            n++;
         }

         break;
      }
      case ucErrorList:
      {
         int status;
         Fs::ErrorInfo e;
         char* ct = 0;

         for (status = request.getFirstError(&e); status == success; status = request.getNextError(&e))
         {
            ct = strdup(ctime(&e.time));
            ct[strlen(ct)-1] = 0;   // remove linefeed of ctime()
            
            tell(eloAlways, "%s:  %03d/%03d  '%s' - %s", ct, e.number, e.info, e.text, Fs::errState2Text(e.state));
            
            free(ct);
         }

         break;
      }
      case ucMenuList:
      {
         int status;
         Fs::MenuItem m;
         int n = 0;

         for (status = request.getFirstMenuItem(&m); status == success; status = request.getNextMenuItem(&m))
         {
            tell(eloAlways, "%3d) 0x%04x (0x%04x) '%s'", n, m.address, m.unknown, m.description);
         }

         break;
      }
   }
   
   serial.close();
   
   return 0;
}