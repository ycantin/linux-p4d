//***************************************************************************
// p4d / Linux - Heizungs Manager
// File p4d.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 04.11.2010 - 25.04.2020  Jörg Wendel
//***************************************************************************

//***************************************************************************
// Include
//***************************************************************************

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>
#include <inttypes.h>
#include <libxml/parser.h>
#include <algorithm>

#include "lib/json.h"
#include "p4d.h"

int P4d::shutdown = no;

//***************************************************************************
// Configuration Items
//***************************************************************************

std::list<P4d::ConfigItemDef> P4d::configuration
{
   // web

   { "addrsDashboard",            ctMultiSelect, false, "2 WEB Interface", "Sensoren 'Dashboard'", "Komma getrennte Liste aus Typ:ID siehe 'Aufzeichnung'" },
   { "addrsList",                 ctMultiSelect, false, "2 WEB Interface", "Sensoren 'Liste'", "Komma getrennte Liste aus Typ:ID siehe 'Aufzeichnung'" },
   // { "addrsMainMobile",        ctMultiSelect, false, "2 WEB Interface", "Sensoren Mobile Device", "Komma getrennte Liste aus Typ:ID siehe 'Aufzeichnung'" },

   { "webUrl",                    ctString,  false, "2 WEB Interface", "URL der Visualisierung", "kann mit %weburl% in die Mails eingefügt werden" },
   { "webSSL",                    ctBool,    false, "2 WEB Interface", "Use SSL for WebInterface", "" },
   { "haUrl",                     ctString,  false, "2 WEB Interface", "URL der Hausautomatisierung", "Zur Anzeige des Menüs als Link" },

   { "heatingType",               ctChoice,  false, "2 WEB Interface", "Typ der Heizung", "" },
   { "style",                     ctChoice,  false, "2 WEB Interface", "Farbschema", "" },
   { "iconSet",                   ctChoice,  false, "2 WEB Interface", "Status Icon Set", "" },
   { "schema",                    ctChoice,  false, "2 WEB Interface", "Schematische Darstellung", "" },

   // p4d

   { "interval",                  ctInteger, false, "1 P4 Daemon", "Intervall der Aufzeichung", "Datenbank Aufzeichung [s]" },
   { "webPort",                   ctInteger, false, "1 P4 Daemon", "Port des Web Interfaces", "" },
   { "stateCheckInterval",        ctInteger, false, "1 P4 Daemon", "Intervall der Status Prüfung", "Intervall der Status Prüfung [s]" },
   { "ttyDevice",                 ctString,  false, "1 P4 Daemon", "TTY Device", "Beispiel: '/dev/ttyUsb0'" },
   { "loglevel",                  ctInteger, false, "1 P4 Daemon", "Log level", "" },

   { "tsync",                     ctBool,    false, "1 P4 Daemon", "Zeitsynchronisation", "täglich 3:00" },
   { "maxTimeLeak",               ctInteger, false, "1 P4 Daemon", " bei Abweichung über [s]", "Mindestabweichung für Synchronisation in Sekunden" },

   { "aggregateHistory",          ctInteger, false, "1 P4 Daemon", "Historie [Tage]", "history for aggregation in days (default 0 days -&gt; aggegation turned OFF)" },
   { "aggregateInterval",         ctInteger, false, "1 P4 Daemon", " danach aggregieren über", "aggregation interval in minutes - 'one sample per interval will be build'" },
   { "peakResetAt",               ctString,  true,  "1 P4 Daemon", "", "" },

   { "consumptionPerHour",        ctNum,     false, "1 P4 Daemon", "Pellet Verbrauch / Stoker Stunde", "" },

   // MQTT interface

   { "mqttUrl",                   ctString,  false, "4 MQTT Interface", "MQTT Broker Url", "Optional. Beispiel: 'tcp://127.0.0.1:1883'" },
   { "mqttUser",                  ctString,  false, "4 MQTT Interface", "User", "" },
   { "mqttPassword",              ctString,  false, "4 MQTT Interface", "Password", "" },
   { "mqttDataTopic",             ctString,  false, "4 MQTT Interface", "MQTT Data Topic Name", "&lt;NAME&gt; wird gegen den Messwertnamen und &lt;GROUP&gt; gegen den Namen der Gruppe ersetzt. Beispiel: p4d2mqtt/sensor/&lt;NAME&gt;/state" },
   { "mqttHaveConfigTopic",       ctBool,    false, "4 MQTT Interface", "Config Topic", "Speziell für HomeAssistant" },

   // mail

   { "mail",                      ctBool,    false, "3 Mail", "Mail Benachrichtigung", "Mail Benachrichtigungen aktivieren/deaktivieren" },
   { "mailScript",                ctString,  false, "3 Mail", "p4d sendet Mails über das Skript", "" },
   { "stateMailTo",               ctString,  false, "3 Mail", "Status Mail Empfänger", "Komma getrennte Empfängerliste" },
   { "stateMailStates",           ctMultiSelect, false, "3 Mail", "  für folgende Status", "" },

   { "errorMailTo",               ctString,  false, "3 Mail", "Fehler Mail Empfänger", "Komma getrennte Empfängerliste" },
};

//***************************************************************************
// Web Service
//***************************************************************************

const char* cWebService::events[] =
{
   "unknown",
   "login",
   "logout",
   "toggleio",
   "toggleionext",
   "togglemode",
   "storeconfig",
   "gettoken",
   "iosetup",
   "storeiosetup",
   "chartdata",
   "logmessage",
   "userconfig",
   "changepasswd",
   "resetpeaks",
   "groupconfig",
   "errors",
   "menu",
   "pareditrequest",
   "parstore",
   "alerts",
   "storealerts",
   "sendmail",
   "chartbookmarks",
   "storechartbookmarks",
   "inittables",
   "storeschema",
   "updatetimeranges",
   "pellets",
   "pelletsadd",
   0
};

const char* cWebService::toName(Event event)
{
   if (event >= evUnknown && event < evCount)
      return events[event];

   return events[evUnknown];
}

cWebService::Event cWebService::toEvent(const char* name)
{
   for (int e = evUnknown; e < evCount; e++)
      if (strcasecmp(name, events[e]) == 0)
         return (Event)e;

   return evUnknown;
}

//***************************************************************************
// Object
//***************************************************************************

P4d::P4d()
{
   nextAt = time(0);
   startedAt = time(0);

   // force german locale at least for 'strftime'

   setlocale(LC_ALL, "de_DE.UTF-8");

   cDbConnection::init();
   cDbConnection::setEncoding("utf8");
   cDbConnection::setHost(dbHost);
   cDbConnection::setPort(dbPort);
   cDbConnection::setName(dbName);
   cDbConnection::setUser(dbUser);
   cDbConnection::setPass(dbPass);

   sem = new Sem(0x3da00001);
   serial = new Serial;
   request = new P4Request(serial);
   curl = new cCurl();
   webSock = new cWebSock(this, httpPath);
}

P4d::~P4d()
{
   exit();

   delete mqttWriter;
   delete mqttReader;

   free(mailScript);
   free(stateMailAtStates);
   free(stateMailTo);
   free(errorMailTo);
   free(sensorScript);

   delete serial;
   delete request;
   delete sem;
   delete curl;
   delete webSock;

   cDbConnection::exit();
}

//***************************************************************************
// Push In Message (from WS to p4d)
//***************************************************************************

int P4d::pushInMessage(const char* data)
{
   cMyMutexLock lock(&messagesInMutex);

   messagesIn.push(data);
   loopCondition.Broadcast();

   return success;
}

//***************************************************************************
// Push Out Message (from p4d to WS)
//***************************************************************************

int P4d::pushOutMessage(json_t* oContents, const char* title, long client)
{
   json_t* obj = json_object();

   addToJson(obj, "event", title);
   json_object_set_new(obj, "object", oContents);

   char* p = json_dumps(obj, JSON_REAL_PRECISION(4));
   json_decref(obj);

   if (!p)
   {
      tell(0, "Error: Dumping json message failed");
      return fail;
   }

   webSock->pushOutMessage(p, (lws*)client);
   tell(2, "-> event '%s' (0x%lx) [%.150s..]", title, client, p);
   tell(3, "DEBUG: PushMessage [%s]", p);
   free(p);

   webSock->performData(cWebSock::mtData);

   return done;
}

int P4d::pushDataUpdate(const char* title, long client)
{
   // push all in the jsonSensorList to the 'interested' clients

   if (client)
   {
      auto cl = wsClients[(void*)client];
      json_t* oWsJson = json_array();

      json_t* oJson = json_object();
      daemonState2Json(oJson);
      pushOutMessage(oJson, "daemonstate", client);

      oJson = json_object();
      s3200State2Json(oJson);
      pushOutMessage(oJson, "s3200-state", client);

      if (cl.page == "index")
      {
         if (addrsDashboard.size())
            for (const auto sensor : addrsDashboard)
               json_array_append(oWsJson, jsonSensorList[sensor]);
         else
            for (auto sj : jsonSensorList)
               json_array_append(oWsJson, sj.second);
      }
      else if (cl.page == "list")
      {
         if (addrsList.size())
            for (const auto sensor : addrsList)
               json_array_append(oWsJson, jsonSensorList[sensor]);
         else
            for (auto sj : jsonSensorList)
               json_array_append(oWsJson, sj.second);
      }
      else if (cl.page == "schema")
      {
         // #TODO - send visible instead of all??

         for (auto sj : jsonSensorList)
            json_array_append(oWsJson, sj.second);
      }

      pushOutMessage(oWsJson, title, client);
   }
   else
   {
      for (const auto cl : wsClients)
      {
         json_t* oWsJson = json_array();

         json_t* oJson = json_object();
         daemonState2Json(oJson);
         pushOutMessage(oJson, "daemonstate", client);

         oJson = json_object();
         s3200State2Json(oJson);
         pushOutMessage(oJson, "s3200-state", client);

         if (cl.second.page == "index")
         {
            if (addrsDashboard.size())
               for (const auto sensor : addrsDashboard)
                  json_array_append(oWsJson, jsonSensorList[sensor]);
            else
               for (auto sj : jsonSensorList)
                  json_array_append(oWsJson, sj.second);
         }
         else if (cl.second.page == "list")
         {
            if (addrsList.size())
               for (const auto sensor : addrsList)
                  json_array_append(oWsJson, jsonSensorList[sensor]);
            else
               for (auto sj : jsonSensorList)
                  json_array_append(oWsJson, sj.second);
         }
         else if (cl.second.page == "schema")
         {
            // #TODO - send visible instead of all??

            for (auto sj : jsonSensorList)
               json_array_append(oWsJson, sj.second);
         }

         pushOutMessage(oWsJson, title, (long)cl.first);
      }
   }

   // cleanup

   for (auto sj : jsonSensorList)
      json_decref(sj.second);

   jsonSensorList.clear();

   return success;
}

//***************************************************************************
// Init / Exit
//***************************************************************************

int P4d::init()
{
   int status {success};
   char* dictPath {nullptr};

   if (fileExists(httpPath))
   {
      char* link {nullptr};
      asprintf(&link, "%s/stylesheet.css", httpPath);

      if (!fileExists(link))
      {
         char* stylesheet {nullptr};
         asprintf(&stylesheet, "%s/stylesheet-dark.css", httpPath);
         tell(eloAlways, "Creating link '%s'", link);
         createLink(link, stylesheet, true);
         free(stylesheet);
      }

      free(link);
   }
   else
   {
      tell(eloAlways, "Missing http path '%s'", httpPath);
   }

   curl->init();

   // initialize the dictionary

   asprintf(&dictPath, "%s/p4d.dat", confDir);

   if (dbDict.in(dictPath) != success)
   {
      tell(0, "Fatal: Dictionary not loaded, aborting!");
      return 1;
   }

   tell(0, "Dictionary '%s' loaded", dictPath);
   free(dictPath);

   if ((status = initDb()) != success)
   {
      exitDb();
      return status;
   }

   // ---------------------------------
   // check users - add default user if empty

   int userCount {0};
   tableUsers->countWhere("", userCount);

   if (userCount <= 0)
   {
      tell(0, "Initially adding default user (p4/p4-3200)");

      md5Buf defaultPwd;
      createMd5("p4-3200", defaultPwd);
      tableUsers->clear();
      tableUsers->setValue("USER", "p4");
      tableUsers->setValue("PASSWD", defaultPwd);
      tableUsers->setValue("TOKEN", "dein&&secret12login34token");
      tableUsers->setValue("RIGHTS", 0xff);  // all rights
      tableUsers->store();
   }

   // Sensor Script

   asprintf(&sensorScript, "%s/script-sensor.sh", confDir);

   if (!fileExists(sensorScript))
   {
      tell(0, "Info: No sensor script '%s' found", sensorScript);
      free(sensorScript);
      sensorScript = nullptr;
   }
   else
   {
      tell(0, "Found sensor script '%s'", sensorScript);
   }

   // prepare one wire sensors

   w1.scan();

   // ---------------------------------
   // apply some configuration specials

   applyConfigurationSpecials();

   // init web socket ...

   while (webSock->init(webPort, webSocketPingTime, webSsl) != success)
   {
      tell(0, "Retrying in 2 seconds");
      sleep(2);
   }

   initialized = true;

   return success;
}

