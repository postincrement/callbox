#include "sqlite_database.h"

#include <iostream>

static void BindText(sqlite3_stmt * statement, int index, const PString & value)
{
  if (value.IsEmpty())
    sqlite3_bind_null(statement, index);
  else
    sqlite3_bind_text(statement, index, value.GetPointer(), (int)value.GetLength(), SQLITE_TRANSIENT);
}


CallboxSQLiteDatabase::CallboxSQLiteDatabase()
{
}


CallboxSQLiteDatabase::~CallboxSQLiteDatabase()
{
  Close();
}


bool CallboxSQLiteDatabase::Open(const PString & target)
{
  PWaitAndSignal lock(m_mutex);
  m_db.reset();

  sqlite3 * opened = NULL;
  if (sqlite3_open(target, &opened) != SQLITE_OK) {
    std::cerr << "Cannot open database " << target << ": "
              << (opened != NULL ? sqlite3_errmsg(opened) : "sqlite3_open failed") << std::endl;
    m_db.reset(opened);
    m_db.reset();
    return false;
  }
  m_db.reset(opened);

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
  if (!Exec(schema)) {
    m_db.reset();
    return false;
  }
  return true;
}


void CallboxSQLiteDatabase::Close()
{
  PWaitAndSignal lock(m_mutex);
  m_db.reset();
}


bool CallboxSQLiteDatabase::SaveRegistration(const CallboxRegistration & registration)
{
  PWaitAndSignal lock(m_mutex);
  if (!m_db) {
    std::cerr << "Cannot save a registration: the database is not open." << std::endl;
    return false;
  }

  sqlite3_stmt * statement = NULL;
  if (!Prepare(
        "INSERT INTO registrations"
        " (protocol, aor, contact, expires, product, updated_at)"
        " VALUES (?, ?, ?, ?, ?, ?)",
        statement))
    return false;

  BindText(statement, 1, registration.protocol);
  BindText(statement, 2, registration.aor);
  BindText(statement, 3, registration.contact);
  BindText(statement, 4, registration.expires);
  BindText(statement, 5, registration.product);
  BindText(statement, 6, registration.updatedAt);

  bool ok = sqlite3_step(statement) == SQLITE_DONE;
  if (!ok)
    Report("save registration");
  sqlite3_finalize(statement);
  return ok;
}


bool CallboxSQLiteDatabase::SaveCall(const CallboxCallRecord & call)
{
  PWaitAndSignal lock(m_mutex);
  if (!m_db) {
    std::cerr << "Cannot save a call: the database is not open." << std::endl;
    return false;
  }

  sqlite3_stmt * statement = NULL;
  if (!Prepare(
        "INSERT INTO calls"
        " (started_at, answered_at, ended_at, protocol, caller_uri, caller_address,"
        "  callee_alias, local_port, audio_codec, video_codec, signalling_security,"
        "  media_security, end_reason, mode)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        statement))
    return false;

  BindText(statement, 1, call.startedAt);
  BindText(statement, 2, call.answeredAt);
  BindText(statement, 3, call.endedAt);
  BindText(statement, 4, call.protocol);
  BindText(statement, 5, call.callerUri);
  BindText(statement, 6, call.callerAddress);
  BindText(statement, 7, call.calleeAlias);
  sqlite3_bind_int(statement, 8, call.localPort);
  BindText(statement, 9, call.audioCodec);
  BindText(statement, 10, call.videoCodec);
  BindText(statement, 11, call.signallingSecurity);
  BindText(statement, 12, call.mediaSecurity);
  BindText(statement, 13, call.endReason);
  BindText(statement, 14, call.mode);

  bool ok = sqlite3_step(statement) == SQLITE_DONE;
  if (!ok)
    Report("save call");
  sqlite3_finalize(statement);
  return ok;
}


bool CallboxSQLiteDatabase::Exec(const char * sql)
{
  char * error = NULL;
  int rc = sqlite3_exec(m_db.get(), sql, NULL, NULL, &error);
  if (rc != SQLITE_OK) {
    std::cerr << "Cannot prepare database: "
              << (error != NULL ? error : sqlite3_errmsg(m_db.get())) << std::endl;
    sqlite3_free(error);
    return false;
  }
  return true;
}


bool CallboxSQLiteDatabase::Prepare(const char * sql, sqlite3_stmt * & statement)
{
  if (sqlite3_prepare_v2(m_db.get(), sql, -1, &statement, NULL) == SQLITE_OK)
    return true;
  Report("prepare statement");
  return false;
}


void CallboxSQLiteDatabase::Report(const char * action) const
{
  std::cerr << "Database " << action << " failed: "
            << (m_db ? sqlite3_errmsg(m_db.get()) : "not open") << std::endl;
}
