#ifndef CALLBOX_PROCESS_H
#define CALLBOX_PROCESS_H

#include <ptlib.h>
#include <ptlib/pprocess.h>

class CallboxProcess : public PProcess
{
  PCLASSINFO(CallboxProcess, PProcess);
public:
  CallboxProcess();

  virtual void Main() override;
  virtual bool OnInterrupt(bool terminating) override;

private:
  PSyncPoint m_stop;
};

#endif