int P4d::exit()
{
   exitDb();
   serial->close();
   curl->exit();

   return success;
}

//***************************************************************************
// Init/Exit Database
//***************************************************************************

cDbFieldDef xmlTimeDef("XML_TIME", "xmltime", cDBS::ffAscii, 20, cDBS::ftData);
cDbFieldDef rangeFromDef("RANGE_FROM", "rfrom", cDBS::ffDateTime, 0, cDBS::ftData);
cDbFieldDef rangeToDef("RANGE_TO", "rto", cDBS::ffDateTime, 0, cDBS::ftData);
cDbFieldDef avgValueDef("AVG_VALUE", "avalue", cDBS::ffFloat, 122, cDBS::ftData);
cDbFieldDef maxValueDef("MAX_VALUE", "mvalue", cDBS::ffInt, 0, cDBS::ftData);
cDbFieldDef minValueDef("MIN_VALUE", "minvalue", cDBS::ffInt, 0, cDBS::ftData);
cDbFieldDef rangeEndDef("time", "time", cDBS::ffDateTime, 0, cDBS::ftData);
cDbFieldDef endTimeDef("END_TIME", "endtime", cDBS::ffDateTime, 0, cDBS::ftData);

int P4d::initDb()
{
   static int initial = yes;
   int status {success};

   if (connection)
      exitDb();

   tell(eloAlways, "Try conneting to database");

   connection = new cDbConnection();

   if (initial)
   {
      // ------------------------------------------
      // initially create/alter tables and indices
      // ------------------------------------------

      tell(0, "Checking database connection ...");

      if (connection->attachConnection() != success)
      {
         tell(0, "Error: Initial database connect failed");
         return fail;
      }

      tell(0, "Checking table structure and indices ...");

      for (auto t = dbDict.getFirstTableIterator(); t != dbDict.getTableEndIterator(); t++)
      {
         cDbTable* table = new cDbTable(connection, t->first.c_str());

         tell(1, "Checking table '%s'", t->first.c_str());

         if (!table->exist())
         {
            if ((status += table->createTable()) != success)
               continue;
         }
         else
         {
            status += table->validateStructure(2);
         }

         status += table->createIndices();

         delete table;
      }

      connection->detachConnection();

      if (status != success)
         return abrt;

      tell(0, "Checking table structure and indices succeeded");
   }

   // ------------------------
   // create/open tables
   // ------------------------

   tableValueFacts = new cDbTable(connection, "valuefacts");
   if (tableValueFacts->open() != success) return fail;

   tableGroups = new cDbTable(connection, "groups");
   if (tableGroups->open() != success) return fail;

   tableErrors = new cDbTable(connection, "errors");
   if (tableErrors->open() != success) return fail;

   tableMenu = new cDbTable(connection, "menu");
   if (tableMenu->open() != success) return fail;

   tableSamples = new cDbTable(connection, "samples");
   if (tableSamples->open() != success) return fail;

   tablePeaks = new cDbTable(connection, "peaks");
   if (tablePeaks->open() != success) return fail;

   tableSensorAlert = new cDbTable(connection, "sensoralert");
   if (tableSensorAlert->open() != success) return fail;

   tableSchemaConf = new cDbTable(connection, "schemaconf");
   if (tableSchemaConf->open() != success) return fail;

   tableConfig = new cDbTable(connection, "config");
   if (tableConfig->open() != success) return fail;

   tableTimeRanges = new cDbTable(connection, "timeranges");
   if (tableTimeRanges->open() != success) return fail;

   tableScripts = new cDbTable(connection, "scripts");
   if (tableScripts->open() != success) return fail;

   tableUsers = new cDbTable(connection, "users");
   if (tableUsers->open() != success) return fail;

   tablePellets = new cDbTable(connection, "pellets");
   if (tablePellets->open() != success) return fail;

   // prepare statements

   selectActiveValueFacts = new cDbStatement(tableValueFacts);

   selectActiveValueFacts->build("select ");
   selectActiveValueFacts->bindAllOut();
   selectActiveValueFacts->build(" from %s where ", tableValueFacts->TableName());
   selectActiveValueFacts->bind("STATE", cDBS::bndIn | cDBS::bndSet);

   status += selectActiveValueFacts->prepare();

   // ------------------

   selectAllValueFacts = new cDbStatement(tableValueFacts);

   selectAllValueFacts->build("select ");
   selectAllValueFacts->bindAllOut();
   selectAllValueFacts->build(" from %s", tableValueFacts->TableName());

   status += selectAllValueFacts->prepare();

   // ------------------

   selectAllGroups = new cDbStatement(tableGroups);

   selectAllGroups->build("select ");
   selectAllGroups->bindAllOut();
   selectAllGroups->build(" from %s", tableGroups->TableName());

   status += selectAllGroups->prepare();

   // ----------------

   selectAllMenuItems = new cDbStatement(tableMenu);

   selectAllMenuItems->build("select ");
   selectAllMenuItems->bindAllOut();
   selectAllMenuItems->build(" from %s", tableMenu->TableName());

   status += selectAllMenuItems->prepare();

   // ----------------

   selectMenuItemsByParent = new cDbStatement(tableMenu);

   selectMenuItemsByParent->build("select ");
   selectMenuItemsByParent->bindAllOut();
   selectMenuItemsByParent->build(" from %s where ", tableMenu->TableName());
   selectMenuItemsByParent->bind("PARENT", cDBS::bndIn | cDBS::bndSet);

   status += selectMenuItemsByParent->prepare();


   // ----------------

   selectMenuItemsByChild = new cDbStatement(tableMenu);

   selectMenuItemsByChild->build("select ");
   selectMenuItemsByChild->bindAllOut();
   selectMenuItemsByChild->build(" from %s where ", tableMenu->TableName());
   selectMenuItemsByChild->bind("CHILD", cDBS::bndIn | cDBS::bndSet);

   status += selectMenuItemsByChild->prepare();

   // ------------------

   selectSchemaConfByState = new cDbStatement(tableSchemaConf);

   selectSchemaConfByState->build("select ");
   selectSchemaConfByState->bindAllOut();
   selectSchemaConfByState->build(" from %s where ", tableSchemaConf->TableName());
   selectSchemaConfByState->bind("STATE", cDBS::bndIn | cDBS::bndSet);

   status += selectSchemaConfByState->prepare();

   // ------------------

   selectAllSchemaConf = new cDbStatement(tableSchemaConf);

   selectAllSchemaConf->build("select ");
   selectAllSchemaConf->bindAllOut();
   selectAllSchemaConf->build(" from %s", tableSchemaConf->TableName());

   status += selectAllSchemaConf->prepare();

   // ------------------

   selectSensorAlerts = new cDbStatement(tableSensorAlert);

   selectSensorAlerts->build("select ");
   selectSensorAlerts->bindAllOut();
   selectSensorAlerts->build(" from %s where state = 'A'", tableSensorAlert->TableName());
   selectSensorAlerts->bind("KIND", cDBS::bndIn | cDBS::bndSet, " and ");

   status += selectSensorAlerts->prepare();

      // ------------------

   selectAllSensorAlerts = new cDbStatement(tableSensorAlert);

   selectAllSensorAlerts->build("select ");
   selectAllSensorAlerts->bindAllOut();
   selectAllSensorAlerts->build(" from %s", tableSensorAlert->TableName());

   status += selectAllSensorAlerts->prepare();

   // ------------------
   // select min(value), time from samples
   //    where address = ? type = ?
   //     and time > ?

   minValue.setField(&minValueDef);
   selectStokerHours = new cDbStatement(tableSamples);

   selectStokerHours->build("select ");
   selectStokerHours->bindTextFree("min(value)", &minValue, "", cDBS::bndOut);
   selectStokerHours->bind("TIME", cDBS::bndOut, ", ");
   selectStokerHours->build(" from %s where ", tableSamples->TableName());
   selectStokerHours->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectStokerHours->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectStokerHours->bindCmp(0, "TIME", 0, ">", " and ");

   status += selectStokerHours->prepare();

   // ------------------
   // select * from samples      (for alertCheck)
   //    where type = ? and address = ?
   //     and time <= ?
   //     and time > ?

   rangeEnd.setField(&rangeEndDef);

   selectSampleInRange = new cDbStatement(tableSamples);

   selectSampleInRange->build("select ");
   selectSampleInRange->bind("ADDRESS", cDBS::bndOut);
   selectSampleInRange->bind("TYPE", cDBS::bndOut, ", ");
   selectSampleInRange->bind("TIME", cDBS::bndOut, ", ");
   selectSampleInRange->bind("VALUE", cDBS::bndOut, ", ");
   selectSampleInRange->build(" from %s where ", tableSamples->TableName());
   selectSampleInRange->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectSampleInRange->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectSampleInRange->bindCmp(0, &rangeEnd, "<=", " and ");
   selectSampleInRange->bindCmp(0, "TIME", 0, ">", " and ");
   selectSampleInRange->build(" order by time");

   status += selectSampleInRange->prepare();

   // ------------------
   // select samples for chart data
   // ein sample avg / 5 Minuten

   rangeFrom.setField(&rangeFromDef);
   rangeTo.setField(&rangeToDef);
   xmlTime.setField(&xmlTimeDef);
   avgValue.setField(&avgValueDef);
   maxValue.setField(&maxValueDef);
   selectSamplesRange = new cDbStatement(tableSamples);

   selectSamplesRange->build("select ");
   selectSamplesRange->bind("ADDRESS", cDBS::bndOut);
   selectSamplesRange->bind("TYPE", cDBS::bndOut, ", ");
   selectSamplesRange->bindTextFree("date_format(time, '%Y-%m-%dT%H:%i')", &xmlTime, ", ", cDBS::bndOut);
   selectSamplesRange->bindTextFree("avg(value)", &avgValue, ", ", cDBS::bndOut);
   selectSamplesRange->bindTextFree("max(value)", &maxValue, ", ", cDBS::bndOut);
   selectSamplesRange->build(" from %s where ", tableSamples->TableName());
   selectSamplesRange->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectSamplesRange->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectSamplesRange->bindCmp(0, "TIME", &rangeFrom, ">=", " and ");
   selectSamplesRange->bindCmp(0, "TIME", &rangeTo, "<=", " and ");
   selectSamplesRange->build(" group by date(time), ((60/5) * hour(time) + floor(minute(time)/5))");
   selectSamplesRange->build(" order by time");

   status += selectSamplesRange->prepare();

   // ------------------
   // select samples for chart data (for dashboard widget and rage > 15)
   // ein sample avg / 60 Minuten

   selectSamplesRange60 = new cDbStatement(tableSamples);

   selectSamplesRange60->build("select ");
   selectSamplesRange60->bind("ADDRESS", cDBS::bndOut);
   selectSamplesRange60->bind("TYPE", cDBS::bndOut, ", ");
   selectSamplesRange60->bindTextFree("date_format(time, '%Y-%m-%dT%H:%i')", &xmlTime, ", ", cDBS::bndOut);
   selectSamplesRange60->bindTextFree("avg(value)", &avgValue, ", ", cDBS::bndOut);
   selectSamplesRange60->bindTextFree("max(value)", &maxValue, ", ", cDBS::bndOut);
   selectSamplesRange60->build(" from %s where ", tableSamples->TableName());
   selectSamplesRange60->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectSamplesRange60->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectSamplesRange60->bindCmp(0, "TIME", &rangeFrom, ">=", " and ");
   selectSamplesRange60->bindCmp(0, "TIME", &rangeTo, "<=", " and ");
   selectSamplesRange60->build(" group by date(time), ((60/60) * hour(time) + floor(minute(time)/60))");
   selectSamplesRange60->build(" order by time");

   status += selectSamplesRange60->prepare();

   // ------------------
   // select samples for chart data
   // ein sample avg / Tag (for ranges > 300 Tage)

   selectSamplesRange720 = new cDbStatement(tableSamples);

   selectSamplesRange720->build("select ");
   selectSamplesRange720->bind("ADDRESS", cDBS::bndOut);
   selectSamplesRange720->bind("TYPE", cDBS::bndOut, ", ");
   selectSamplesRange720->bindTextFree("date_format(time, '%Y-%m-%dT%H:%i')", &xmlTime, ", ", cDBS::bndOut);
   selectSamplesRange720->bindTextFree("avg(value)", &avgValue, ", ", cDBS::bndOut);
   selectSamplesRange720->bindTextFree("max(value)", &maxValue, ", ", cDBS::bndOut);
   selectSamplesRange720->build(" from %s where ", tableSamples->TableName());
   selectSamplesRange720->bind("ADDRESS", cDBS::bndIn | cDBS::bndSet);
   selectSamplesRange720->bind("TYPE", cDBS::bndIn | cDBS::bndSet, " and ");
   selectSamplesRange720->bindCmp(0, "TIME", &rangeFrom, ">=", " and ");
   selectSamplesRange720->bindCmp(0, "TIME", &rangeTo, "<=", " and ");
   selectSamplesRange720->build(" group by date(time)");
   selectSamplesRange720->build(" order by time");

   status += selectSamplesRange720->prepare();

   // ------------------
   // state duration
   // select value, text, min(time)
   //  from samples
   //  where
   //    address = 1
   //    and type = 'UD'
   //    and text is not null
   //    and date(time) = curdate()
   //    and time > ?
   //    and vaue != ?

   endTime.setField(&endTimeDef);
   selectStateDuration = new cDbStatement(tableSamples);

   selectStateDuration->build("select ");
   selectStateDuration->bind("VALUE", cDBS::bndOut);
   selectStateDuration->bind("TEXT", cDBS::bndOut, ", ");
   selectStateDuration->bindTextFree("min(time)", &endTime, ", ", cDBS::bndOut);
   selectStateDuration->build(" from %s",  tableSamples->TableName());
   selectStateDuration->build(" where %s = 1", tableSamples->getField("ADDRESS")->getDbName());
   selectStateDuration->build(" and %s = 'UD'", tableSamples->getField("TYPE")->getDbName());
   selectStateDuration->build(" and %s is not null", tableSamples->getField("TEXT")->getDbName());
   selectStateDuration->build(" and date(%s) = curdate()", tableSamples->getField("TIME")->getDbName());
   selectStateDuration->bindCmp(0, "TIME", 0, ">", " and ");
   selectStateDuration->bindCmp(0, "VALUE", 0, "!=", " and ");

   status += selectStateDuration->prepare();

   // ------------------
   // all errors

   selectAllErrors = new cDbStatement(tableErrors);

   selectAllErrors->build("select ");
   selectAllErrors->bindAllOut();
   selectAllErrors->build(" from %s",  tableErrors->TableName());
   selectAllErrors->build(" order by time1 desc");

   status += selectAllErrors->prepare();

   // ------------------
   // pending errors

   selectPendingErrors = new cDbStatement(tableErrors);

   selectPendingErrors->build("select ");
   selectPendingErrors->bindAllOut();
   selectPendingErrors->build(" from %s where %s <> 'quittiert' and (%s <= 0 or %s is null)",
                              tableErrors->TableName(),
                              tableErrors->getField("STATE")->getDbName(),
                              tableErrors->getField("MAILCNT")->getDbName(),
                              tableErrors->getField("MAILCNT")->getDbName());

   status += selectPendingErrors->prepare();


   // --------------------
   // select max(time) from samples

   selectMaxTime = new cDbStatement(tableSamples);

   selectMaxTime->build("select ");
   selectMaxTime->bind("TIME", cDBS::bndOut, "max(");
   selectMaxTime->build(") from %s", tableSamples->TableName());

   status += selectMaxTime->prepare();

   // ------------------

   selectScriptByName = new cDbStatement(tableScripts);

   selectScriptByName->build("select ");
   selectScriptByName->bindAllOut();
   selectScriptByName->build(" from %s where ", tableScripts->TableName());
   selectScriptByName->bind("NAME", cDBS::bndIn | cDBS::bndSet);

   status += selectScriptByName->prepare();

   // ------------------
   //

   selectScript = new cDbStatement(tableScripts);

   selectScript->build("select ");
   selectScript->bindAllOut();
   selectScript->build(" from %s where ", tableScripts->TableName());
   selectScript->bind("PATH", cDBS::bndIn | cDBS::bndSet);

   status += selectScript->prepare();

   // ------------------
   // select all config

   selectAllConfig = new cDbStatement(tableConfig);

   selectAllConfig->build("select ");
   selectAllConfig->bindAllOut();
   selectAllConfig->build(" from %s", tableConfig->TableName());

   status += selectAllConfig->prepare();

   // ------------------
   // select all users

   selectAllUser = new cDbStatement(tableUsers);

   selectAllUser->build("select ");
   selectAllUser->bindAllOut();
   selectAllUser->build(" from %s", tableUsers->TableName());

   status += selectAllUser->prepare();

   // ------------------
   // select all pellets

   selectAllPellets = new cDbStatement(tablePellets);
   selectAllPellets->build("select ");
   selectAllPellets->bindAllOut();
   selectAllPellets->build(" from %s", tablePellets->TableName());
   selectAllPellets->build(" order by time");
   status += selectAllPellets->prepare();

   // ------------------

   if (status == success)
      tell(eloAlways, "Connection to database established");

   int gCount {0};

   if (connection->query(gCount, "select * from groups") == success)
   {
      if (!gCount)
      {
         connection->query("insert into groups set name='Heizung'");
         connection->query("update valuefacts set groupid = 1 where groupid is null or groupid = 0");
      }
   }

   readConfiguration();
   updateScripts();

   return status;
}

