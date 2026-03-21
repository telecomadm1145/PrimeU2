#ifndef WIN32_SYNC_PRIMITIVES_H
#define WIN32_SYNC_PRIMITIVES_H

#ifdef _WIN32

#include "SyncPrimitives.h"

class Win32SyncFactory : public ISyncFactory {
public:
  IEvent *CreateEventObject(bool bManualReset, bool bInitialState) override;
  ISemaphore *CreateSemaphoreObject(int initialCount, int maxCount) override;
  ICriticalSection *CreateCriticalSectionObject() override;
  void SleepMillis(uint32_t ms) override;
  void YieldThread() override;
};

#endif // _WIN32

#endif // WIN32_SYNC_PRIMITIVES_H
