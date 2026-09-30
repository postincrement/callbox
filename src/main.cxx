#include "process.h"

// PServiceProcess redefines PCREATE_PROCESS as WinMain. Callbox stays a
// console program so a foreground run can print to the terminal, and the
// same entry point still reaches InternalMain for a Windows service.
int main(int argc, char * argv[])
{
  CallboxProcess * process = new CallboxProcess();
  process->PreInitialise(argc, argv);
#if defined(_WIN32)
  int code = process->InternalMain(GetModuleHandleA(NULL));
#else
  int code = process->InternalMain(NULL);
#endif
  delete process;
  return code;
}
