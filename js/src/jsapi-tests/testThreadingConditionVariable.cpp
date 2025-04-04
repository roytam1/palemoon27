/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
* vim: set ts=8 sts=4 et sw=4 tw=99:
*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"
#include "threading/ConditionVariable.h"
#include "threading/Mutex.h"

struct TestState {
    js::Mutex mutex;
    js::ConditionVariable condition;
    bool flag;
    PRThread* testThread;

    explicit TestState(bool createThread = true)
      : flag(false), testThread(nullptr)
    {
        if (createThread) {
            testThread = PR_CreateThread(PR_USER_THREAD,
                                         setFlag,
                                         (void *)this,
                                         PR_PRIORITY_NORMAL,
                                         PR_GLOBAL_THREAD,
                                         PR_JOINABLE_THREAD,
                                         0);
            MOZ_RELEASE_ASSERT(testThread);
        }
    }

    static void setFlag(void* arg) {
        auto& state = *static_cast<TestState*>(arg);
        js::UniqueLock<js::Mutex> lock(state.mutex);
        state.flag = true;
        state.condition.notify_one();
    }

    void join() {
        MOZ_RELEASE_ASSERT(testThread != nullptr);
        PR_JoinThread(testThread);
        testThread = nullptr;
    }
};

BEGIN_TEST(testThreadingConditionVariable)
{
    auto state = mozilla::MakeUnique<TestState>();
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        while (!state->flag)
            state->condition.wait(lock);
    }
    state->join();

    CHECK(state->flag);

    return true;
}
END_TEST(testThreadingConditionVariable)

BEGIN_TEST(testThreadingConditionVariablePredicate)
{
    auto state = mozilla::MakeUnique<TestState>();
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        state->condition.wait(lock, [&state]() {return state->flag;});
    }
    state->join();

    CHECK(state->flag);

    return true;
}
END_TEST(testThreadingConditionVariablePredicate)

BEGIN_TEST(testThreadingConditionVariableUntilOkay)
{
    auto state = mozilla::MakeUnique<TestState>();
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        while (!state->flag) {
            auto to = mozilla::TimeStamp::Now() + mozilla::TimeDuration::FromSeconds(600);
            js::CVStatus res = state->condition.wait_until(lock, to);
            CHECK(res == js::CVStatus::NoTimeout);
        }
    }
    state->join();

    CHECK(state->flag);

    return true;
}
END_TEST(testThreadingConditionVariableUntilOkay)

BEGIN_TEST(testThreadingConditionVariableUntilTimeout)
{
    auto state = mozilla::MakeUnique<TestState>(false);
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        while (!state->flag) {
            auto to = mozilla::TimeStamp::Now() + mozilla::TimeDuration::FromMilliseconds(10);
            js::CVStatus res = state->condition.wait_until(lock, to);
            if (res == js::CVStatus::Timeout)
                break;
        }
    }
    CHECK(!state->flag);

    // Timeout in the past should return with timeout immediately.
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        auto to = mozilla::TimeStamp::Now() - mozilla::TimeDuration::FromMilliseconds(10);
        js::CVStatus res = state->condition.wait_until(lock, to);
        CHECK(res == js::CVStatus::Timeout);
    }

    return true;
}
END_TEST(testThreadingConditionVariableUntilTimeout)

BEGIN_TEST(testThreadingConditionVariableUntilOkayPredicate)
{
    auto state = mozilla::MakeUnique<TestState>();
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        auto to = mozilla::TimeStamp::Now() + mozilla::TimeDuration::FromSeconds(600);
        bool res = state->condition.wait_until(lock, to, [&state](){return state->flag;});
        CHECK(res);
    }
    state->join();

    CHECK(state->flag);

    return true;
}
END_TEST(testThreadingConditionVariableUntilOkayPredicate)

BEGIN_TEST(testThreadingConditionVariableUntilTimeoutPredicate)
{
    auto state = mozilla::MakeUnique<TestState>(false);
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        auto to = mozilla::TimeStamp::Now() + mozilla::TimeDuration::FromMilliseconds(10);
        bool res = state->condition.wait_until(lock, to, [&state](){return state->flag;});
        CHECK(!res);
    }
    CHECK(!state->flag);

    return true;
}
END_TEST(testThreadingConditionVariableUntilTimeoutPredicate)

BEGIN_TEST(testThreadingConditionVariableForOkay)
{
    auto state = mozilla::MakeUnique<TestState>();
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        while (!state->flag) {
            auto duration = mozilla::TimeDuration::FromSeconds(600);
            js::CVStatus res = state->condition.wait_for(lock, duration);
            CHECK(res == js::CVStatus::NoTimeout);
        }
    }
    state->join();

    CHECK(state->flag);

    return true;
}
END_TEST(testThreadingConditionVariableForOkay)

BEGIN_TEST(testThreadingConditionVariableForTimeout)
{
    auto state = mozilla::MakeUnique<TestState>(false);
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        while (!state->flag) {
            auto duration = mozilla::TimeDuration::FromMilliseconds(10);
            js::CVStatus res = state->condition.wait_for(lock, duration);
            if (res == js::CVStatus::Timeout)
                break;
        }
    }
    CHECK(!state->flag);

    // Timeout in the past should return with timeout immediately.
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        auto duration = mozilla::TimeDuration::FromMilliseconds(-10);
        js::CVStatus res = state->condition.wait_for(lock, duration);
        CHECK(res == js::CVStatus::Timeout);
    }

    return true;
}
END_TEST(testThreadingConditionVariableForTimeout)

BEGIN_TEST(testThreadingConditionVariableForOkayPredicate)
{
    auto state = mozilla::MakeUnique<TestState>();
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        auto duration = mozilla::TimeDuration::FromSeconds(600);
        bool res = state->condition.wait_for(lock, duration, [&state](){return state->flag;});
        CHECK(res);
    }
    state->join();

    CHECK(state->flag);

    return true;
}
END_TEST(testThreadingConditionVariableForOkayPredicate)

BEGIN_TEST(testThreadingConditionVariableForTimeoutPredicate)
{
    auto state = mozilla::MakeUnique<TestState>(false);
    {
        js::UniqueLock<js::Mutex> lock(state->mutex);
        auto duration = mozilla::TimeDuration::FromMilliseconds(10);
        bool res = state->condition.wait_for(lock, duration, [&state](){return state->flag;});
        CHECK(!res);
    }
    CHECK(!state->flag);

    return true;
}
END_TEST(testThreadingConditionVariableForTimeoutPredicate)
