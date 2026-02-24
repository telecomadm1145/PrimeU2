// PrimU.cpp : Defines the entry point for the console application.
//

#include "Win32SyncPrimitives.h"
#include "executable.h"
#include "executor.h"
#include "stdafx.h"


ISyncFactory *g_SyncFactory = new Win32SyncFactory();

int main(int argc, char **argv) {
  // if (argc != 2)
  //{
  //     printf("Usage: %s armfir.elf\n", argv[0]);
  //     return 1;
  // }

  Executable exec((char *)"prime_data\\A\\programs\\misc\\armfir.elf");

  if (exec.get_state() == EXEC_LOAD_FAILED) {
    printf("Failed to load executable");
    return 1;
  }

  if (!sExecutor->Initialize(&exec)) {
    printf("Initializing VM failed. Returned:%i", sExecutor->GetLastError());
    return 1;
  }

  sExecutor->Execute();
  // Executor loops infinitely inside Execute() now.
  sExecutor->Cleanup();

  return 0;
}
