#include "process.h"

#include "calls.h"
#include "db/sqlite_database.h"

#include <opal/mediafmt.h>

#include <ptclib/pjson.h>
#include <ptlib/pluginmgr.h>
#include <ptlib/tracing.h>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <cstring>
#endif

#include <iostream>

static void WriteCodecList(ostream & strm, const char * heading, const OpalMediaType & type)
{
  OpalMediaFormatList formats = OpalMediaFormat::GetAllRegisteredMediaFormats();
  PStringArray names;
  for (OpalMediaFormatList::iterator format = formats.begin(); format != formats.end(); ++format) {
    if (format->IsTransportable() && format->IsMediaType(type))
      names.AppendString(format->GetName());
  }

  strm << heading << ':' << endl;
  if (names.IsEmpty()) {
    strm << "  none" << endl;
    return;
  }
  for (PINDEX i = 0; i < names.GetSize(); ++i)
    strm << "  " << names[i] << endl;
}


static void Usage(ostream & strm)
{
  strm <<
    "Usage: callbox [service options] [options] <config.json>\n"
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
    "\n"
    "Giving only the JSON file runs in the foreground. On Unix, -d runs it as a\n"
    "daemon and -t asks the daemon in the pid file to stop. On Windows,\n"
    "\"install\" registers a service (the JSON path is remembered), \"start\" and\n"
    "\"stop\" control it, and \"foreground\" runs it in a console.\n"
    "\n"
    "Unix service options:\n"
    "  -x, --execute       run in the foreground (used when only a JSON file is given)\n"
    "  -d, --daemon        run as a daemon\n"
    "  -c, --console       write service messages to the console instead of syslog\n"
    "  -p, --pid-file      pid file or directory (default /var/run)\n"
    "  -s, --status        report whether the daemon in the pid file is running\n"
    "  -t, --terminate     stop the daemon in the pid file\n"
    "  -k, --kill          kill the daemon if it does not stop\n"
    "  -l, --log-file      write service messages to a file instead of syslog\n"
    "  -v, --version       print version information and exit\n"
    "\n"
    "Windows service commands, as the first argument:\n"
    "  foreground          run in this console\n"
    "  install             register the service, remembering the JSON file\n"
    "  remove              unregister the service\n"
    "  start               start the service\n"
    "  stop                stop the service\n"
    "\n"
    "--call-log prints one line when a call is accepted and another when it\n"
    "ends. --call-log-file appends those lines to a file. A relative log path\n"
    "is resolved from the directory that contains the JSON file.\n"
    "\n"
    "--trace turns on PTLib and OPAL trace. Repeat it for more detail, or set\n"
    "--trace-level. -o names the trace file. \"stderr\" writes it to the\n"
    "console.\n"
    "\n"
    "Local test:\n"
    "  callbox callbox.json\n"
    "\n"
    "Daemon:\n"
    "  callbox -d -p /tmp callbox.json\n"
    "  callbox -t -p /tmp\n"
    "\n"
    "Container:\n"
    "  ./container-build.sh\n"
    "  ./container-run.sh callbox.json ./data\n";
}


static bool IsUnixModeOption(const PString & arg)
{
  return arg == "-d" || arg == "--daemon" ||
         arg == "-x" || arg == "--execute" ||
         arg == "-h" || arg == "--help" ||
         arg == "-v" || arg == "--version" ||
         arg == "-s" || arg == "--status" ||
         arg == "-t" || arg == "--terminate" ||
         arg == "-k" || arg == "--kill" ||
         arg == "-U" || arg == "--trace-up" ||
         arg == "-D" || arg == "--trace-down";
}


#if defined(_WIN32)
static bool IsWindowsRunMode(const PString & arg)
{
  return arg *= "debug" || arg *= "foreground" || arg *= "debughidden";
}


static bool IsWindowsManagementCommand(const PString & arg)
{
  return arg *= "default" || arg *= "tray" || arg *= "notray" || arg *= "version" ||
         arg *= "install" || arg *= "register" || arg *= "remove" || arg *= "start" ||
         arg *= "stop" || arg *= "pause" || arg *= "resume" || arg *= "deinstall" ||
         arg *= "autorestart" || arg *= "norestart" || arg *= "nowin" || arg *= "help";
}
#endif


static bool TakeOptionValue(const PStringArray & args, PINDEX & index, const PString & arg, PString & value)
{
  PINDEX equals = arg.Find('=');
  if (arg.Left(2) == "--" && equals != P_MAX_INDEX) {
    value = arg.Mid(equals + 1);
    return true;
  }
  if (index + 1 >= args.GetSize())
    return false;
  value = args[++index];
  return true;
}


