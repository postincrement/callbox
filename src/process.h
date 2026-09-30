#ifndef CALLBOX_PROCESS_H
#define CALLBOX_PROCESS_H

#include <ptlib.h>
#include <ptlib/svcproc.h>

class CallboxCalls;
class CallboxSQLiteDatabase;

class CallboxProcess : public PServiceProcess
{
  PCLASSINFO(CallboxProcess, PServiceProcess);
public:
  CallboxProcess();
  ~CallboxProcess();

  virtual int InternalMain(void * arg) override;
  virtual PBoolean OnStart() override;
  virtual void Main() override;
  virtual void OnStop() override;
  virtual bool OnInterrupt(bool terminating) override;

private:
  bool CollectArguments();
  void ApplyTrace() const;
  void Shutdown();

  PString m_databaseOverride;
  bool m_callLog;
  PString m_callLogFile;
  unsigned m_traceCount;
  unsigned m_traceLevel;
  PString m_traceFile;
  PString m_savedConfig;

  CallboxSQLiteDatabase * m_store;
  CallboxCalls * m_calls;
  bool m_shutDown;
  bool m_argumentsReady;
  bool m_inServiceMain;
};

#endif
