/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EventTokenBucket.h"

#include "nsICancelable.h"
#include "nsNetUtil.h"
#include "nsSocketTransportService2.h"

#ifdef DEBUG
#include "MainThreadUtils.h"
#endif

#ifdef XP_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

extern PRThread *gSocketThread;

namespace mozilla {
namespace net {

////////////////////////////////////////////
// EventTokenBucketCancelable
////////////////////////////////////////////

class TokenBucketCancelable : public nsICancelable
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICANCELABLE

  TokenBucketCancelable(class ATokenBucketEvent *event);
  virtual ~TokenBucketCancelable() {}
  void Fire();

private:
  friend class EventTokenBucket;
  ATokenBucketEvent *mEvent;
};

NS_IMPL_ISUPPORTS(TokenBucketCancelable, nsICancelable)

TokenBucketCancelable::TokenBucketCancelable(ATokenBucketEvent *event)
  : mEvent(event)
{
}

NS_IMETHODIMP
TokenBucketCancelable::Cancel(nsresult reason)
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  mEvent = nullptr;
  return NS_OK;
}

void
TokenBucketCancelable::Fire()
{
  if (!mEvent)
    return;

  ATokenBucketEvent *event = mEvent;
  mEvent = nullptr;
  event->OnTokenBucketAdmitted();
}

////////////////////////////////////////////
// EventTokenBucket
////////////////////////////////////////////

NS_IMPL_ISUPPORTS(EventTokenBucket, nsITimerCallback)

// by default 1hz with no burst
EventTokenBucket::EventTokenBucket(uint32_t eventsPerSecond,
                                   uint32_t burstSize)
  : mUnitCost(kUsecPerSec)
  , mMaxCredit(kUsecPerSec)
  , mCredit(kUsecPerSec)
  , mPaused(false)
  , mStopped(false)
  , mTimerArmed(false)
#ifdef XP_WIN
  , mFineGrainTimerInUse(false)
  , mFineGrainResetTimerArmed(false)
#endif
{
  MOZ_COUNT_CTOR(EventTokenBucket);
  mLastUpdate = TimeStamp::Now();

  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;
  nsCOMPtr<nsIEventTarget> sts;
  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  if (NS_SUCCEEDED(rv))
    sts = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv))
    mTimer = do_CreateInstance("@mozilla.org/timer;1");
  if (mTimer)
    mTimer->SetTarget(sts);
  SetRate(eventsPerSecond, burstSize);
}

EventTokenBucket::~EventTokenBucket()
{
  SOCKET_LOG(("EventTokenBucket::dtor %p events=%d\n",
              this, mEvents.GetSize()));

  MOZ_COUNT_DTOR(EventTokenBucket);
  if (mTimer && mTimerArmed)
    mTimer->Cancel();

#ifdef XP_WIN
  NormalTimers();
  if (mFineGrainResetTimerArmed) {
    mFineGrainResetTimerArmed = false;
    mFineGrainResetTimer->Cancel();
  }
#endif

  // Complete any queued events to prevent hangs
  while (mEvents.GetSize()) {
    nsRefPtr<TokenBucketCancelable> cancelable = 
      dont_AddRef(static_cast<TokenBucketCancelable *>(mEvents.PopFront()));
    cancelable->Fire();
  }
}

void
EventTokenBucket::SetRate(uint32_t eventsPerSecond,
                          uint32_t burstSize)
{
  SOCKET_LOG(("EventTokenBucket::SetRate %p %u %u\n",
              this, eventsPerSecond, burstSize));

  if (eventsPerSecond > kMaxHz) {
    eventsPerSecond = kMaxHz;
    SOCKET_LOG(("  eventsPerSecond out of range\n"));
  }

  if (!eventsPerSecond) {
    eventsPerSecond = 1;
    SOCKET_LOG(("  eventsPerSecond out of range\n"));
  }

  mUnitCost = kUsecPerSec / eventsPerSecond;
  mMaxCredit = mUnitCost * burstSize;
  if (mMaxCredit > kUsecPerSec * 60 * 15) {
    SOCKET_LOG(("  burstSize out of range\n"));
    mMaxCredit = kUsecPerSec * 60 * 15;
  }
  mCredit = mMaxCredit;
  mLastUpdate = TimeStamp::Now();
}

