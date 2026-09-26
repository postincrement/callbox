#ifndef CALLBOX_SQLITE_DATABASE_H
#define CALLBOX_SQLITE_DATABASE_H

#include "database.h"

#include <memory>
#include <sqlite3.h>

struct CallboxSQLiteCloser
{
  void operator()(sqlite3 * db) const { sqlite3_close(db); }
};

/** SQLite implementation of CallboxDatabase. */
class CallboxSQLiteDatabase : public CallboxDatabase
{
  PCLASSINFO(CallboxSQLiteDatabase, CallboxDatabase);
public:
  CallboxSQLiteDatabase();
  virtual ~CallboxSQLiteDatabase();

  virtual bool Open(const PString & target) override;
  virtual void Close() override;
  virtual bool SaveRegistration(const CallboxRegistration & registration) override;
  virtual bool SaveCall(const CallboxCallRecord & call) override;

private:
  bool Exec(const char * sql);
  bool Prepare(const char * sql, sqlite3_stmt * & statement);
  void Report(const char * action) const;

  std::unique_ptr<sqlite3, CallboxSQLiteCloser> m_db;
  PDECLARE_MUTEX(m_mutex);
};

#endif
