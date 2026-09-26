#ifndef CALLBOX_DATABASE_H
#define CALLBOX_DATABASE_H

#include <ptlib.h>

/** One row in the registrations table. */
struct CallboxRegistration
{
  PString protocol;
  PString aor;
  PString contact;
  PString expires;
  PString product;
  PString updatedAt;
};

/** One row in the calls table. */
struct CallboxCallRecord
{
  PString startedAt;
  PString answeredAt;
  PString endedAt;
  PString protocol;
  PString callerUri;
  PString callerAddress;
  PString calleeAlias;
  int localPort;
  PString audioCodec;
  PString videoCodec;
  PString signallingSecurity;
  PString mediaSecurity;
  PString endReason;
  PString mode;
};

/**
 * Storage used by callbox.
 *
 * Subclass this to add a backend. CallboxSQLiteDatabase is the file backend.
 * A PostgreSQL or MySQL backend implements the same virtual functions and is
 * constructed in place of the SQLite class.
 */
class CallboxDatabase : public PObject
{
  PCLASSINFO(CallboxDatabase, PObject);
public:
  virtual ~CallboxDatabase();

  /** Open the database. target is a file path or a server connection string. */
  virtual bool Open(const PString & target) = 0;

  /** Close the database. Safe to call more than once. */
  virtual void Close() = 0;

  virtual bool SaveRegistration(const CallboxRegistration & registration) = 0;
  virtual bool SaveCall(const CallboxCallRecord & call) = 0;
};

#endif