int P4d::exitDb()
{
   delete tableSamples;               tableSamples = nullptr;
   delete tablePeaks;                 tablePeaks = nullptr;
   delete tableValueFacts;            tableValueFacts = nullptr;
   delete tableGroups;                tableGroups = nullptr;
   delete tableUsers;                 tableUsers = nullptr;
   delete tablePellets;               tablePellets= nullptr;
   delete tableMenu;                  tableMenu = nullptr;
   delete tableSensorAlert;           tableSensorAlert = nullptr;
   delete tableSchemaConf;            tableSchemaConf = nullptr;
   delete tableErrors;                tableErrors = nullptr;
   delete tableConfig;                tableConfig = nullptr;
   delete tableTimeRanges;            tableTimeRanges = nullptr;
   delete tableScripts;               tableScripts = nullptr;

   delete selectActiveValueFacts;     selectActiveValueFacts = nullptr;
   delete selectAllValueFacts;        selectAllValueFacts = nullptr;
   delete selectAllGroups;            selectAllGroups = nullptr;
   delete selectAllMenuItems;         selectAllMenuItems = nullptr;
   delete selectMenuItemsByParent;    selectMenuItemsByParent = nullptr;
   delete selectMenuItemsByChild;     selectMenuItemsByChild = nullptr;
   delete selectSensorAlerts;         selectSensorAlerts = nullptr;
   delete selectAllSensorAlerts;      selectAllSensorAlerts = nullptr;
   delete selectSampleInRange;        selectSampleInRange = nullptr;
   delete selectAllErrors;            selectAllErrors = nullptr;
   delete selectPendingErrors;        selectPendingErrors = nullptr;
   delete selectMaxTime;              selectMaxTime = nullptr;
   delete selectScriptByName;         selectScriptByName = nullptr;
   delete selectScript;               selectScript = nullptr;
   delete selectAllConfig;            selectAllConfig = nullptr;
   delete selectAllUser;              selectAllUser = nullptr;
   delete selectAllPellets;           selectAllPellets = nullptr;
   delete selectStokerHours;          selectStokerHours = nullptr;
   delete selectSamplesRange;         selectSamplesRange = nullptr;
   delete selectSamplesRange60;       selectSamplesRange60 = nullptr;
   delete selectSamplesRange720;      selectSamplesRange720 = nullptr;
   delete selectStateDuration;        selectStateDuration = nullptr;
   delete selectSchemaConfByState;    selectSchemaConfByState = nullptr;
   delete selectAllSchemaConf;        selectAllSchemaConf = nullptr;
   delete connection; connection = nullptr;

   return done;
}

//***************************************************************************
// Read Configuration
//***************************************************************************

int P4d::readConfiguration()
{
   char* webUser {nullptr};
   char* webPass {nullptr};
   md5Buf defaultPwd;

   // init default web user and password

   createMd5("p4-3200", defaultPwd);
   getConfigItem("user", webUser, "p4");
   getConfigItem("passwd", webPass, defaultPwd);

   free(webUser);
   free(webPass);

   // init configuration

   getConfigItem("loglevel", loglevel, 1);
   getConfigItem("interval", interval, 60);
   getConfigItem("consumptionPerHour", consumptionPerHour, 0);

   getConfigItem("webPort", webPort, 1111);
   getConfigItem("webUrl", webUrl);
   getConfigItem("webSsl", webSsl);

   char* port {nullptr};
   asprintf(&port, "%d", webPort);
   if (isEmpty(webUrl) || !strstr(webUrl, port))
   {
      asprintf(&webUrl, "http%s://%s:%d", webSsl ? "s" : "", getFirstIp(), webPort);
      setConfigItem("webUrl", webUrl);
   }
   free(port);

   getConfigItem("knownStates", knownStates, "");

   if (!isEmpty(knownStates))
   {
      std::vector<std::string> sStates = split(knownStates, ':');
      for (const auto& s : sStates)
         stateDurations[atoi(s.c_str())] = 0;

      tell(eloAlways, "Loaded (%zu) states [%s]", stateDurations.size(), knownStates);
   }

   getConfigItem("stateCheckInterval", stateCheckInterval, 10);
   getConfigItem("ttyDevice", ttyDevice, "/dev/ttyUSB0");
   getConfigItem("heatingType", heatingType, "P4");
   tell(eloDetail, "The heating type is set to '%s'", heatingType);
   getConfigItem("iconSet", iconSet, "light");

   char* addrs {nullptr};
   getConfigItem("addrsDashboard", addrs, "");
   addrsDashboard = split(addrs, ',');
   getConfigItem("addrsList", addrs, "");
   addrsList = split(addrs, ',');
   free(addrs);

   getConfigItem("mail", mail, no);
   getConfigItem("mailScript", mailScript, BIN_PATH "/p4d-mail.sh");
   getConfigItem("stateMailStates", stateMailAtStates, "0,1,3,19");
   getConfigItem("stateMailTo", stateMailTo);
   getConfigItem("errorMailTo", errorMailTo);

   getConfigItem("aggregateInterval", aggregateInterval, 15);
   getConfigItem("aggregateHistory", aggregateHistory, 0);

   getConfigItem("tsync", tSync, no);
   getConfigItem("maxTimeLeak", maxTimeLeak, 10);

   getConfigItem("chart", chartSensors, "");

   getConfigItem("mqttDataTopic", mqttDataTopic, "p4d2mqtt/sensor/<NAME>/state");
   getConfigItem("mqttUrl", mqttUrl, "");          // "tcp://127.0.0.1:1883";
   getConfigItem("mqttUser", mqttUser, nullptr);
   getConfigItem("mqttPassword", mqttPassword, nullptr);
   getConfigItem("mqttHaveConfigTopic", mqttHaveConfigTopic, yes);
   getConfigItem("mqttDataTopic", mqttDataTopic, "p4d2mqtt/sensor/<NAME>/state");

   if (mqttDataTopic[strlen(mqttDataTopic)-1] == '/')
      mqttDataTopic[strlen(mqttDataTopic)-1] = '\0';

   if (isEmpty(mqttDataTopic) || isEmpty(mqttUrl))
      mqttInterfaceStyle = misNone;
   else if (strstr(mqttDataTopic, "<NAME>"))
      mqttInterfaceStyle = misMultiTopic;
   else if (strstr(mqttDataTopic, "<GROUP>"))
      mqttInterfaceStyle = misGroupedTopic;
   else
      mqttInterfaceStyle = misSingleTopic;

   for (int f = selectAllGroups->find(); f; f = selectAllGroups->fetch())
      groups[tableGroups->getIntValue("ID")].name = tableGroups->getStrValue("NAME");

   selectAllGroups->freeResult();

   return done;
}

int P4d::applyConfigurationSpecials()
{
   return done;
}

//***************************************************************************
// Initialize
//***************************************************************************

int P4d::initialize(int truncate)
{
   sem->p();

   tell(0, "opening %s", ttyDevice);

   if (serial->open(ttyDevice) != success)
   {
      sem->v();
      return fail;
   }

   if (request->check() != success)
   {
      tell(0, "request->check failed");
      serial->close();
      return fail;
   }

   updateTimeRangeData();

   if (!connection)
      return fail;

   // truncate config tables ?

   if (truncate)
   {
      tell(eloAlways, "Truncate configuration tables!");

      tableValueFacts->truncate();
      tableSchemaConf->truncate();
      tableMenu->truncate();
   }

   tell(eloAlways, "Requesting value facts from s 3200");
   initValueFacts();
   tell(eloAlways, "Update html schema configuration");
   updateSchemaConfTable();
   tell(eloAlways, "Requesting menu structure from s 3200");
   initMenu();

   serial->close();
   sem->v();

   return done;
}

//***************************************************************************
// Setup
//***************************************************************************