void
EventTokenBucket::ClearCredits()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  SOCKET_LOG(("EventTokenBucket::ClearCredits %p\n", this));
  mCredit = 0;
}

uint32_t
EventTokenBucket::BurstEventsAvailable()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  return static_cast<uint32_t>(mCredit / mUnitCost);
}

uint32_t
EventTokenBucket::QueuedEvents()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  return mEvents.GetSize();
}

void
EventTokenBucket::Pause()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  SOCKET_LOG(("EventTokenBucket::Pause %p\n", this));
  if (mPaused || mStopped)
    return;

  mPaused = true;
  if (mTimerArmed) {
    mTimer->Cancel();
    mTimerArmed = false;
  }
}

void
EventTokenBucket::UnPause()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  SOCKET_LOG(("EventTokenBucket::UnPause %p\n", this));
  if (!mPaused || mStopped)
    return;

  mPaused = false;
  DispatchEvents();
  UpdateTimer();
}

nsresult
EventTokenBucket::SubmitEvent(ATokenBucketEvent *event, nsICancelable **cancelable)
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  SOCKET_LOG(("EventTokenBucket::SubmitEvent %p\n", this));

  if (mStopped || !mTimer)
    return NS_ERROR_FAILURE;

  UpdateCredits();

  nsRefPtr<TokenBucketCancelable> cancelEvent = new TokenBucketCancelable(event);
  // When this function exits the cancelEvent needs 2 references, one for the
  // mEvents queue and one for the caller of SubmitEvent()

  NS_ADDREF(*cancelable = cancelEvent.get());

  if (mPaused || !TryImmediateDispatch(cancelEvent.get())) {
    // queue it
    SOCKET_LOG(("   queued\n"));
    mEvents.Push(cancelEvent.forget().take());
    UpdateTimer();
  }
  else {
    SOCKET_LOG(("   dispatched synchronously\n"));
  }

  return NS_OK;
}

bool
EventTokenBucket::TryImmediateDispatch(TokenBucketCancelable *cancelable)
{
  if (mCredit < mUnitCost)
    return false;

  mCredit -= mUnitCost;
  cancelable->Fire();
  return true;
}

void
EventTokenBucket::DispatchEvents()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  SOCKET_LOG(("EventTokenBucket::DispatchEvents %p %d\n", this, mPaused));
  if (mPaused || mStopped)
    return;

  while (mEvents.GetSize() && mUnitCost <= mCredit) {
    nsRefPtr<TokenBucketCancelable> cancelable = 
      dont_AddRef(static_cast<TokenBucketCancelable *>(mEvents.PopFront()));
    if (cancelable->mEvent) {
      SOCKET_LOG(("EventTokenBucket::DispachEvents [%p] "
                  "Dispatching queue token bucket event cost=%lu credit=%lu\n",
                  this, mUnitCost, mCredit));
      mCredit -= mUnitCost;
      cancelable->Fire();
    }
  }
  
#ifdef XP_WIN
  if (!mEvents.GetSize())
    WantNormalTimers();
#endif
}
 
void
EventTokenBucket::UpdateTimer()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);
  if (mTimerArmed || mPaused || mStopped || !mEvents.GetSize() || !mTimer)
    return;

  if (mCredit >= mUnitCost)
    return;

  // determine the time needed to wait to accumulate enough credits to admit
  // one more event and set the timer for that point. Always round it
  // up because firing early doesn't help.
  //
  uint64_t deficit = mUnitCost - mCredit;
  uint64_t msecWait = (deficit + (kUsecPerMsec - 1)) / kUsecPerMsec;

  if (msecWait < 4) // minimum wait
    msecWait = 4;
  else if (msecWait > 60000) // maximum wait
    msecWait = 60000;

#ifdef XP_WIN
  FineGrainTimers();