CallboxProcess::CallboxProcess()
  : PServiceProcess("Callbox", "callbox", 0, 1, PProcess::AlphaCode, 0)
  , m_callLog(false)
  , m_traceCount(0)
  , m_traceLevel(0)
  , m_store(NULL)
  , m_calls(NULL)
  , m_shutDown(false)
  , m_argumentsReady(false)
  , m_inServiceMain(false)
{
#if defined(_WIN32)
  SetDescription("OPAL callbox");
#endif
}


CallboxProcess::~CallboxProcess()
{
  Shutdown();
}


bool CallboxProcess::CollectArguments()
{
  PArgList & args = GetArguments();
  PStringArray original = args.GetParameters();
  PStringArray kept;
  bool help = false;
  bool mode = false;
  bool positional = false;
#if defined(_WIN32)
  bool management = false;
#endif

  for (PINDEX i = 0; i < original.GetSize(); ++i) {
    const PString & arg = original[i];
    PString value;

    if (arg == "--help" || arg == "-h") {
      help = true;
      continue;
    }
    if (arg == "--database" || arg.Left(11) == "--database=") {
      if (!TakeOptionValue(original, i, arg, value)) {
        cerr << "Missing value for --database" << endl;
        SetTerminationValue(1);
        return false;
      }
      m_databaseOverride = value;
      continue;
    }
    if (arg == "--call-log") {
      m_callLog = true;
      continue;
    }
    if (arg == "--call-log-file" || arg.Left(16) == "--call-log-file=") {
      if (!TakeOptionValue(original, i, arg, value)) {
        cerr << "Missing value for --call-log-file" << endl;
        SetTerminationValue(1);
        return false;
      }
      m_callLogFile = value;
      continue;
    }
    if (arg == "--trace") {
      ++m_traceCount;
      continue;
    }
    if (arg == "--trace-level" || arg.Left(14) == "--trace-level=") {
      if (!TakeOptionValue(original, i, arg, value)) {
        cerr << "Missing value for --trace-level" << endl;
        SetTerminationValue(1);
        return false;
      }
      m_traceLevel = value.AsUnsigned();
      continue;
    }
    if (arg == "--output" || arg == "-o" || arg.Left(9) == "--output=" ||
        (arg.Left(2) == "-o" && arg.GetLength() > 2 && arg[1] == 'o')) {
      if (arg.Left(2) == "-o" && arg.GetLength() > 2 && arg[1] == 'o' && arg.Left(2) != "--")
        m_traceFile = arg.Mid(2);
      else if (!TakeOptionValue(original, i, arg, m_traceFile)) {
        cerr << "Missing value for --output" << endl;
        SetTerminationValue(1);
        return false;
      }
      continue;
    }

    if (IsUnixModeOption(arg))
      mode = true;

    if (arg.Left(1) != "-") {
      positional = true;
#if defined(_WIN32)
      if (IsWindowsRunMode(arg)) {
        mode = true;
        kept.AppendString(arg);
        continue;
      }
      if (IsWindowsManagementCommand(arg)) {
        mode = true;
        management = true;
        kept.AppendString(arg);
        continue;
      }
      if (m_savedConfig.IsEmpty())
        m_savedConfig = arg;
      if (management)
        continue;
#endif
      if (m_savedConfig.IsEmpty())
        m_savedConfig = arg;
    }

    kept.AppendString(arg);
  }

  if (help) {
    Usage(cout);
    SetTerminationValue(0);
    return false;
  }

#if defined(_WIN32)
  if (management && !m_savedConfig.IsEmpty()) {
    if (!PFilePath::IsAbsolutePath(m_savedConfig))
      m_savedConfig = PString(PDirectory()) + m_savedConfig;
    PConfig cfg("Callbox");
    cfg.SetString("Configuration", m_savedConfig);
  }
#endif

  PStringArray finalArgs;
  if (!mode && positional) {
#if defined(_WIN32)
    finalArgs.AppendString("foreground");
#else
    finalArgs.AppendString("-x");
    finalArgs.AppendString("-c");
#endif
  }
  for (PINDEX i = 0; i < kept.GetSize(); ++i)
    finalArgs.AppendString(kept[i]);
  args.SetArgs(finalArgs);
  return true;
}


int CallboxProcess::InternalMain(void * arg)
{
  // macOS SDL keeps the original thread in its event loop and calls
  // InternalMain again on a worker. Service setup and OnStart belong to the
  // first call. The worker only runs Main().
  if (m_inServiceMain)
    return PProcess::InternalMain(arg);

  if (!m_argumentsReady) {
    if (!CollectArguments())
      return GetTerminationValue();
    m_argumentsReady = true;
  }

  m_inServiceMain = true;
  return PServiceProcess::InternalMain(arg);
}