int P4d::setup()
{
   if (!connection)
      return fail;

   for (int f = selectAllValueFacts->find(); f; f = selectAllValueFacts->fetch())
   {
      char* res {nullptr};
      char buf[100+TB] {""};
      char oldState = tableValueFacts->getStrValue("STATE")[0];
      char state {oldState};

      printf("%s 0x%04lx '%s' (%s)",
             tableValueFacts->getStrValue("TYPE"),
             tableValueFacts->getIntValue("ADDRESS"),
             tableValueFacts->getStrValue("UNIT"),
             tableValueFacts->getStrValue("TITLE"));

      printf(" - aufzeichnen? (%s): ", oldState == 'A' ? "Y/n" : "y/N");

      if ((res = fgets(buf, 100, stdin)) && strlen(res) > 1)
         state = toupper(res[0]) == 'Y' ? 'A' : 'D';

      if (state != oldState && tableValueFacts->find())
      {
         tableValueFacts->setCharValue("STATE", state);
         tableValueFacts->store();
      }
   }

   selectAllValueFacts->freeResult();

   tell(eloAlways, "Update html schema configuration");
   updateSchemaConfTable();

   return done;
}

//***************************************************************************
// Update Conf Tables
//***************************************************************************

int P4d::updateSchemaConfTable()
{
   const int step = 20;
   int y = 50;
   int added = 0;

   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      int addr = tableValueFacts->getIntValue("ADDRESS");
      const char* type = tableValueFacts->getStrValue("TYPE");
      y += step;

      tableSchemaConf->clear();
      tableSchemaConf->setValue("ADDRESS", addr);
      tableSchemaConf->setValue("TYPE", type);

      if (!tableSchemaConf->find())
      {
         tableSchemaConf->setValue("KIND", "value");
         tableSchemaConf->setValue("STATE", "A");
         tableSchemaConf->setValue("COLOR", "black");
         tableSchemaConf->setValue("XPOS", 12);
         tableSchemaConf->setValue("YPOS", y);

         tableSchemaConf->store();
         added++;
      }
   }

   selectActiveValueFacts->freeResult();
   tell(eloAlways, "Added %d html schema configurations", added);

   return success;
}

//***************************************************************************
// Update Value Facts
//***************************************************************************

int P4d::initValueFacts()
{
   int status {success};
   Fs::ValueSpec v;
   int count {0};
   int added {0};
   int modified {0};

   // check serial communication

   if (request->check() != success)
   {
      serial->close();
      return fail;
   }

   // ---------------------------------
   // Add the sensor definitions delivered by the S 3200

   for (status = request->getFirstValueSpec(&v); status != Fs::wrnLast; status = request->getNextValueSpec(&v))
   {
      if (status != success)
         continue;

      tell(eloDebug, "%3d) 0x%04x '%s' %d '%s' (%04d) '%s'",
           count, v.address, v.name, v.factor, v.unit, v.type, v.description);

      // update table

      tableValueFacts->clear();
      tableValueFacts->setValue("ADDRESS", v.address);
      tableValueFacts->setValue("TYPE", "VA");

      if (!tableValueFacts->find())
      {
         tableValueFacts->setValue("NAME", v.name);
         tableValueFacts->setValue("STATE", "D");
         tableValueFacts->setValue("UNIT", strcmp(v.unit, "°") == 0 ? "°C" : v.unit);
         tableValueFacts->setValue("FACTOR", v.factor);
         tableValueFacts->setValue("TITLE", v.description);
         tableValueFacts->setValue("RES1", v.type);
         tableValueFacts->setValue("MAXSCALE", v.unit[0] == '%' ? 100 : 300);

         tableValueFacts->store();
         added++;
      }
      else
      {
         tableValueFacts->clearChanged();
         tableValueFacts->setValue("NAME", v.name);
         tableValueFacts->setValue("UNIT", strcmp(v.unit, "°") == 0 ? "°C" : v.unit);
         tableValueFacts->setValue("FACTOR", v.factor);
         tableValueFacts->setValue("TITLE", v.description);
         tableValueFacts->setValue("RES1", v.type);

         if (tableValueFacts->getValue("MAXSCALE")->isNull())
            tableValueFacts->setValue("MAXSCALE", v.unit[0] == '%' ? 100 : 300);

         if (tableValueFacts->getChanges())
         {
            tableValueFacts->store();
            modified++;
         }
      }

      count++;
   }

   tell(eloAlways, "Read %d value facts, modified %d and added %d", count, modified, added);
   count = 0;

   tell(0, "Update example value of table valuefacts");

   for (int f = selectAllValueFacts->find(); f; f = selectAllValueFacts->fetch())
   {
      if (!tableValueFacts->hasValue("TYPE", "VA"))
         continue;

      Value v(tableValueFacts->getIntValue("ADDRESS"));

      if ((status = request->getValue(&v)) != success)
      {
         tell(eloAlways, "Getting value 0x%04x failed, error %d", v.address, status);
         continue;
      }

      double factor = tableValueFacts->getIntValue("FACTOR");
      int dataType = tableValueFacts->getIntValue("RES1");
      int value = dataType == 1 ? (word)v.value : (sword)v.value;
      double theValue = value / factor;
      tableValueFacts->setValue("VALUE", theValue);
      tableValueFacts->update();
      count++;
   }

   selectAllValueFacts->freeResult();
   tell(0, "Updated %d example values", count);

   // ---------------------------------
   // add default for digital outputs

   added = 0;
   count = 0;
   modified = 0;

   for (int f = selectAllMenuItems->find(); f; f = selectAllMenuItems->fetch())
   {
      char* name = 0;
      const char* type = 0;
      int structType = tableMenu->getIntValue("TYPE");
      std::string sname = tableMenu->getStrValue("TITLE");

      switch (structType)
      {
         case mstDigOut: type = "DO"; break;
         case mstDigIn:  type = "DI"; break;
         case mstAnlOut: type = "AO"; break;
      }

      if (!type)
         continue;

      // update table

      tableValueFacts->clear();
      tableValueFacts->setValue("ADDRESS", tableMenu->getIntValue("ADDRESS"));
      tableValueFacts->setValue("TYPE", type);
      tableValueFacts->clearChanged();

      if (!tableValueFacts->find())
      {
         tableValueFacts->setValue("STATE", "D");
         added++;
         modified--;
      }

      const char* unit = tableMenu->getStrValue("UNIT");

      if (isEmpty(unit) && strcmp(type, "AO") == 0)
         unit = "%";

      removeCharsExcept(sname, nameChars);
      asprintf(&name, "%s_0x%x", sname.c_str(), (int)tableMenu->getIntValue("ADDRESS"));

      tableValueFacts->setValue("NAME", name);
      tableValueFacts->setValue("UNIT", unit);
      tableValueFacts->setValue("FACTOR", 1);
      tableValueFacts->setValue("TITLE", tableMenu->getStrValue("TITLE"));

      if (tableValueFacts->getValue("MAXSCALE")->isNull())
         tableValueFacts->setValue("MAXSCALE", v.unit[0] == '%' ? 100 : 300);

      if (tableValueFacts->getChanges())
      {
         tableValueFacts->store();
         modified++;
      }

      free(name);
      count++;
   }

   selectAllMenuItems->freeResult();
   tell(eloAlways, "Checked %d digital lines, added %d, modified %d", count, added, modified);

   // ---------------------------------
   // add value definitions for special data

   count = 0;
   added = 0;
   modified = 0;

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", udState);      // 1  -> Kessel Status
   tableValueFacts->setValue("TYPE", "UD");            // UD -> User Defined

   if (!tableValueFacts->find())
   {
      tableValueFacts->setValue("NAME", "Status");
      tableValueFacts->setValue("STATE", "A");
      tableValueFacts->setValue("UNIT", "zst");
      tableValueFacts->setValue("FACTOR", 1);
      tableValueFacts->setValue("TITLE", "Heizungsstatus");

      tableValueFacts->store();
      added++;
   }

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", udMode);       // 2  -> Kessel Mode
   tableValueFacts->setValue("TYPE", "UD");            // UD -> User Defined

   if (!tableValueFacts->find())
   {
      tableValueFacts->setValue("NAME", "Betriebsmodus");
      tableValueFacts->setValue("STATE", "A");
      tableValueFacts->setValue("UNIT", "zst");
      tableValueFacts->setValue("FACTOR", 1);
      tableValueFacts->setValue("TITLE", "Betriebsmodus");

      tableValueFacts->store();
      added++;
   }

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", udTime);       // 3  -> Kessel Zeit
   tableValueFacts->setValue("TYPE", "UD");            // UD -> User Defined

   if (!tableValueFacts->find())
   {
      tableValueFacts->setValue("NAME", "Uhrzeit");
      tableValueFacts->setValue("STATE", "A");
      tableValueFacts->setValue("UNIT", "T");
      tableValueFacts->setValue("FACTOR", 1);
      tableValueFacts->setValue("TITLE", "Datum Uhrzeit der Heizung");

      tableValueFacts->store();
      added++;
   }

   tell(eloAlways, "Added %d user defined values", added);

   // ---------------------------------
   // add one wire sensor data

   if (w1.scan() == success)
   {
      W1::SensorList* list = w1.getList();

      // yes, we have one-wire sensors

      count = 0;
      added = 0;
      modified = 0;

      for (W1::SensorList::iterator it = list->begin(); it != list->end(); ++it)
      {
         // update table

         tableValueFacts->clear();
         tableValueFacts->setValue("ADDRESS", (int)W1::toId(it->first.c_str()));
         tableValueFacts->setValue("TYPE", "W1");

         if (!tableValueFacts->find())
         {
            tableValueFacts->setValue("NAME", it->first.c_str());
            tableValueFacts->setValue("STATE", "D");
            tableValueFacts->setValue("UNIT", "°C");
            tableValueFacts->setValue("FACTOR", 1);
            tableValueFacts->setValue("TITLE", it->first.c_str());
            tableValueFacts->setValue("MAXSCALE", 300);

            tableValueFacts->store();
            added++;
         }
         else
         {
            if (tableValueFacts->getValue("MAXSCALE")->isNull())
               tableValueFacts->setValue("MAXSCALE", 300);

            if (tableValueFacts->getValue("UNIT")->hasValue("°"))
               tableValueFacts->setValue("UNIT", "°C");

            tableValueFacts->store();
            modified++;
         }
         count++;
      }

      tell(eloAlways, "Found %d one wire sensors, added %d, modified %d", count, added, modified);
   }

   return success;
}

//***************************************************************************
// Update Script Table
//***************************************************************************

int P4d::updateScripts()
{
   char* scriptPath = 0;
   DIR* dir;
   dirent* dp;

   asprintf(&scriptPath, "%s/scripts.d", confDir);

   if (!(dir = opendir(scriptPath)))
   {
      tell(0, "Info: Script path '%s' not exists - '%s'", scriptPath, strerror(errno));
      free(scriptPath);
      return fail;
   }

   while ((dp = readdir(dir)))
   {
      char* script = 0;
      asprintf(&script, "%s/%s", scriptPath, dp->d_name);

      tableScripts->clear();
      tableScripts->setValue("PATH", script);

      free(script);

      if (dp->d_type != DT_LNK && dp->d_type != DT_REG)
         continue;

      if (!selectScript->find())
      {
         tableScripts->setValue("NAME", dp->d_name);
         tableScripts->insert();
      }
   }

   closedir(dir);
   free(scriptPath);

   return done;
}

//***************************************************************************
// Initialize Menu Structure
//***************************************************************************

int P4d::initMenu(bool updateParameters)
{
   int status;
   Fs::MenuItem m;
   int count = 0;

   // check serial communication

   if (request->check() != success)
   {
      serial->close();
      return fail;
   }

   tableMenu->truncate();
   tableMenu->clear();

   // ...

   for (status = request->getFirstMenuItem(&m); status != Fs::wrnLast && !doShutDown();
        status = request->getNextMenuItem(&m))
   {
      if (status == wrnSkip)
         continue;

      if (status != success)
         break;

      tell(eloDebug, "%3d) Address: 0x%4x, parent: 0x%4x, child: 0x%4x; '%s'",
           count++, m.parent, m.address, m.child, m.description);

      // update table

      tableMenu->clear();

      tableMenu->setValue("STATE", "D");
      tableMenu->setValue("UNIT", m.type == mstAnlOut && isEmpty(m.unit) ? "%" : m.unit);

      tableMenu->setValue("PARENT", m.parent);
      tableMenu->setValue("CHILD", m.child);
      tableMenu->setValue("ADDRESS", m.address);
      tableMenu->setValue("TITLE", m.description);

      tableMenu->setValue("TYPE", m.type);
      tableMenu->setValue("UNKNOWN1", m.unknown1);
      tableMenu->setValue("UNKNOWN2", m.unknown2);

      tableMenu->insert();

      count++;
   }

   tell(eloAlways, "Read %d menu items", count);

   if (updateParameters)
   {
      count = 0;
      tell(eloAlways, "Update menu parameters");
      tableMenu->clear();

      for (int f = selectAllMenuItems->find(); f; f = selectAllMenuItems->fetch())
      {
         updateParameter(tableMenu);
         count++;
      }

      tell(eloAlways, "Updated %d menu parameters", count);
   }

   return success;
}

