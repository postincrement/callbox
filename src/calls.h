#ifndef CALLBOX_CALLS_H
#define CALLBOX_CALLS_H

#include <ptlib.h>
#include <ptclib/pjson.h>
#include <opal/manager.h>

#include "db/database.h"

#include <map>
#include <vector>

struct CallboxNumber
{
  PString alias;
  PStringArray audioCodecs;
  PStringArray videoCodecs;
  PString videoImage;
  PStringArray features;
  PString vxml;
  bool conference;
  WORD sipPort;
  WORD h323Port;

  bool HasFeature(const PString & feature) const;
};

/** SIP and H.323 listeners, and the numbers those listeners answer. */
class CallboxCalls : public OpalManager
{
  PCLASSINFO(CallboxCalls, OpalManager);
public:
  explicit CallboxCalls(CallboxDatabase & database);

  /** Read numbers from config and open the SIP and H.323 listeners. */
  bool Start(const PJSON::Object & config, const PDirectory & configDirectory);

  /** Shut the listeners down. */
  void Stop();

  /** Log "call accepted" and "call ended" to standard output, a file, or both. */
  void SetCallLog(bool toStdout, const PString & path);

  /** Open the call log file. Fails when the file cannot be created. */
  bool OpenCallLog();

  const std::vector<CallboxNumber> & GetNumbers() const { return m_numbers; }

  /** True when the number that answered this call lists the feature. */
  bool AllowsFeature(const OpalConnection & connection, const PString & feature) const;

  virtual PBoolean OnIncomingConnection(
    OpalConnection & connection,
    unsigned options,
    OpalConnection::StringOptions * stringOptions
  ) override;

  virtual void OnEstablished(OpalConnection & connection) override;
  virtual void OnClearedCall(OpalCall & call) override;

  virtual void AdjustMediaFormats(
    bool local,
    const OpalConnection & connection,
    OpalMediaFormatList & mediaFormats
  ) const override;

private:
  struct ActiveCall
  {
    size_t index;
    WORD localPort;
    PString startedAt;
    PString answeredAt;
    PString callerUri;
    PString callerAddress;
    PString protocol;
    PString audioCodec;
    PString videoCodec;
  };

  int MatchNumber(OpalConnection & connection) const;
  void RememberCodecs(OpalConnection & connection, ActiveCall & active) const;
  const ActiveCall * FindActive(const OpalConnection & connection) const;
  void WriteCallLog(const char * event,
                    const ActiveCall & active,
                    const CallboxNumber & number,
                    const PString & detail);

  CallboxDatabase & m_database;
  std::vector<CallboxNumber> m_numbers;
  std::map<PString, ActiveCall> m_active;
  mutable PMutex m_callMutex;
  bool m_callLogStdout;
  PString m_callLogPath;
  PTextFile m_callLogFile;
  PMutex m_callLogMutex;
};

#endif
