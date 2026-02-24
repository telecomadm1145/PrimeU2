#ifndef SYNC_PRIMITIVES_H
#define SYNC_PRIMITIVES_H

#include <cstdint>

// Abstract Event
class IEvent {
public:
  virtual ~IEvent() = default;
  virtual void Set() = 0;
  virtual void Reset() = 0;
  virtual bool Wait(
      uint32_t timeoutMillis) = 0; // Returns true if signaled, false on timeout
};

// Abstract Semaphore
class ISemaphore {
public:
  virtual ~ISemaphore() = default;
  virtual bool Wait(
      uint32_t timeoutMillis) = 0; // Returns true if acquired, false on timeout
  virtual void Release(int releaseCount, int *previousCount) = 0;
};

// Abstract Critical Section
class ICriticalSection {
public:
  virtual ~ICriticalSection() = default;
  virtual void Enter() = 0;
  virtual void Leave() = 0;
};

// Abstract Thread Layer (for Sleep, Yield, etc.)
class ISyncFactory {
public:
  virtual ~ISyncFactory() = default;
  virtual IEvent *CreateEventObject(bool bManualReset, bool bInitialState) = 0;
  virtual ISemaphore *CreateSemaphoreObject(int initialCount, int maxCount) = 0;
  virtual ICriticalSection *CreateCriticalSectionObject() = 0;
  virtual void SleepMillis(uint32_t ms) = 0;
  virtual void YieldThread() = 0;
};

// Global instance of the active factory
extern ISyncFactory *g_SyncFactory;

#endif // SYNC_PRIMITIVES_H