//***************************************************************************
// Update Time Range Data
//***************************************************************************

int P4d::updateTimeRangeData()
{
   Fs::TimeRanges t;
   int status;
   char fName[10+TB];
   char tName[10+TB];

   tell(0, "Updating time ranges data ...");

   if (request->check() != success)
   {
      tell(0, "request->check failed");
      serial->close();
      return fail;
   }

   // update / insert time ranges

   for (status = request->getFirstTimeRanges(&t); status == success; status = request->getNextTimeRanges(&t))
   {
      tableTimeRanges->clear();
      tableTimeRanges->setValue("ADDRESS", t.address);

      for (int n = 0; n < 4; n++)
      {
         sprintf(fName, "FROM%d", n+1);
         sprintf(tName, "TO%d", n+1);
         tableTimeRanges->setValue(fName, t.getTimeRangeFrom(n));
         tableTimeRanges->setValue(tName, t.getTimeRangeTo(n));
      }

      tableTimeRanges->store();
      tableTimeRanges->reset();

   }

   tell(0, "Updating time ranges data done");

   return done;
}

//***************************************************************************
// Store
//***************************************************************************

int P4d::store(time_t now, const char* name, const char* title, const char* unit,
               const char* type, int address, double value,
               uint factor, uint groupid, const char* text)
{
   // static time_t lastHmFailAt = 0;

   double theValue = value / (double)factor;

   if (strcmp(type, "VA") == 0)
      vaValues[address] = theValue;

   tableSamples->clear();

   tableSamples->setValue("TIME", now);
   tableSamples->setValue("ADDRESS", address);
   tableSamples->setValue("TYPE", type);
   tableSamples->setValue("AGGREGATE", "S");

   tableSamples->setValue("VALUE", theValue);
   tableSamples->setValue("TEXT", text);
   tableSamples->setValue("SAMPLES", 1);

   tableSamples->store();

   // peaks

   tablePeaks->clear();

   tablePeaks->setValue("ADDRESS", address);
   tablePeaks->setValue("TYPE", type);

   if (!tablePeaks->find())
   {
      tablePeaks->setValue("MIN", theValue);
      tablePeaks->setValue("MAX", theValue);
      tablePeaks->store();
   }
   else
   {
      if (theValue > tablePeaks->getFloatValue("MAX"))
         tablePeaks->setValue("MAX", theValue);

      if (theValue < tablePeaks->getFloatValue("MIN"))
         tablePeaks->setValue("MIN", theValue);

      tablePeaks->store();
   }

   // Home Assistant

   if (mqttInterfaceStyle == misSingleTopic)
      jsonAddValue(oJson, name, title, unit, theValue, groupid, text, initialRun /*forceConfig*/);
   else if (mqttInterfaceStyle == misGroupedTopic)
      jsonAddValue(groups[groupid].oJson, name, title, unit, theValue, 0, text, initialRun /*forceConfig*/);
   else if (mqttInterfaceStyle == misMultiTopic)
      mqttPublishSensor(name, title, unit, theValue, text, initialRun /*forceConfig*/);

   return success;
}

//***************************************************************************
// Schedule Time Sync In
//***************************************************************************

void P4d::scheduleTimeSyncIn(int offset)
{
   struct tm tm = {0};
   time_t now;

   now = time(0);
   localtime_r(&now, &tm);

   tm.tm_sec = 0;
   tm.tm_min = 0;
   tm.tm_hour = 3;
   tm.tm_isdst = -1;               // force DST auto detect

   nextTimeSyncAt = mktime(&tm);
   nextTimeSyncAt += offset;
}

//***************************************************************************
// standby
//***************************************************************************

int P4d::standby(int t)
{
   time_t end = time(0) + t;

   while (time(0) < end && !doShutDown())
   {
      meanwhile();
      usleep(50000);
   }

   return done;
}

int P4d::standbyUntil(time_t until)
{
   while (time(0) < until && !doShutDown())
   {
      meanwhile();
      loopCondition.TimedWait(loopMutex, 1000);
   }

   return done;
}

//***************************************************************************
// Meanwhile
//***************************************************************************

int P4d::meanwhile()
{
   if (!initialized)
      return done;

   if (!connection || !connection->isConnected())
      return fail;

   tell(3, "loop ...");

   dispatchClientRequest();

   if (!isEmpty(mqttUrl))
      performMqttRequests();

   return done;
}

//***************************************************************************
// Loop
//***************************************************************************

int P4d::loop()
{
   int status;
   time_t nextStateAt {0};
   int lastState {na};

   loopMutex.Lock();

   // info

   if (mail && !isEmpty(stateMailTo))
      tell(eloAlways, "Mail at states '%s' to '%s'", stateMailAtStates, stateMailTo);

   if (mail && !isEmpty(errorMailTo))
      tell(eloAlways, "Mail at errors to '%s'", errorMailTo);

   // init

   scheduleAggregate();

   sem->p();
   serial->open(ttyDevice);
   sem->v();

   while (!doShutDown())
   {
      stateChanged = false;

      // check db connection

      while (!doShutDown() && (!connection || !connection->isConnected()))
      {
         if (initDb() == success)
            break;
         else
            exitDb();

         tell(eloAlways, "Retrying in 10 seconds");
         standby(10);
      }

      if (doShutDown())
         break;

      meanwhile();
      standbyUntil(min(nextStateAt, nextAt));

      // aggregate

      if (aggregateHistory && nextAggregateAt <= time(0))
         aggregate();

      // update/check state

      status = updateState(&currentState);

      if (status != success)
      {
         sem->p();
         serial->close();
         tell(eloAlways, "Error reading serial interface, reopen now!");
         status = serial->open(ttyDevice);
         sem->v();

         if (status != success)
         {
            tell(eloAlways, "Retrying in 10 seconds");
            standby(10);
         }

         continue;
      }

      stateChanged = lastState != currentState.state;

      if (stateChanged)
      {
         mailBodyHtml = "";
         lastState = currentState.state;
         nextAt = time(0);              // force on state change
         tell(eloAlways, "State changed to '%s'", currentState.stateinfo);
      }

      nextStateAt = stateCheckInterval ? time(0) + stateCheckInterval : nextAt;

      // work expected?

      if (time(0) < nextAt)
         continue;

      // check serial connection

      sem->p();
      status = request->check();
      sem->v();

      if (status != success)
      {
         sem->p();
         serial->close();
         tell(eloAlways, "Error reading serial interface, reopen now");
         serial->open(ttyDevice);
         sem->v();

         continue;
      }

      // perform update

      nextAt = time(0) + interval;
      nextStateAt = stateCheckInterval ? time(0) + stateCheckInterval : nextAt;
      calcStateDuration();

      {
         sem->p();
         update();
         updateErrors();
         sem->v();
      }

      afterUpdate();

      if (stateChanged && mail)
      {
         sendStateMail();

         if (errorsPending)
            sendErrorMail();
      }

      initialRun = false;
   }

   serial->close();
   loopMutex.Unlock();

   return success;
}

//***************************************************************************
// Update State
//***************************************************************************

int P4d::updateState(Status* state)
{
   static time_t nextReportAt = 0;

   int status;
   time_t now;

   // get state

   sem->p();
   tell(eloDetail, "Checking state ...");
   status = request->getStatus(state);
   now = time(0);
   sem->v();

   if (status != success)
      return status;

   tell(eloDetail, "... got (%d) '%s'%s", state->state, toTitle(state->state),
        isError(state->state) ? " -> Störung" : "");

   // ----------------------
   // check time sync

   if (!nextTimeSyncAt)
      scheduleTimeSyncIn();

   if (tSync && maxTimeLeak && labs(state->time - now) > maxTimeLeak)
   {
      if (now > nextReportAt)
      {
         tell(eloAlways, "Time drift is %ld seconds", state->time - now);
         nextReportAt = now + 2 * tmeSecondsPerMinute;
      }

      if (now > nextTimeSyncAt)
      {
         scheduleTimeSyncIn(tmeSecondsPerDay);

         tell(eloAlways, "Time drift is %ld seconds, syncing now", state->time - now);

         sem->p();

         if (request->syncTime() == success)
            tell(eloAlways, "Time sync succeeded");
         else
            tell(eloAlways, "Time sync failed");

         sleep(2);   // S-3200 need some seconds to store time :o

         status = request->getStatus(state);
         now = time(0);

         sem->v();

         tell(eloAlways, "Time drift now %ld seconds", state->time - now);
      }
   }

   return status;
}

//***************************************************************************
// Update
//***************************************************************************

int P4d::update(bool webOnly, long client)
{
   int status;
   int count = 0;
   time_t now = time(0);
   char num[100];

   if (!webOnly)
      w1.update();

   tell(eloDetail, "Reading values ...");

   tableValueFacts->clear();
   tableValueFacts->setValue("STATE", "A");

   if (!webOnly)
      connection->startTransaction();

   for (auto sj : jsonSensorList)
      json_decref(sj.second);

   jsonSensorList.clear();

   if (mqttInterfaceStyle == misSingleTopic)
       oJson = json_object();

   for (int f = selectActiveValueFacts->find(); f; f = selectActiveValueFacts->fetch())
   {
      int addr = tableValueFacts->getIntValue("ADDRESS");
      double factor = tableValueFacts->getIntValue("FACTOR");
      const char* title = tableValueFacts->getStrValue("TITLE");
      const char* usrtitle = tableValueFacts->getStrValue("USRTITLE");
      const char* type = tableValueFacts->getStrValue("TYPE"); // VA, DI, ...
      int dataType = tableValueFacts->getIntValue("RES1");
      const char* unit = tableValueFacts->getStrValue("UNIT");
      const char* name = tableValueFacts->getStrValue("NAME");
      uint groupid = tableValueFacts->getIntValue("GROUPID");
      const char* orgTitle = title;

      if (!isEmpty(usrtitle))
         title = usrtitle;

      if (mqttInterfaceStyle == misGroupedTopic)
      {
         if (!groups[groupid].oJson)
            groups[groupid].oJson = json_object();
      }

      json_t* ojData = json_object();
      char* tupel {nullptr};
      asprintf(&tupel, "%s:0x%02x", type, addr);
      jsonSensorList[tupel] = ojData;
      free(tupel);

      sensor2Json(ojData, tableValueFacts);

      if (tableValueFacts->hasValue("TYPE", "VA"))
      {
         Value v(addr);

         if ((status = request->getValue(&v)) != success)
         {
            tell(eloAlways, "Getting value 0x%04x failed, error %d", addr, status);
            continue;
         }

         int value = dataType == 1 ? (word)v.value : (sword)v.value;
         double theValue = value / factor;

         json_object_set_new(ojData, "value", json_real(theValue));
         json_object_set_new(ojData, "image", json_string(getImageOf(orgTitle, title, theValue)));

         if (!webOnly)
         {
            store(now, name, title, unit, type, v.address, value, factor, groupid);
            sprintf(num, "%.2f%s", theValue, unit);
            addParameter2Mail(title, num);
         }
      }

      else if (sensorScript && tableValueFacts->hasValue("TYPE", "SC"))
      {
         std::string txt = getScriptSensor(addr).c_str();

         if (!txt.empty())
         {
            double value = strtod(txt.c_str(), 0);

            tell(eloDebug, "Debug: Got '%s' (%.2f) for (%d) from script", txt.c_str(), value, addr);
            json_object_set_new(ojData, "value", json_real(value / factor));

            if (!webOnly)
            {
               store(now, name, title, unit, type, addr, value, factor, groupid);
               sprintf(num, "%.2f%s", value / factor, unit);
               addParameter2Mail(title, num);
            }
         }
      }

      else if (tableValueFacts->hasValue("TYPE", "DO"))
      {
         Fs::IoValue v(addr);

         if (request->getDigitalOut(&v) != success)
         {
            tell(eloAlways, "Getting digital out 0x%04x failed, error %d", addr, status);
            continue;
         }

         json_object_set_new(ojData, "value", json_integer(v.state));
         json_object_set_new(ojData, "image", json_string(getImageOf(orgTitle, title, v.state)));

         if (!webOnly)
         {
            store(now, name, title, unit, type, v.address, v.state, factor, groupid);
            sprintf(num, "%d", v.state);
            addParameter2Mail(title, num);
         }
      }

      else if (tableValueFacts->hasValue("TYPE", "DI"))
      {
         Fs::IoValue v(addr);

         if (request->getDigitalIn(&v) != success)
         {
            tell(eloAlways, "Getting digital in 0x%04x failed, error %d", addr, status);
            continue;
         }

         json_object_set_new(ojData, "value", json_integer(v.state));
         json_object_set_new(ojData, "image", json_string(getImageOf(orgTitle, title, v.state)));

         if (!webOnly)
         {
            store(now, name, title, unit, type, v.address, v.state, factor, groupid);
            sprintf(num, "%d", v.state);
            addParameter2Mail(title, num);
         }
      }

      else if (tableValueFacts->hasValue("TYPE", "AO"))
      {
         Fs::IoValue v(addr);

         if (request->getAnalogOut(&v) != success)
         {
            tell(eloAlways, "Getting analog out 0x%04x failed, error %d", addr, status);
            continue;
         }

         json_object_set_new(ojData, "value", json_integer(v.state));

         if (!webOnly)
         {
            store(now, name, title, unit, type, v.address, v.state, factor, groupid);
            sprintf(num, "%d", v.state);
            addParameter2Mail(title, num);
         }
      }

      else if (tableValueFacts->hasValue("TYPE", "W1"))
      {
         double value = w1.valueOf(name);

         json_object_set_new(ojData, "value", json_real(value));

         if (!webOnly)
         {
            store(now, name, title, unit, type, addr, value, factor, groupid);
            sprintf(num, "%.2f%s", value / factor, unit);
            addParameter2Mail(title, num);
         }
      }

      else if (tableValueFacts->hasValue("TYPE", "SD"))   // state duration
      {
         const auto it = stateDurations.find(addr);

         if (it == stateDurations.end())
            continue;

         double value = stateDurations[addr] / 60;

         json_object_set_new(ojData, "value", json_real(value));

         if (!webOnly)
         {
            store(now, name, title, unit, type, addr, value, factor, groupid);
            sprintf(num, "%.2f%s", value / factor, unit);
            addParameter2Mail(title, num);
         }
      }

      else if (tableValueFacts->hasValue("TYPE", "UD"))
      {
         switch (tableValueFacts->getIntValue("ADDRESS"))
         {
            case udState:
            {
               json_object_set_new(ojData, "text", json_string(currentState.stateinfo));
               json_object_set_new(ojData, "image", json_string(getStateImage(currentState.state)));

               if (!webOnly)
               {
                  store(now, name, unit, title, type, udState, currentState.state, factor, groupid, currentState.stateinfo);
                  addParameter2Mail(title, currentState.stateinfo);
               }

               break;
            }
            case udMode:
            {
               json_object_set_new(ojData, "text", json_string(currentState.modeinfo));

               if (!webOnly)
               {
                  store(now, name, title, unit, type, udMode, currentState.mode, factor, groupid, currentState.modeinfo);
                  addParameter2Mail(title, currentState.modeinfo);
               }

               break;
            }
            case udTime:
            {
               std::string date = l2pTime(currentState.time, "%A, %d. %b. %Y %H:%M:%S");
               json_object_set_new(ojData, "text", json_string(date.c_str()));

               if (!webOnly)
               {
                  store(now, name, title, unit, type, udTime, currentState.time, factor, groupid, date.c_str());
                  addParameter2Mail(title, date.c_str());
               }

               break;
            }
         }
      }

      count++;
   }

   selectActiveValueFacts->freeResult();

   if (!webOnly)
   {
      connection->commit();
      tell(eloAlways, "Processed %d samples, state is '%s'", count, currentState.stateinfo);
   }

   // send result to all connected WEBIF clients

   pushDataUpdate(webOnly ? "init" : "all", client);

   tell(1, "MQTT InterfaceStyle is %d", mqttInterfaceStyle);

   // MQTT

   if (mqttInterfaceStyle == misSingleTopic)
   {
      mqttWrite(oJson, 0);
      json_decref(oJson);
      oJson = nullptr;
   }

   else if (mqttInterfaceStyle == misGroupedTopic)
   {
      tell(1, "Writing MQTT for %d groups", groups.size());

      for (auto it : groups)
      {
         if (it.second.oJson)
         {
            mqttWrite(it.second.oJson, it.first);
            json_decref(groups[it.first].oJson);
            groups[it.first].oJson = nullptr;
         }
      }
   }

   if (!webOnly)
      sensorAlertCheck(now);

   return success;
}