#endif

  SOCKET_LOG(("EventTokenBucket::UpdateTimer %p for %dms\n",
              this, msecWait));
  nsresult rv = mTimer->InitWithCallback(this, static_cast<uint32_t>(msecWait),
                                         nsITimer::TYPE_ONE_SHOT);
  mTimerArmed = NS_SUCCEEDED(rv);
}

NS_IMETHODIMP
EventTokenBucket::Notify(nsITimer *timer)
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);

#ifdef XP_WIN
  if (timer == mFineGrainResetTimer) {
    FineGrainResetTimerNotify();
    return NS_OK;
  }
#endif

  SOCKET_LOG(("EventTokenBucket::Notify() %p\n", this));
  mTimerArmed = false;
  if (mStopped)
    return NS_OK;

  UpdateCredits();
  DispatchEvents();
  UpdateTimer();

  return NS_OK;
}

void
EventTokenBucket::UpdateCredits()
{
  MOZ_ASSERT(PR_GetCurrentThread() == gSocketThread);

  TimeStamp now = TimeStamp::Now();
  TimeDuration elapsed = now - mLastUpdate;
  mLastUpdate = now;

  mCredit += static_cast<uint64_t>(elapsed.ToMicroseconds());
  if (mCredit > mMaxCredit)
    mCredit = mMaxCredit;
  SOCKET_LOG(("EventTokenBucket::UpdateCredits %p to %lu (%lu each.. %3.2f)\n",
              this, mCredit, mUnitCost, (double)mCredit / mUnitCost));
}

#ifdef XP_WIN
void
EventTokenBucket::FineGrainTimers()
{
  SOCKET_LOG(("EventTokenBucket::FineGrainTimers %p mFineGrainTimerInUse=%d\n",
              this, mFineGrainTimerInUse));

  mLastFineGrainTimerUse = TimeStamp::Now();

  if (mFineGrainTimerInUse)
    return;

  if (mUnitCost > kCostFineGrainThreshold)
    return;

  SOCKET_LOG(("EventTokenBucket::FineGrainTimers %p timeBeginPeriod()\n",
              this));

  mFineGrainTimerInUse = true;
  timeBeginPeriod(1);
}

void
EventTokenBucket::NormalTimers()
{
  if (!mFineGrainTimerInUse)
    return;
  mFineGrainTimerInUse = false;

  SOCKET_LOG(("EventTokenBucket::NormalTimers %p timeEndPeriod()\n", this));
  timeEndPeriod(1);
}

void
EventTokenBucket::WantNormalTimers()
{
    if (!mFineGrainTimerInUse)
      return;
    if (mFineGrainResetTimerArmed)
      return;

    TimeDuration elapsed(TimeStamp::Now() - mLastFineGrainTimerUse);
    static const TimeDuration fiveSeconds = TimeDuration::FromSeconds(5);

    if (elapsed >= fiveSeconds) {
      NormalTimers();
      return;
    }
    
    if (!mFineGrainResetTimer)
      mFineGrainResetTimer = do_CreateInstance("@mozilla.org/timer;1");

    // if we can't delay the reset, just do it now
    if (!mFineGrainResetTimer) {
      NormalTimers();
      return;
    }

    // pad the callback out 100ms to avoid having to round trip this again if the
    // timer calls back just a tad early.
    SOCKET_LOG(("EventTokenBucket::WantNormalTimers %p "
                "Will reset timer granularity after delay", this));

    mFineGrainResetTimer->InitWithCallback(
      this,
      static_cast<uint32_t>((fiveSeconds - elapsed).ToMilliseconds()) + 100,
      nsITimer::TYPE_ONE_SHOT);
    mFineGrainResetTimerArmed = true;
}

void
EventTokenBucket::FineGrainResetTimerNotify()
{
  SOCKET_LOG(("EventTokenBucket::FineGrainResetTimerNotify() events = %d\n",
              this, mEvents.GetSize()));
  mFineGrainResetTimerArmed = false;

  // If we are currently processing events then wait for the queue to drain
  // before trying to reset back to normal timers again
  if (!mEvents.GetSize())
    WantNormalTimers();
}

#endif

} // mozilla::net
} // mozilla