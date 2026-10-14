#include "native/NativeAotGlibcSupport.hpp"
#include <condition_variable>
#include <atomic>
#include <cerrno>
#include <map>
#include <memory>
#include <mutex>
#include <cstring>
#include <pthread.h>
#include <shared_mutex>

namespace {

using namespace NativeAotGlibcSupport;

std::mutex tableLock;
std::map<std::uint64_t, std::unique_ptr<pthread_mutex_t>> mutexes;
std::map<std::uint64_t, std::unique_ptr<pthread_rwlock_t>> rwlocks;
std::map<std::uint64_t, std::unique_ptr<pthread_cond_t>> conditions;

template<class Object, class Create>
Object* Find(std::map<std::uint64_t, std::unique_ptr<Object>>& table, std::uint64_t address, Create create) {
    std::lock_guard<std::mutex> lock(tableLock);
    auto& slot = table[address];
    if (!slot) {
        slot = std::make_unique<Object>();
        create(slot.get());
    }
    return slot.get();
}

pthread_mutex_t* Mutex(std::uint64_t address) {
    return Find(mutexes, address, [](pthread_mutex_t* mutex) { pthread_mutex_init(mutex, nullptr); });
}

pthread_rwlock_t* Rwlock(std::uint64_t address) {
    return Find(rwlocks, address, [](pthread_rwlock_t* rwlock) { pthread_rwlock_init(rwlock, nullptr); });
}

pthread_cond_t* Condition(std::uint64_t address) {
    return Find(conditions, address, [](pthread_cond_t* condition) { pthread_cond_init(condition, nullptr); });
}

void RequireDefaultMutex(std::uint64_t address) {
    std::int32_t kind;
    std::memcpy(&kind, reinterpret_cast<const void*>(address + 16), sizeof(kind));
    if (kind != 0) Unsupported("only default glibc mutexes are supported");
}

void ReturnResult(State& state, int result) {
    ReturnInteger(state, result == 0 ? 0 : GuestErrno(result));
}

void MutexLock(State& state) {
    const auto address = NativeAotGuest::Argument(state, 0);
    RequireDefaultMutex(address);
    ReturnResult(state, pthread_mutex_lock(Mutex(address)));
}

void MutexUnlock(State& state) {
    ReturnResult(state, pthread_mutex_unlock(Mutex(NativeAotGuest::Argument(state, 0))));
}

void ReadLock(State& state) {
    ReturnResult(state, pthread_rwlock_rdlock(Rwlock(NativeAotGuest::Argument(state, 0))));
}

void WriteLock(State& state) {
    ReturnResult(state, pthread_rwlock_wrlock(Rwlock(NativeAotGuest::Argument(state, 0))));
}

void RwUnlock(State& state) {
    ReturnResult(state, pthread_rwlock_unlock(Rwlock(NativeAotGuest::Argument(state, 0))));
}

void CondBroadcast(State& state) {
    ReturnResult(state, pthread_cond_broadcast(Condition(NativeAotGuest::Argument(state, 0))));
}

void CondWait(State& state) {
    const auto mutex = NativeAotGuest::Argument(state, 1);
    RequireDefaultMutex(mutex);
    ReturnResult(state, pthread_cond_wait(Condition(NativeAotGuest::Argument(state, 0)), Mutex(mutex)));
}

constexpr std::size_t StackBytes = 8 * 1024 * 1024;

struct GuestThread {
    pthread_t Host{};
    NativeAotGuestState State{};
    std::uint64_t Id = 0;
    std::uint64_t Target = 0;
    std::uint64_t Top = 0;
    std::uint64_t Result = 0;
    bool Joining = false;
};

std::map<std::uint64_t, std::shared_ptr<GuestThread>> threads;
std::uint64_t nextThread = 2;
thread_local std::uint64_t threadId = 1;

void* RunThread(void* value) {
    auto& thread = *static_cast<GuestThread*>(value);
    threadId = thread.Id;
    thread.State.FsBase = NativeAotGuest::CreateThreadArea();
    thread.Result = NativeAotGuest::Invoke(thread.State, thread.Target);
    NativeAotGuest::FreeThreadArea(thread.State.FsBase);
    return nullptr;
}

void ThreadCreate(State& state) {
    if (!NativeAotGuest::ConcurrentMemory()) Unsupported("pthread_create requires the ordered memory profile");
    if (NativeAotGuest::Argument(state, 1) != 0) Unsupported("only default joinable thread attributes are supported");
    const auto output = NativeAotGuest::Argument(state, 0);
    const auto target = NativeAotGuest::Argument(state, 2);
    if (output == 0 || NativeAotGuestFind(target) == nullptr) {
        ReturnResult(state, EINVAL);
        return;
    }
    auto thread = std::make_shared<GuestThread>();
    thread->Target = target;
    thread->Top = NativeAotGuest::AllocateStack(StackBytes);
    thread->State.Gpr[4] = thread->Top;
    thread->State.Gpr[7] = NativeAotGuest::Argument(state, 3);
    thread->State.Mxcsr = state.Mxcsr;
    thread->State.X87Control = state.X87Control;
    int result;
    {
        std::lock_guard<std::mutex> lock(tableLock);
        thread->Id = nextThread++;
        threads.emplace(thread->Id, thread);
        result = pthread_create(&thread->Host, nullptr, RunThread, thread.get());
        if (result != 0) threads.erase(thread->Id);
    }
    if (result != 0) NativeAotGuest::FreeStack(thread->Top, StackBytes);
    else std::memcpy(reinterpret_cast<void*>(output), &thread->Id, sizeof(thread->Id));
    ReturnResult(state, result);
}

void ThreadJoin(State& state) {
    const auto id = NativeAotGuest::Argument(state, 0);
    if (id == threadId) {
        ReturnResult(state, EDEADLK);
        return;
    }
    std::shared_ptr<GuestThread> thread;
    {
        std::lock_guard<std::mutex> lock(tableLock);
        const auto found = threads.find(id);
        if (found == threads.end()) {
            ReturnResult(state, ESRCH);
            return;
        }
        thread = found->second;
        if (thread->Joining) {
            ReturnResult(state, EINVAL);
            return;
        }
        thread->Joining = true;
    }
    const auto result = pthread_join(thread->Host, nullptr);
    if (result == 0) {
        const auto output = NativeAotGuest::Argument(state, 1);
        if (output != 0) std::memcpy(reinterpret_cast<void*>(output), &thread->Result, sizeof(thread->Result));
        NativeAotGuest::FreeStack(thread->Top, StackBytes);
    }
    {
        std::lock_guard<std::mutex> lock(tableLock);
        if (result == 0) threads.erase(id);
        else thread->Joining = false;
    }
    ReturnResult(state, result);
}

void ThreadSelf(State& state) {
    ReturnInteger(state, threadId);
}

void ThreadEqual(State& state) {
    ReturnInteger(state, NativeAotGuest::Argument(state, 0) == NativeAotGuest::Argument(state, 1));
}

void Once(State& state) {
    const auto address = NativeAotGuest::Argument(state, 0);
    const auto target = NativeAotGuest::Argument(state, 1);
    if (address % std::atomic_ref<std::uint32_t>::required_alignment != 0)
        Unsupported("pthread_once control is misaligned");
    std::atomic_ref<std::uint32_t> control(*reinterpret_cast<std::uint32_t*>(address));
    auto value = control.load();
    for (;;) {
        if (value == 2) break;
        if (value == 0 && control.compare_exchange_strong(value, 1)) {
            NativeAotGuest::Invoke(state, target);
            control.store(2);
            control.notify_all();
            break;
        }
        if (value > 2) Unsupported("unsupported pthread_once control");
        control.wait(1);
        value = control.load();
    }
    ReturnInteger(state, 0);
}

void MutexInit(State& state) {
    if (NativeAotGuest::Argument(state, 1) != 0) Unsupported("only default mutex attributes are supported");
    const auto address = NativeAotGuest::Argument(state, 0);
    std::lock_guard<std::mutex> lock(tableLock);
    if (mutexes.contains(address)) {
        ReturnResult(state, EBUSY);
        return;
    }
    auto object = std::make_unique<pthread_mutex_t>();
    const auto result = pthread_mutex_init(object.get(), nullptr);
    if (result == 0) {
        std::memset(reinterpret_cast<void*>(address), 0, 40);
        mutexes.emplace(address, std::move(object));
    }
    ReturnResult(state, result);
}

void MutexDestroy(State& state) {
    const auto address = NativeAotGuest::Argument(state, 0);
    std::lock_guard<std::mutex> lock(tableLock);
    const auto found = mutexes.find(address);
    if (found == mutexes.end()) {
        ReturnResult(state, EINVAL);
        return;
    }
    const auto result = pthread_mutex_destroy(found->second.get());
    if (result == 0) mutexes.erase(found);
    ReturnResult(state, result);
}

void CondInit(State& state) {
    if (NativeAotGuest::Argument(state, 1) != 0) Unsupported("only default condition attributes are supported");
    const auto address = NativeAotGuest::Argument(state, 0);
    std::lock_guard<std::mutex> lock(tableLock);
    if (conditions.contains(address)) {
        ReturnResult(state, EBUSY);
        return;
    }
    auto object = std::make_unique<pthread_cond_t>();
    const auto result = pthread_cond_init(object.get(), nullptr);
    if (result == 0) {
        std::memset(reinterpret_cast<void*>(address), 0, 48);
        conditions.emplace(address, std::move(object));
    }
    ReturnResult(state, result);
}

void CondDestroy(State& state) {
    const auto address = NativeAotGuest::Argument(state, 0);
    std::lock_guard<std::mutex> lock(tableLock);
    const auto found = conditions.find(address);
    if (found == conditions.end()) {
        ReturnResult(state, EINVAL);
        return;
    }
    const auto result = pthread_cond_destroy(found->second.get());
    if (result == 0) conditions.erase(found);
    ReturnResult(state, result);
}

void CondSignal(State& state) {
    ReturnResult(state, pthread_cond_signal(Condition(NativeAotGuest::Argument(state, 0))));
}

const NativeAotGuest::ProviderBinding Bindings[] = {
    {"pthread_create", ThreadCreate, nullptr},
    {"pthread_join", ThreadJoin, nullptr},
    {"pthread_self", ThreadSelf, nullptr},
    {"pthread_equal", ThreadEqual, nullptr},
    {"pthread_once", Once, nullptr},
    {"pthread_mutex_init", MutexInit, nullptr},
    {"pthread_mutex_destroy", MutexDestroy, nullptr},
    {"pthread_cond_init", CondInit, nullptr},
    {"pthread_cond_destroy", CondDestroy, nullptr},
    {"pthread_cond_signal", CondSignal, nullptr},
    {"pthread_mutex_lock", MutexLock, nullptr},
    {"pthread_mutex_unlock", MutexUnlock, nullptr},
    {"pthread_rwlock_rdlock", ReadLock, nullptr},
    {"pthread_rwlock_wrlock", WriteLock, nullptr},
    {"pthread_rwlock_unlock", RwUnlock, nullptr},
    {"pthread_cond_broadcast", CondBroadcast, nullptr},
    {"pthread_cond_wait", CondWait, nullptr},
};

}

namespace NativeAotGlibcSupport {

void RegisterThreads() {
    Register(Bindings, sizeof(Bindings) / sizeof(Bindings[0]));
}

}