//***************************************************************************
// Calc State Duration
//***************************************************************************

int P4d::calcStateDuration()
{
   time_t beginTime {0};
   int thisState = {-1};
   std::string text {""};
   std::string kStates {""};

   for (auto& s : stateDurations)
   {
      s.second = 0;
      kStates += ":" + std::to_string(s.first);
   }

   if (knownStates != kStates)
   {
      setConfigItem("knownStates", kStates.c_str());
      getConfigItem("knownStates", knownStates, "");
   }

   tableSamples->clear();
   tableSamples->setValue("TIME", beginTime);
   tableSamples->setValue("VALUE", (double)thisState);

   while (selectStateDuration->find())
   {
      time_t eTime {time(0)};

      if (!endTime.isNull())
         eTime = endTime.getTimeValue();

      if (beginTime)
      {
         stateDurations[thisState] += eTime-beginTime;
         tell(3, "%s:0x%02x (%s) '%d/%s' %.2f minutes", "SD", thisState,
              l2pTime(beginTime).c_str(), thisState, text.c_str(), (eTime-beginTime) / 60.0);
      }

      if (endTime.isNull())
         break;

      thisState = tableSamples->getFloatValue("VALUE");
      text = tableSamples->getStrValue("TEXT");
      beginTime = eTime;

      addValueFact(thisState, "SD", 1, ("State_Duration_"+std::to_string(thisState)).c_str(),
                   "min", (std::string(text)+" (Laufzeit/Tag)").c_str(), false, 2000);

      selectStateDuration->freeResult();
      tableSamples->clear();
      tableSamples->setValue("TIME", beginTime);
      tableSamples->setValue("VALUE", (double)thisState);
   }

   selectStateDuration->freeResult();

   if (loglevel >= 2)
   {
      int total {0};

      for (const auto& d : stateDurations)
      {
         tell(2, "%d: %ld minutes", d.first, d.second / 60);
         total += d.second;
      }

      tell(2, "total: %d minutes", total / 60);
   }

   return success;
}

//***************************************************************************
// Get Script Sensor
//***************************************************************************

std::string P4d::getScriptSensor(int address)
{
   char* cmd {nullptr};

   if (!sensorScript)
      return "";

   asprintf(&cmd, "%s %d", sensorScript, address);
   tell(0, "Calling '%s'", cmd);
   std::string s = executeCommand(cmd);

   free(cmd);

   return s;
}

//***************************************************************************
// After Update
//***************************************************************************

void P4d::afterUpdate()
{
   char* path = 0;

   asprintf(&path, "%s/after-update.sh", confDir);

   if (fileExists(path))
   {
      tell(0, "Calling '%s'", path);
      system(path);
   }

   free(path);
}

//***************************************************************************
// Dispatch Mqtt Command Request
//   Format:  '{ "command" : "parstore", "address" : 0, "value" : "9" }'
//***************************************************************************

int P4d::dispatchMqttCommandRequest(const char* jString)
{
   json_error_t error;
   json_t* jData = json_loads(jString, 0, &error);

   if (!jData)
   {
      tell(0, "Error: Ignoring invalid json in [%s]", jString);
      tell(0, "Error decoding json: %s (%s, line %d column %d, position %d)",
           error.text, error.source, error.line, error.column, error.position);
      return fail;
   }

   const char* command = getStringFromJson(jData, "command", "");

   if (isEmpty(command))
   {
      tell(0, "Error: Missing 'command' in MQTT request [%s], ignoring", jString);
   }
   else if (strcmp(command, "parget") == 0)
   {
      int status {fail};
      json_t* jAddress = getObjectFromJson(jData, "address");
      int address = getIntFromJson(jData, "address", -1);

      if (!json_is_integer(jAddress) || address == -1)
      {
         tell(0, "Error: Missing address or invalid object type for MQTT command 'parstore' in [%s], ignoring", jString);
         return fail;
      }

      tell(0, "Perform MQTT command '%s' for address %d", command, address);

      ConfigParameter p(address);

      sem->p();
      status = request->getParameter(&p);
      sem->v();

      if (status != success)
      {
         tell(eloAlways, "Parameter request failed!");
         return fail;
      }

      tell(eloAlways, "Address: 0x%4.4x; Unit: %s; Value: %.*f", p.address, p.unit, p.digits, p.rValue);
   }
   else if (strcmp(command, "parstore") == 0)
   {
      int status {fail};
      json_t* jAddress = getObjectFromJson(jData, "address");
      int address = getIntFromJson(jData, "address", -1);
      const char* value = getStringFromJson(jData, "value");

      if (!json_is_integer(jAddress) || address == -1)
      {
         tell(0, "Error: Missing address or invalid object type for MQTT command 'parstore' in [%s], ignoring", jString);
         return fail;
      }

      if (isEmpty(value))
      {
         tell(0, "Error: Missing value for MQTT command 'parstore' in [%s], ignoring", jString);
         return fail;
      }

      tell(0, "Perform MQTT command '%s' for address %d with value '%s'", command, address, value);

      ConfigParameter p(address);

      sem->p();
      status = request->getParameter(&p);
      sem->v();

      if (status != success)
      {
         tell(eloAlways, "Set of parameter failed, query of current setting failed!");
         return fail;
      }

      if (p.setValueDirect(value, p.digits, p.getFactor()) != success)
      {
         tell(eloAlways, "Set of parameter failed, wrong format");
         return fail;
      }

      tell(eloAlways, "Storing value '%s' for parameter at address 0x%x", value, address);
      sem->p();
      status = request->setParameter(&p);
      sem->v();

      if (status == success)
      {
         tell(eloAlways, "Stored parameter");
      }
      else
      {
         tell(eloAlways, "Set of parameter failed, error was (%d)", status);

         if (status == P4Request::wrnNonUpdate)
            tell(eloAlways, "Value identical, ignoring request");
         else if (status == P4Request::wrnOutOfRange)
            tell(eloAlways, "Value out of range");
         else
            tell(eloAlways, "Serial communication error");
      }
   }
   else
   {
      tell(0, "Error: Got unexpected command '%s' in MQTT request [%s], ignoring", command, jString);
   }

   return success;
}

//***************************************************************************
// Sensor Alert Check
//***************************************************************************

void P4d::sensorAlertCheck(time_t now)
{
   tableSensorAlert->clear();
   tableSensorAlert->setValue("KIND", "M");

   // iterate over all alert roules ..

   for (int f = selectSensorAlerts->find(); f; f = selectSensorAlerts->fetch())
   {
      alertMailBody = "";
      alertMailSubject = "";

      performAlertCheck(tableSensorAlert->getRow(), now, 0);
   }

   selectSensorAlerts->freeResult();
}

//***************************************************************************
// Perform Alert Check
//***************************************************************************

int P4d::performAlertCheck(cDbRow* alertRow, time_t now, int recurse, int force)
{
   int alert = 0;

   // data from alert row

   int addr = alertRow->getIntValue("ADDRESS");
   const char* type = alertRow->getStrValue("TYPE");

   int id = alertRow->getIntValue("ID");
   int lgop = alertRow->getIntValue("LGOP");
   time_t lastAlert = alertRow->getIntValue("LASTALERT");
   int maxRepeat = alertRow->getIntValue("MAXREPEAT");

   int minIsNull = alertRow->getValue("MIN")->isNull();
   int maxIsNull = alertRow->getValue("MAX")->isNull();
   int min = alertRow->getIntValue("MIN");
   int max = alertRow->getIntValue("MAX");

   int range = alertRow->getIntValue("RANGEM");
   int delta = alertRow->getIntValue("DELTA");

   // lookup value facts

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", addr);
   tableValueFacts->setValue("TYPE", type);

   // lookup samples

   tableSamples->clear();
   tableSamples->setValue("ADDRESS", addr);
   tableSamples->setValue("TYPE", type);
   tableSamples->setValue("AGGREGATE", "S");
   tableSamples->setValue("TIME", now);

   if (!tableSamples->find() || !tableValueFacts->find())
   {
      tell(eloAlways, "Info: Can't perform sensor check for %s/%d '%s'", type, addr, l2pTime(now).c_str());
      return 0;
   }

   // data from samples and value facts

   double value = tableSamples->getFloatValue("VALUE");

   const char* title = tableValueFacts->getStrValue("TITLE");
   const char* unit = tableValueFacts->getStrValue("UNIT");

   // -------------------------------
   // check min / max threshold

   if (!minIsNull || !maxIsNull)
   {
      if (force || (!minIsNull && value < min) || (!maxIsNull && value > max))
      {
         tell(eloAlways, "%d) Alert for sensor %s/0x%x, value %.2f not in range (%d - %d)",
              id, type, addr, value, min, max);

         // max one alert mail per maxRepeat [minutes]

         if (force || !lastAlert || lastAlert < time(0)- maxRepeat * tmeSecondsPerMinute)
         {
            alert = 1;
            add2AlertMail(alertRow, title, value, unit);
         }
      }
   }

   // -------------------------------
   // check range delta

   if (range && delta)
   {
      // select value of this sensor around 'time = (now - range)'

      time_t rangeStartAt = time(0) - range*tmeSecondsPerMinute;
      time_t rangeEndAt = rangeStartAt + interval;

      tableSamples->clear();
      tableSamples->setValue("ADDRESS", addr);
      tableSamples->setValue("TYPE", type);
      tableSamples->setValue("AGGREGATE", "S");
      tableSamples->setValue("TIME", rangeStartAt);
      rangeEnd.setValue(rangeEndAt);

      if (selectSampleInRange->find())
      {
         double oldValue = tableSamples->getFloatValue("VALUE");

         if (force || labs(value - oldValue) > delta)
         {
            tell(eloAlways, "%d) Alert for sensor %s/0x%x , value %.2f changed more than %d in %d minutes",
                 id, type, addr, value, delta, range);

            // max one alert mail per maxRepeat [minutes]

            if (force || !lastAlert || lastAlert < time(0)- maxRepeat * tmeSecondsPerMinute)
            {
               alert = 1;
               add2AlertMail(alertRow, title, value, unit);
            }
         }
      }

      selectSampleInRange->freeResult();
      tableSamples->reset();
   }

   // ---------------------------
   // Check sub rules recursive

   if (alertRow->getIntValue("SUBID") > 0)
   {
      if (recurse > 50)
      {
         tell(eloAlways, "Info: Aborting recursion after 50 steps, seems to be a config error!");
      }
      else
      {
         tableSensorAlert->clear();
         tableSensorAlert->setValue("ID", alertRow->getIntValue("SUBID"));

         if (tableSensorAlert->find())
         {
            int sAlert = performAlertCheck(tableSensorAlert->getRow(), now, recurse+1);

            switch (lgop)
            {
               case loAnd:    alert = alert &&  sAlert; break;
               case loOr:     alert = alert ||  sAlert; break;
               case loAndNot: alert = alert && !sAlert; break;
               case loOrNot:  alert = alert || !sAlert; break;
            }
         }
      }
   }

   // ---------------------------------
   // update master row and send mail

   if (alert && !recurse)
   {
      tableSensorAlert->clear();
      tableSensorAlert->setValue("ID", id);

      if (tableSensorAlert->find())
      {
         if (!force)
         {
            tableSensorAlert->setValue("LASTALERT", time(0));
            tableSensorAlert->update();
         }

         sendAlertMail(tableSensorAlert->getStrValue("MADDRESS"));
      }
   }

   return alert;
}