void CallboxProcess::ApplyTrace() const
{
  if (m_traceCount == 0 && m_traceLevel == 0 && m_traceFile.IsEmpty())
    return;

  PStringArray traceArgs;
  for (unsigned i = 0; i < m_traceCount; ++i)
    traceArgs.AppendString("--trace");
  if (m_traceLevel > 0) {
    traceArgs.AppendString("--trace-level");
    traceArgs.AppendString(PString(PString::Unsigned, m_traceLevel));
  }
  if (!m_traceFile.IsEmpty()) {
    traceArgs.AppendString("--output");
    traceArgs.AppendString(m_traceFile);
  }

  PArgList traceList;
  traceList.SetArgs(traceArgs);
  traceList.Parse(PTRACE_ARGLIST, false);
  PTRACE_INITIALISE(traceList);
}


PBoolean CallboxProcess::OnStart()
{
  ApplyTrace();

  PArgList & args = GetArguments();
  if (args.GetCount() > 1) {
    Usage(cerr);
    return false;
  }

  PString configPath = args.GetCount() == 1 ? args[0] : m_savedConfig;
#if defined(_WIN32)
  if (configPath.IsEmpty())
    configPath = PConfig("Callbox").GetString("Configuration");
#endif
  if (configPath.IsEmpty()) {
    cerr << "No configuration file. Pass the JSON file, or install the service with one." << endl;
    Usage(cerr);
    return false;
  }

  PFilePath configFilePath(configPath);
  PTextFile configFile;
  if (!configFile.Open(configFilePath, PFile::ReadOnly)) {
    cerr << "Cannot read configuration " << configFilePath << endl;
    return false;
  }
  PJSON config(configFile.ReadString(P_MAX_INDEX));
  if (!config.IsValid() || !config.IsObject()) {
    cerr << "Configuration is not a JSON object: " << configFilePath << endl;
    return false;
  }

  const char * envDatabase = getenv("CALLBOX_DATABASE");
  PString database = envDatabase != NULL ? envDatabase : "";
  if (!m_databaseOverride.IsEmpty())
    database = m_databaseOverride;
  if (database.IsEmpty()) {
    const PJSON::Object & root = config.GetObject();
    if (root.IsType("database", PJSON::e_String))
      database = root.GetString("database");
  }
  if (database.IsEmpty()) {
    cerr << "No database path. Set \"database\" in " << configFilePath
         << " or pass --database." << endl;
    return false;
  }
  if (!PFilePath::IsAbsolutePath(database))
    database = configFilePath.GetDirectory() + database;

  PFilePath databasePath(database);
  PDirectory databaseDir(databasePath.GetDirectory());
  if (!databaseDir.Exists() && !databaseDir.Create(PFileInfo::DefaultDirPerms, true)) {
    cerr << "Cannot create database directory " << databaseDir << endl;
    return false;
  }

  m_store = new CallboxSQLiteDatabase;
  if (!m_store->Open(databasePath)) {
    Shutdown();
    return false;
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

  m_calls = new CallboxCalls(*m_store);
  PString callLogFile = m_callLogFile;
  if (!callLogFile.IsEmpty() && !PFilePath::IsAbsolutePath(callLogFile))
    callLogFile = PString(configFilePath.GetDirectory()) + callLogFile;
  m_calls->SetCallLog(m_callLog, callLogFile);
  if (!m_calls->OpenCallLog() || !m_calls->Start(config.GetObject(), configFilePath.GetDirectory())) {
    Shutdown();
    return false;
  }

  cout << "callbox linked with OPAL " << OpalGetVersion() << endl
       << "configuration: " << configFilePath << endl
       << "database: " << databasePath << endl;
  WriteCodecList(cout, "Audio codecs", OpalMediaType::Audio());
#if OPAL_VIDEO
  WriteCodecList(cout, "Video codecs", OpalMediaType::Video());
#else
  cout << "Video codecs:" << endl << "  none" << endl;
#endif
  cout << "Listening." << endl;
  PSYSTEMLOG(StdError, "Listening on configuration " << configFilePath);

  return true;
}


void CallboxProcess::Main()
{
  PServiceProcess::Main();
  Shutdown();
}


void CallboxProcess::OnStop()
{
  PServiceProcess::OnStop();
}


bool CallboxProcess::OnInterrupt(bool)
{
  OnStop();
  return true;
}


void CallboxProcess::Shutdown()
{
  if (m_shutDown)
    return;
  m_shutDown = true;

  if (m_calls != NULL)
    m_calls->Stop();
  delete m_calls;
  m_calls = NULL;

  if (m_store != NULL)
    m_store->Close();
  delete m_store;
  m_store = NULL;
}
