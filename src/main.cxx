#include <ptlib.h>
#include <ptclib/pjson.h>
#include <opal/manager.h>

#include <sqlite3.h>

class CallboxProcess : public PProcess
{
  PCLASSINFO(CallboxProcess, PProcess);
public:
  CallboxProcess()
    : PProcess("Callbox", "callbox")
  {
  }

  virtual void Main() override;

private:
  bool OpenDatabase(const PFilePath & path);
};


PCREATE_PROCESS(CallboxProcess);


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
    "Local test:\n"
    "  callbox callbox.json\n"
    "\n"
    "Container:\n"
    "  ./container-build.sh\n"
    "  ./container-run.sh callbox.json ./data\n");
}


void CallboxProcess::Main()
{
  PArgList & args = GetArguments();
  args.Parse(
    "h-help."
    "d-database:"
  );

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
  if (!OpenDatabase(databasePath)) {
    SetTerminationValue(1);
    return;
  }

  cout << "callbox linked with OPAL " << OpalGetVersion() << endl
       << "protocols:";
#if OPAL_H323
  cout << " H.323";
#endif
#if OPAL_SIP
  cout << " SIP";
#endif
#if !OPAL_H323 && !OPAL_SIP
  cout << " none";
#endif
  cout << endl
       << "configuration: " << configPath << endl
       << "database: " << databasePath << endl;
}


bool CallboxProcess::OpenDatabase(const PFilePath & path)
{
  sqlite3 * db = NULL;
  if (sqlite3_open(path, &db) != SQLITE_OK) {
    cerr << "Cannot open database " << path << ": "
         << (db != NULL ? sqlite3_errmsg(db) : "sqlite3_open failed") << endl;
    sqlite3_close(db);
    return false;
  }

  char * error = NULL;
  const char * schema =
    "CREATE TABLE IF NOT EXISTS registrations ("
    "  id INTEGER PRIMARY KEY,"
    "  protocol TEXT NOT NULL,"
    "  aor TEXT NOT NULL,"
    "  contact TEXT,"
    "  expires TEXT,"
    "  product TEXT,"
    "  updated_at TEXT NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS calls ("
    "  id INTEGER PRIMARY KEY,"
    "  started_at TEXT NOT NULL,"
    "  answered_at TEXT,"
    "  ended_at TEXT,"
    "  protocol TEXT,"
    "  caller_uri TEXT,"
    "  caller_address TEXT,"
    "  callee_alias TEXT,"
    "  local_port INTEGER,"
    "  audio_codec TEXT,"
    "  video_codec TEXT,"
    "  signalling_security TEXT,"
    "  media_security TEXT,"
    "  end_reason TEXT,"
    "  mode TEXT"
    ");";
  int rc = sqlite3_exec(db, schema, NULL, NULL, &error);
  if (rc != SQLITE_OK) {
    cerr << "Cannot prepare database " << path << ": "
         << (error != NULL ? error : sqlite3_errmsg(db)) << endl;
    sqlite3_free(error);
    sqlite3_close(db);
    return false;
  }

  sqlite3_close(db);
  return true;
}