//***************************************************************************
// Add Parameter To Mail
//***************************************************************************

void P4d::addParameter2Mail(const char* name, const char* value)
{
   if (stateChanged)
      mailBodyHtml += "        <tr><td>" + std::string(name) + "</td><td>" + std::string(value) + "</td></tr>\n";
}

//***************************************************************************
// Schedule Aggregate
//***************************************************************************

int P4d::scheduleAggregate()
{
   struct tm tm = { 0 };
   time_t now;

   if (!aggregateHistory)
   {
      tell(0, "NO aggregateHistory configured!");
      return done;
   }

   // calc today at 01:00:00

   now = time(0);
   localtime_r(&now, &tm);

   tm.tm_sec = 0;
   tm.tm_min = 0;
   tm.tm_hour = 1;
   tm.tm_isdst = -1;               // force DST auto detect

   nextAggregateAt = mktime(&tm);

   // if in the past ... skip to next day ...

   if (nextAggregateAt <= time(0))
      nextAggregateAt += tmeSecondsPerDay;

   tell(eloAlways, "Scheduled aggregation for '%s' with interval of %d minutes",
        l2pTime(nextAggregateAt).c_str(), aggregateInterval);

   return success;
}

//***************************************************************************
// Aggregate
//***************************************************************************

int P4d::aggregate()
{
   char* stmt = 0;
   time_t history = time(0) - (aggregateHistory * tmeSecondsPerDay);
   int aggCount = 0;

   asprintf(&stmt,
            "replace into samples "
            "  select address, type, 'A' as aggregate, "
            "    CONCAT(DATE(time), ' ', SEC_TO_TIME((TIME_TO_SEC(time) DIV %d) * %d)) + INTERVAL %d MINUTE time, "
            "    unix_timestamp(sysdate()) as inssp, unix_timestamp(sysdate()) as updsp, "
            "    round(sum(value)/count(*), 2) as value, text, count(*) samples "
            "  from "
            "    samples "
            "  where "
            "    aggregate != 'A' and "
            "    time <= from_unixtime(%ld) "
            "  group by "
            "    CONCAT(DATE(time), ' ', SEC_TO_TIME((TIME_TO_SEC(time) DIV %d) * %d)) + INTERVAL %d MINUTE, address, type;",
            aggregateInterval * tmeSecondsPerMinute, aggregateInterval * tmeSecondsPerMinute, aggregateInterval,
            history,
            aggregateInterval * tmeSecondsPerMinute, aggregateInterval * tmeSecondsPerMinute, aggregateInterval);

   tell(eloAlways, "Starting aggregation ...");

   if (connection->query(aggCount, "%s", stmt) == success)
   {
      int delCount = 0;

      tell(eloDebug, "Aggregation: [%s]", stmt);
      free(stmt);

      // Einzelmesspunkte löschen ...

      asprintf(&stmt, "aggregate != 'A' and time <= from_unixtime(%ld)", history);

      if (tableSamples->deleteWhere(stmt, delCount) == success)
      {
         tell(eloAlways, "Aggregation with interval of %d minutes done; "
              "Created %d aggregation rows, deleted %d sample rows",
              aggregateInterval, aggCount, delCount);
      }
   }

   free(stmt);

   // schedule even in case of error!

   scheduleAggregate();

   return success;
}

//***************************************************************************
// Update Errors
//***************************************************************************

int P4d::updateErrors()
{
   int status;
   Fs::ErrorInfo e;
   char timeField[5+TB] = "";
   time_t timeOne = 0;
   cTimeMs timeMs;

   cDbStatement* select = new cDbStatement(tableErrors);
   select->build("select ");
   select->bindAllOut();
   select->build(" from %s where ", tableErrors->TableName());
   select->bind("NUMBER", cDBS::bndIn | cDBS::bndSet);
   select->bind("TIME1", cDBS::bndIn | cDBS::bndSet, " and ");

   if (select->prepare() != success)
   {
      tell(eloAlways, "prepare failed!");
      return fail;
   }

   tell(eloAlways, "Updating error list");

   for (status = request->getFirstError(&e); status == success; status = request->getNextError(&e))
   {
      int insert = yes;

      sprintf(timeField, "TIME%d", e.state);

      tell(eloDebug2, "Debug: S-3200 error-message %d / %d '%s' '%s' %d [%s]; (for %s)",
           e.number, e.state, l2pTime(e.time).c_str(),  Fs::errState2Text(e.state), e.info, e.text,
           timeField);

      if (e.state == 1)
         timeOne = e.time;

      if (!timeOne)
         continue;

      if (timeOne)
      {
         tableErrors->clear();
         tableErrors->setValue("NUMBER", e.number);
         tableErrors->setValue("TIME1", timeOne);

         insert = !select->find();
      }

      tableErrors->clearChanged();

      if (insert
          || (e.state == 2 && !tableErrors->hasValue("STATE", Fs::errState2Text(2)))
          || (e.state == 4 && tableErrors->hasValue("STATE", Fs::errState2Text(1))))
      {
         tableErrors->setValue(timeField, e.time);
         tableErrors->setValue("STATE", Fs::errState2Text(e.state));
         tableErrors->setValue("NUMBER", e.number);
         tableErrors->setValue("INFO", e.info);
         tableErrors->setValue("TEXT", e.text);
      }

      if (insert)
         tableErrors->insert();
      else if (tableErrors->getChanges())
         tableErrors->update();

      if (e.state == 2)
         timeOne = 0;
   }

   delete select;

   tell(eloAlways, "Updating error list done in %" PRIu64 "ms", timeMs.Elapsed());

   // count pending (not 'quittiert' AND not mailed) errors

   tableErrors->clear();
   selectPendingErrors->find();
   errorsPending = selectPendingErrors->getResultCount();
   selectPendingErrors->freeResult();

   tell(eloDetail, "Info: Found (%d) pending errors", errorsPending);

   return success;
}

int P4d::updateParameter(cDbTable* tableMenu)
{
   int type = tableMenu->getIntValue("TYPE");
   int paddr = tableMenu->getIntValue("ADDRESS");
   int child = tableMenu->getIntValue("CHILD");

   tell(3, "Update parameter %d/%d ...", type, paddr);

   sem->p();

   if (type == mstFirmware)
   {
      Fs::Status s;

      if (request->getStatus(&s) == success)
      {
         if (tableMenu->find())
         {
            tableMenu->setValue("VALUE", s.version);
            tableMenu->setValue("UNIT", "");
            tableMenu->update();
         }
      }
   }

   else if (type == mstDigOut || type == mstDigIn || type == mstAnlOut)
   {
      int status;
      Fs::IoValue v(paddr);

      if (type == mstDigOut)
         status = request->getDigitalOut(&v);
      else if (type == mstDigIn)
         status = request->getDigitalIn(&v);
      else
         status = request->getAnalogOut(&v);

      if (status == success)
      {
         char* buf = 0;

         if (type == mstAnlOut)
         {
            if (v.mode == 0xff)
               asprintf(&buf, "%d (A)", v.state);
            else
               asprintf(&buf, "%d (%d)", v.state, v.mode);
         }
         else
            asprintf(&buf, "%s (%c)", v.state ? "on" : "off", v.mode);

         if (tableMenu->find())
         {
            tableMenu->setValue("VALUE", buf);
            tableMenu->setValue("UNIT", "");
            tableMenu->update();
         }

         free(buf);
      }
   }

   else if (type == mstMesswert || type == mstMesswert1)
   {
      int status;
      Fs::Value v(paddr);

      tableValueFacts->clear();
      tableValueFacts->setValue("TYPE", "VA");
      tableValueFacts->setValue("ADDRESS", paddr);

      if (tableValueFacts->find())
      {
         double factor = tableValueFacts->getIntValue("FACTOR");
         const char* unit = tableValueFacts->getStrValue("UNIT");
         int dataType = tableValueFacts->getIntValue("RES1");

         status = request->getValue(&v);

         if (status == success)
         {
            char* buf = 0;
            int value = dataType == 1 ? (word)v.value : (sword)v.value;

            asprintf(&buf, "%.2f", value / factor);

            if (tableMenu->find())
            {
               tableMenu->setValue("VALUE", buf);
               tableMenu->setValue("UNIT", strcmp(unit, "°") == 0 ? "°C" : unit);
               tableMenu->update();
            }

            free(buf);
         }
      }
   }
   else if (isGroup(type) || type == mstBusValues || type == mstReset || type == mstEmpty)
   {
      // nothing to do
   }
   else if (child)
   {
      // I have childs -> I have no value -> nothing to do
   }
   else if (paddr == 0 && type != mstPar)
   {
      // address 0 only for type mstPar
   }
   else if (paddr == 9997 || paddr == 9998 || paddr == 9999)
   {
      // this 3 'special' addresses takes a long while and don't deliver any usefull data
   }
   else
   {
      Fs::ConfigParameter p(paddr);

      if (request->getParameter(&p) == success)
      {
         cRetBuf value = p.toNice(type);

         if (tableMenu->find())
         {
            tableMenu->setValue("VALUE", value);
            tableMenu->setValue("UNIT", strcmp(p.unit, "°") == 0 ? "°C" : p.unit);
            tableMenu->setValue("DIGITS", p.digits);
            tableMenu->setValue("MIN", p.rMin);
            tableMenu->setValue("MAX", p.rMax);
            tableMenu->setValue("DEF", p.rDefault);
            tableMenu->setValue("FACTOR", p.getFactor());
            tableMenu->setValue("PUB1", p.ub1);
            tableMenu->setValue("PUB2", p.ub2);
            tableMenu->setValue("PUB3", p.ub3);
            tableMenu->setValue("PUW1", p.uw1);
            tableMenu->update();
         }
      }
   }

   sem->v();

   return done;
}

//***************************************************************************
// Send Mail
//***************************************************************************

int P4d::sendAlertMail(const char* to)
{
   // check

   if (isEmpty(to) || isEmpty(mailScript))
      return done;

   if (alertMailBody.empty())
      alertMailBody = "- undefined -";

   char* html = 0;

   alertMailBody = strReplace("\n", "<br/>\n", alertMailBody);

   const char* htmlHead =
      "<head>\n"
      "  <style type=\"text/css\">\n"
      "    caption { background: #095BA6; font-family: Arial Narrow; color: #fff; font-size: 18px; }\n"
      "  </style>\n"
      "</head>\n";

   asprintf(&html,
            "<html>\n"
            " %s"
            " <body>\n"
            "  %s\n"
            " </body>\n"
            "</html>\n",
            htmlHead, alertMailBody.c_str());

   alertMailBody = html;
   free(html);

   // send mail

   return sendMail(to, alertMailSubject.c_str(), alertMailBody.c_str(), "text/html");
}

//***************************************************************************
// Send Mail
//***************************************************************************

