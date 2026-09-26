#include "process.h"

#include "calls.h"
#include "db/sqlite_database.h"

#include <ptclib/pjson.h>
#include <ptlib/pluginmgr.h>
#include <ptlib/tracing.h>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <cstring>
#endif

#include <iostream>

static void Usage(ostream & strm, const PArgList & args)
{
  args.Usage(strm,
    "[options] <config.json>\n"
    "\n"
    "The JSON file supplies the SQLite database path. A relative database path\n"
    "is resolved from the directory that contains the JSON file. Set\n"
    "CALLBOX_DATABASE, or pass --database, to store the database somewhere else.\n"
    "That is how a container keeps the database on a mounted volume while the\n"
    "JSON file is mounted read-only.\n"
    "\n"
    "Each entry in \"numbers\" is an alias this process answers, with its\n"
    "audio codecs, video codecs, SIP port, and H.323 port. mode is \"ivr\"\n"
    "(the default) or \"conference\". An IVR number uses \"vxml\", or a built-in\n"
    "script that stays up when vxml is omitted. features lists \"hold\" and\n"
    "\"transfer\"; anything else is refused.\n"
    "\n"
    "videoImage is a JPEG or BMP file sent as the video stream when a caller\n"
    "reaches an IVR number. A relative path is resolved from the directory that\n"
    "contains the JSON file. Set it at the top of the file, or on one number to\n"
    "override that default. A conference number mixes the participants' video.\n"
    "The process keeps listening until Ctrl-C or SIGTERM.\n"
    "\n"
    "--call-log prints one line when a call is accepted and another when it\n"
    "ends. --call-log-file appends those lines to a file. A relative log path\n"
    "is resolved from the directory that contains the JSON file.\n"
    "\n"
    "-t turns on PTLib and OPAL trace. Repeat it for more detail, or set\n"
    "--trace-level. -o names the trace file. \"stderr\" writes it to the\n"
    "console.\n"
    "\n"
    "Local test:\n"
    "  callbox callbox.json\n"
    "\n"
    "Container:\n"
    "  ./container-build.sh\n"
    "  ./container-run.sh callbox.json ./data\n");
}


CallboxProcess::CallboxProcess()
  : PProcess("Callbox", "callbox")
{
}


void CallboxProcess::Main()
{
  PArgList & args = GetArguments();
  args.Parse(
    "h-help. Print this help.\n"
    "d-database: SQLite database path. Overrides CALLBOX_DATABASE and the JSON file.\n"
    "c-call-log. Log each call when it is accepted and when it ends.\n"
    "C-call-log-file: Append the same call log lines to this file.\n"
    PTRACE_ARGLIST
  );

  PTRACE_INITIALISE(args);

  if (args.HasOption('h') || args.GetCount() != 1) {
    Usage(args.HasOption('h') ? cout : cerr, args);
    if (!args.HasOption('h'))
      SetTerminationValue(1);
    return;
  }

  PFilePath configPath(args[0]);
  PTextFile configFile;
  if (!configFile.Open(configPath, PFile::ReadOnly)) {
    cerr << "Cannot read configuration " << configPath << endl;
    SetTerminationValue(1);
    return;
  }
  PJSON config(configFile.ReadString(P_MAX_INDEX));
  if (!config.IsValid() || !config.IsObject()) {
    cerr << "Configuration is not a JSON object: " << configPath << endl;
    SetTerminationValue(1);
    return;
  }

  const char * envDatabase = getenv("CALLBOX_DATABASE");
  PString database = envDatabase != NULL ? envDatabase : "";
  if (args.HasOption('d'))
    database = args.GetOptionString('d');
  if (database.IsEmpty()) {
    const PJSON::Object & root = config.GetObject();
    if (root.IsType("database", PJSON::e_String))
      database = root.GetString("database");
  }
  if (database.IsEmpty()) {
    cerr << "No database path. Set \"database\" in " << configPath
         << " or pass --database." << endl;
    SetTerminationValue(1);
    return;
  }
  if (!PFilePath::IsAbsolutePath(database))
    database = configPath.GetDirectory() + database;

  PFilePath databasePath(database);
  PDirectory databaseDir(databasePath.GetDirectory());
  if (!databaseDir.Exists() && !databaseDir.Create(PFileInfo::DefaultDirPerms, true)) {
    cerr << "Cannot create database directory " << databaseDir << endl;
    SetTerminationValue(1);
    return;
  }

  CallboxSQLiteDatabase store;
  if (!store.Open(databasePath)) {
    SetTerminationValue(1);
    return;
  }

  // Codec plugins register H.323 capabilities when they load. They sit next
  // to the libopal this binary was linked against.
#if defined(__APPLE__) || defined(__linux__)
  {
    PString (*version)() = &OpalGetVersion;
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((const void *)version, &info) != 0 && info.dli_fname != NULL) {
      PDirectory plugins(PFilePath(info.dli_fname).GetDirectory() + "plugins");
      if (plugins.Exists())
        PPluginManager::GetPluginManager().LoadDirectory(plugins);
    }
  }
#endif

  CallboxCalls calls(store);
  PString callLogFile;
  if (args.HasOption('C'))
    callLogFile = args.GetOptionString('C');
  if (!callLogFile.IsEmpty() && !PFilePath::IsAbsolutePath(callLogFile))
    callLogFile = PString(configPath.GetDirectory()) + callLogFile;
  calls.SetCallLog(args.HasOption('c'), callLogFile);
  if (!calls.OpenCallLog()) {
    SetTerminationValue(1);
    return;
  }
  if (!calls.Start(config.GetObject(), configPath.GetDirectory())) {
    SetTerminationValue(1);
    return;
  }

  cout << "callbox linked with OPAL " << OpalGetVersion() << endl
       << "configuration: " << configPath << endl
       << "database: " << databasePath << endl
       << "Listening. Stop with Ctrl-C or SIGTERM." << endl;

  m_stop.Wait();
  calls.Stop();
  store.Close();
}


bool CallboxProcess::OnInterrupt(bool)
{
  m_stop.Signal();
  return true;
}
