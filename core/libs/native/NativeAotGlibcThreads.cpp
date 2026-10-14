#include "native/NativeAotGlibcSupport.hpp"
#include <condition_variable>
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

const NativeAotGuest::ProviderBinding Bindings[] = {
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
