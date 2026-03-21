#pragma once

#include "api/Macro.h"
#include "api/memory/Memory.h"
#include <type_traits>

#define LOG(...)

namespace memory {

using FuncPtr = void *;

void unhookAll();

template <typename T> struct IsConstMemberFun : std::false_type {};

template <typename T, typename Ret, typename... Args>
struct IsConstMemberFun<Ret (T::*)(Args...) const> : std::true_type {};

template <typename T>
inline constexpr bool IsConstMemberFunV = IsConstMemberFun<T>::value;

template <typename T> struct AddConstAtMemberFun {
  using type = T;
};

template <typename T, typename Ret, typename... Args>
struct AddConstAtMemberFun<Ret (T::*)(Args...)> {
  using type = Ret (T::*)(Args...) const;
};

template <typename T>
using AddConstAtMemberFunT = typename AddConstAtMemberFun<T>::type;

template <typename T, typename U>
using AddConstAtMemberFunIfOriginIs =
    std::conditional_t<IsConstMemberFunV<U>, AddConstAtMemberFunT<T>, T>;

/**
 * @brief Hook priority enum.
 * @details The lower priority, the hook will be executed earlier
 */
enum class HookPriority : int {
  Lowest = 0,
  Low = 100,
  Normal = 200,
  High = 300,
  Highest = 400,
};

int hook(FuncPtr target, FuncPtr detour, FuncPtr *originalFunc,
         HookPriority priority, bool suspendThreads = true);

bool unhook(FuncPtr target, FuncPtr detour, bool suspendThreads = true);

/**
 * @brief Get the pointer of a function by identifier.
 *
 * @param identifier symbol or signature
 * @return FuncPtr
 */
FuncPtr resolveIdentifier(char const *identifier);
FuncPtr resolveIdentifier(std::initializer_list<const char *> identifiers);

template <typename T>
concept FuncPtrType = std::is_function_v<std::remove_pointer_t<T>> ||
                      std::is_member_function_pointer_v<T>;

template <typename T>
  requires(FuncPtrType<T> || std::is_same_v<T, uintptr_t>)
constexpr FuncPtr resolveIdentifier(T identifier) {
  return toFuncPtr(identifier);
}

// redirect to resolveIdentifier(char const*)
template <typename T>
constexpr FuncPtr resolveIdentifier(char const *identifier) {
  return resolveIdentifier(identifier);
}

// redirect to resolveIdentifier(uintptr_t)
template <typename T> constexpr FuncPtr resolveIdentifier(uintptr_t address) {
  return resolveIdentifier(address);
}

// redirect to resolveIdentifier(FuncPtr)
template <typename T> constexpr FuncPtr resolveIdentifier(FuncPtr address) {
  return address;
}

template <typename T>
constexpr FuncPtr
resolveIdentifier(std::initializer_list<const char *> identifiers) {
  return resolveIdentifier(identifiers);
}

template <typename T> struct HookAutoRegister {
  HookAutoRegister() { T::hook(); }
  ~HookAutoRegister() { T::unhook(); }
  static int hook() { return T::hook(); }
  static bool unhook() { return T::unhook(); }
};

} // namespace memory