int P4d::add2AlertMail(cDbRow* alertRow, const char* title, double value, const char* unit)
{
   char* sensor = 0;

   std::string subject = alertRow->getStrValue("MSUBJECT");
   std::string body = alertRow->getStrValue("MBODY");
   int addr = alertRow->getIntValue("ADDRESS");
   const char* type = alertRow->getStrValue("TYPE");

   int min = alertRow->getIntValue("MIN");
   int max = alertRow->getIntValue("MAX");
   int range = alertRow->getIntValue("RANGEM");
   int delta = alertRow->getIntValue("DELTA");
   int maxRepeat = alertRow->getIntValue("MAXREPEAT");
   double minv {0};
   double maxv {0};

   tablePeaks->clear();
   tablePeaks->setValue("ADDRESS", alertRow->getIntValue("ADDRESS"));
   tablePeaks->setValue("TYPE", alertRow->getStrValue("TYPE"));

   if (tablePeaks->find())
   {
      minv = tablePeaks->getFloatValue("MIN");
      maxv = tablePeaks->getFloatValue("MAX");
   }

   tablePeaks->reset();

   if (!body.length())
      body = "- undefined -";

   // prepare

   asprintf(&sensor, "%s/0x%x", type, addr);

   // templating

   subject = strReplace("%sensorid%", sensor, subject);
   subject = strReplace("%value%", value, subject);
   subject = strReplace("%unit%", unit, subject);
   subject = strReplace("%title%", title, subject);
   subject = strReplace("%min%", (long)min, subject);
   subject = strReplace("%max%", (long)max, subject);
   subject = strReplace("%range%", (long)range, subject);
   subject = strReplace("%delta%", (long)delta, subject);
   subject = strReplace("%time%", l2pTime(time(0)).c_str(), subject);
   subject = strReplace("%repeat%", (long)maxRepeat, subject);
   subject = strReplace("%weburl%", webUrl, subject);
   subject = strReplace("%minv%", minv, subject);
   subject = strReplace("%maxv%", maxv, subject);

   body = strReplace("%sensorid%", sensor, body);
   body = strReplace("%value%", value, body);
   body = strReplace("%unit%", unit, body);
   body = strReplace("%title%", title, body);
   body = strReplace("%min%", (long)min, body);
   body = strReplace("%max%", (long)max, body);
   body = strReplace("%range%", (long)range, body);
   body = strReplace("%delta%", (long)delta, body);
   body = strReplace("%time%", l2pTime(time(0)).c_str(), body);
   body = strReplace("%repeat%", (long)maxRepeat, body);
   body = strReplace("%weburl%", webUrl, body);
   body = strReplace("%minv%", minv, body);
   body = strReplace("%maxv%", maxv, body);

   alertMailSubject += std::string(" ") + subject;
   alertMailBody += std::string("\n") + body;

   free(sensor);

   return success;
}

//***************************************************************************
// Send Error Mail
//***************************************************************************

int P4d::sendErrorMail()
{
   std::string body = "";
   const char* subject = "Heizung: STÖRUNG";
   char* html = 0;

   // check

   if (isEmpty(mailScript) || isEmpty(errorMailTo))
      return done;

   // build mail ..

   for (int f = selectPendingErrors->find(); f; f = selectPendingErrors->fetch())
   {
      char* line = 0;
      time_t t = std::max(std::max(tableErrors->getTimeValue("TIME1"), tableErrors->getTimeValue("TIME4")), tableErrors->getTimeValue("TIME2"));

      asprintf(&line, "        <tr><td>%s</td><td>%s</td><td>%s</td></tr>\n",
               l2pTime(t).c_str(), tableErrors->getStrValue("TEXT"), tableErrors->getStrValue("STATE"));

      body += line;

      tell(eloDebug, "Debug: MAILCNT is (%ld), setting to (%ld)",
           tableErrors->getIntValue("MAILCNT"), tableErrors->getIntValue("MAILCNT")+1);
      tableErrors->find();
      tableErrors->setValue("MAILCNT", tableErrors->getIntValue("MAILCNT")+1);
      tableErrors->update();

      free(line);
   }

   selectPendingErrors->freeResult();

   // HTML mail

   loadHtmlHeader();

   asprintf(&html,
            "<html>\n"
            " %s\n"
            "  <body>\n"
            "   <font face=\"Arial\"><br/>WEB Interface: <a href=\"%s\">S 3200</a><br/></font>\n"
            "   <br/>\n"
            "Aktueller Status: %s"
            "   <br/>\n"
            "   <table>\n"
            "     <thead>\n"
            "       <tr class=\"head\">\n"
            "         <th><font>Zeit</font></th>\n"
            "         <th><font>Fehler</font></th>\n"
            "         <th><font>Status</font></th>\n"
            "       </tr>\n"
            "     </thead>\n"
            "     <tbody>\n"
            "%s"
            "     </tbody>\n"
            "   </table>\n"
            "   <br/>\n"
            "  </body>\n"
            "</html>\n",
            htmlHeader.memory, webUrl, currentState.stateinfo, body.c_str());

   int result = sendMail(errorMailTo, subject, html, "text/html");

   free(html);

   return result;
}

//***************************************************************************
// Send State Mail
//***************************************************************************

int P4d::sendStateMail()
{
   std::string subject = "Heizung - Status: " + std::string(currentState.stateinfo);

   // check

   if (!isMailState() || isEmpty(mailScript) || !mailBodyHtml.length() || isEmpty(stateMailTo))
      return done;

   // HTML mail

   char* html = 0;

   loadHtmlHeader();

   asprintf(&html,
            "<html>\n"
            " %s\n"
            "  <body>\n"
            "   <font face=\"Arial\"><br/>WEB Interface: <a href=\"%s\">S 3200</a><br/></font>\n"
            "   <br/>\n"
            "   <table>\n"
            "     <thead>\n"
            "       <tr class=\"head\">\n"
            "         <th><font>Parameter</font></th>\n"
            "         <th><font>Wert</font></th>\n"
            "       </tr>\n"
            "     </thead>\n"
            "     <tbody>\n"
            "%s"
            "     </tbody>\n"
            "   </table>\n"
            "   <br/>\n"
            "  </body>\n"
            "</html>\n",
            htmlHeader.memory, webUrl, mailBodyHtml.c_str());

   int result = sendMail(stateMailTo, subject.c_str(), html, "text/html");
   free(html);
   mailBodyHtml = "";

   return result;
}

int P4d::sendMail(const char* receiver, const char* subject, const char* body, const char* mimeType)
{
   char* command = {nullptr};
   int result {0};

   asprintf(&command, "%s '%s' '%s' '%s' '%s'", mailScript, subject, body, mimeType, receiver);
   result = system(command);
   free(command);

   if (loglevel >= eloDebug)
      tell(eloAlways, "Send mail '%s' with [%s] to '%s'", subject, body, receiver);
   else
      tell(eloAlways, "Send mail '%s' to '%s'", subject, receiver);

   return result;
}

//***************************************************************************
// Is Mail State
//***************************************************************************

int P4d::isMailState()
{
   int result = no;
   char* mailStates = 0;

   if (isEmpty(stateMailAtStates))
      return yes;

   mailStates = strdup(stateMailAtStates);

   for (const char* p = strtok(mailStates, ","); p; p = strtok(0, ","))
   {
      if (atoi(p) == currentState.state)
      {
         result = yes;
         break;
      }
   }

   free(mailStates);

   return result;
}

//***************************************************************************
// Load Html Header
//***************************************************************************

int P4d::loadHtmlHeader()
{
   char* file;

   // load only once at first call

   if (!htmlHeader.isEmpty())
      return done;

   asprintf(&file, "%s/mail-head.html", confDir);

   if (fileExists(file))
      if (loadFromFile(file, &htmlHeader) == success)
         htmlHeader.append("\0");

   free(file);

   if (!htmlHeader.isEmpty())
      return success;

   htmlHeader.clear();

   htmlHeader.append("  <head>\n"
                     "    <style type=\"text/css\">\n"
                     "      table {"
                     "        border: 1px solid #d2d2d2;\n"
                     "        border-collapse: collapse;\n"
                     "      }\n"
                     "      table tr.head {\n"
                     "        background-color: #004d8f;\n"
                     "        color: #fff;\n"
                     "        font-weight: bold;\n"
                     "        font-family: Helvetica, Arial, sans-serif;\n"
                     "        font-size: 12px;\n"
                     "      }\n"
                     "      table tr th,\n"
                     "      table tr td {\n"
                     "        padding: 4px;\n"
                     "        text-align: left;\n"
                     "      }\n"
                     "      table tr:nth-child(1n) td {\n"
                     "        background-color: #fff;\n"
                     "      }\n"
                     "      table tr:nth-child(2n+2) td {\n"
                     "        background-color: #eee;\n"
                     "      }\n"
                     "      td {\n"
                     "        color: #333;\n"
                     "        font-family: Helvetica, Arial, sans-serif;\n"
                     "        font-size: 12px;\n"
                     "        border: 1px solid #D2D2D2;\n"
                     "      }\n"
                     "      </style>\n"
                     "  </head>\n");
   htmlHeader.append('\0');

   return success;
}

//***************************************************************************
// Add Value Fact
//***************************************************************************

int P4d::addValueFact(int addr, const char* type, int factor, const char* name,
                      const char* unit, const char* title, bool active, int maxScale)
{
   if (maxScale == na)
      maxScale = unit[0] == '%' ? 100 : 45;

   tableValueFacts->clear();
   tableValueFacts->setValue("ADDRESS", addr);
   tableValueFacts->setValue("TYPE", type);

   if (!tableValueFacts->find())
   {
      tableValueFacts->setValue("NAME", name);
      tableValueFacts->setValue("STATE", active ? "A" : "D");
      tableValueFacts->setValue("UNIT", unit);
      tableValueFacts->setValue("TITLE", title);
      tableValueFacts->setValue("FACTOR", factor);
      tableValueFacts->setValue("MAXSCALE", maxScale);

      tableValueFacts->store();
      tell(2, "Inserted valuefact %s:%d", type, addr);
      return 1;    // 1 for 'added'
   }
   else
   {
      tableValueFacts->clearChanged();

      tableValueFacts->setValue("NAME", name);
      tableValueFacts->setValue("UNIT", unit);
      tableValueFacts->setValue("TITLE", title);
      tableValueFacts->setValue("FACTOR", factor);

      if (tableValueFacts->getValue("MAXSCALE")->isNull())
         tableValueFacts->setValue("MAXSCALE", maxScale);

      if (tableValueFacts->getChanges())
      {
         tableValueFacts->store();
         tell(2, "Updated valuefact %s:%d", type, addr);
         return 2;  // 2 for 'modified'
      }
   }

   return fail;
}

//***************************************************************************
// Stored Parameters
//***************************************************************************

int P4d::getConfigItem(const char* name, char*& value, const char* def)
{
   free(value);
   value = nullptr;

   tableConfig->clear();
   tableConfig->setValue("OWNER", myName());
   tableConfig->setValue("NAME", name);

   if (tableConfig->find())
      value = strdup(tableConfig->getStrValue("VALUE"));
   else if (def)  // only if not a nullptr
   {
      value = strdup(def);
      setConfigItem(name, value);  // store the default (may be an empty string)
   }

   tableConfig->reset();

   return success;
}

int P4d::setConfigItem(const char* name, const char* value)
{
   tell(eloAlways, "Storing '%s' with value '%s'", name, value);
   tableConfig->clear();
   tableConfig->setValue("OWNER", myName());
   tableConfig->setValue("NAME", name);
   tableConfig->setValue("VALUE", value);

   return tableConfig->store();
}

int P4d::getConfigItem(const char* name, int& value, int def)
{
   char* txt {nullptr};

   getConfigItem(name, txt);

   if (!isEmpty(txt))
      value = atoi(txt);
   else if (isEmpty(txt) && def != na)
   {
      value = def;
      setConfigItem(name, value);
   }
   else
      value = 0;

   free(txt);

   return success;
}

int P4d::getConfigItem(const char* name, double& value, double def)
{
   char* txt {nullptr};

   getConfigItem(name, txt);

   if (!isEmpty(txt))
   {
      std::string s = strReplace(".", ",", txt);
      value = strtod(s.c_str(), nullptr);
   }
   else if (isEmpty(txt) && def != na)
   {
      value = def;
      setConfigItem(name, value);
   }
   else
      value = 0;

   free(txt);

   return success;
}

int P4d::getConfigItem(const char* name, bool& value, bool def)
{
   char* txt {nullptr};

   getConfigItem(name, txt);

   if (!isEmpty(txt))
      value = atoi(txt);
   else if (isEmpty(txt))
   {
      value = def;
      setConfigItem(name, value);
   }

   free(txt);

   return success;
}

int P4d::setConfigItem(const char* name, int value)
{
   char txt[16] = "";

   snprintf(txt, sizeof(txt), "%d", value);

   return setConfigItem(name, txt);
}
