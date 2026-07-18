#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace pnm
{
    template<typename T = void>
    using Result = std::expected<T, std::error_code>;

    constexpr auto success() -> Result<void> { return {}; }

    namespace utils
    {
        namespace memory
        {
            template<typename T>
            concept Serializable = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

            auto copy(Serializable auto& dest, const Serializable auto& src) -> bool
            {
                auto src_bytes{ std::as_bytes(std::span{ &src, 1 }) };
                auto dest_bytes{ std::as_writable_bytes(std::span{ &dest, 1 }) };
                if (src_bytes.size() != dest_bytes.size()) {
                    return false;
                }
                std::ranges::copy(src_bytes, dest_bytes.begin());
                return true;
            }

            template<auto DeleteFn>
            struct Deleter
            {
                void operator()(auto* ptr) const
                {
                    static_assert(std::invocable<decltype(DeleteFn), decltype(ptr)>,
                                  "The provided function signature does not match the object type.");
                    if (ptr) {
                        DeleteFn(ptr);
                    }
                }
            };
        }

        namespace concurrent
        {
            namespace detail
            {
                struct NotLocked
                {
                    auto lock() -> void {}
                    auto unlock() -> void {}
                };

                template<class T, typename Func, typename... Args>
                concept Thread =
                  !std::copyable<T> && std::movable<T> && requires(Func&& func, Args&&... args) {
                      T{ std::forward<Func>(func), std::forward<Args>(args)... };
                  } && requires(T t) {
                      { t.joinable() } -> std::convertible_to<bool>;
                      { t.join() } -> std::same_as<void>;
                      { t.detach() } -> std::same_as<void>;
                  };

                template<class T, typename Func, typename... Args>
                concept JThread = Thread<T, Func, Args...> && requires(T t) {
                    { t.request_stop() } -> std::convertible_to<bool>;
                    { t.get_stop_token() } -> std::convertible_to<std::stop_token>;
                };
            }

            template<typename T>
            concept Lockable = requires(T t) {
                { t.lock() };
                { t.unlock() };
            };

            template<class T>
            concept Thread = detail::Thread<T, std::identity>;

            template<class T>
            concept JThread = detail::JThread<T, std::identity>;

            template<Thread T, typename Func, typename... Args>
            auto spawn_thread(Func&& func, Args&&... args) -> T
            {
                return T{ std::forward<Func>(func), std::forward<Args>(args)... };
            }

            auto stop_thread(JThread auto& thread) -> void
            {
                if (thread.joinable()) {
                    thread.request_stop();
                    thread.join();
                }
            }
        }

        namespace queue
        {
            template<typename T>
            concept Queue = requires(T t, typename T::value_type v) {
                typename T::value_type;

                requires(requires {
                    { t.empty() } -> std::same_as<bool>;
                } || requires {
                    { t.isEmpty() } -> std::same_as<bool>;
                });

                requires(requires {
                    { t.front() } -> std::same_as<typename T::value_type&>;
                } || requires {
                    { t.head() } -> std::same_as<typename T::value_type&>;
                });

                requires(requires {
                    { t.push(v) } -> std::same_as<void>;
                } || requires {
                    { t.push_back(v) } -> std::same_as<void>;
                } || requires {
                    { t.enqueue(v) } -> std::same_as<void>;
                });

                requires(requires {
                    { t.pop() } -> std::same_as<void>;
                } || requires {
                    { t.pop_front() } -> std::same_as<void>;
                } || requires {
                    { t.dequeue() } -> std::same_as<typename T::value_type>;
                });
            };

            template<concurrent::Lockable L = concurrent::detail::NotLocked>
            // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
            auto pop(Queue auto& queue, L&& lockable = concurrent::detail::NotLocked{})
              -> std::optional<typename std::remove_reference_t<decltype(queue)>::value_type>
            {
                std::scoped_lock lock{ lockable };

                if (queue.empty()) {
                    return std::nullopt;
                }

                if constexpr (requires { queue.dequeue(); }) {
                    return queue.dequeue();
                }
                else {
                    auto val{ std::move(queue.front()) };
                    if constexpr (requires { queue.pop(); }) {
                        queue.pop();
                    }
                    else {
                        queue.pop_front();
                    }
                    return val;
                }
            }

            template<concurrent::Lockable L = concurrent::detail::NotLocked>
            // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
            auto push(Queue auto& queue, auto value, L&& lockable = concurrent::detail::NotLocked{}) -> void
            {
                std::scoped_lock lock{ lockable };

                if constexpr (requires { queue.push(value); }) {
                    queue.push(std::move(value));
                }
                else if constexpr (requires { queue.push_back(value); }) {
                    queue.push_back(std::move(value));
                }
                else {
                    queue.enqueue(std::move(value));
                }
            }
        }

        namespace bit
        {
            template<typename T>
            concept Storage = std::unsigned_integral<T> && !std::same_as<std::remove_cv_t<T>, bool>;

            template<Storage T>
            inline constexpr size_t WIDTH{ std::numeric_limits<T>::digits };

            template<Storage T>
            constexpr size_t size()
            {
                return WIDTH<T>;
            }

            template<size_t I, Storage T>
            constexpr auto bit_mask() -> T
            {
                static_assert(I < size<T>());
                return (T{ 1 } << I);
            }

            template<size_t I, Storage T>
            constexpr auto get() -> T
            {
                return bit_mask<I, T>();
            }

            template<size_t I, size_t N, Storage T>
            constexpr auto mask() -> T
            {
                static_assert(I + N <= size<T>());

                if constexpr (N == 0) {
                    return T{ 0 };
                }
                else if constexpr (N == size<T>()) {
                    return ~T{ 0 };
                }
                else {
                    return static_cast<T>(((T{ 1 } << N) - T{ 1 }) << I);
                }
            }

            template<size_t I, size_t N, Storage T>
            constexpr auto create_mask() -> T
            {
                return mask<I, N, T>();
            }

            template<size_t I, Storage T>
            constexpr auto set(T& data) -> void
            {
                data |= bit_mask<I, T>();
            }

            template<size_t I, Storage T>
            constexpr auto reset(T& data) -> void
            {
                data &= static_cast<T>(~bit_mask<I, T>());
            }

            template<size_t I, Storage T>
            constexpr auto toggle(T& data) -> void
            {
                data ^= bit_mask<I, T>();
            }

            template<size_t I, Storage T>
            constexpr auto check(T data) -> bool
            {
                return (data & bit_mask<I, T>()) != 0;
            }

            template<size_t I, size_t N, Storage T>
            constexpr auto masked_value(T value) -> T
            {
                static_assert(I + N <= size<T>());

                if constexpr (N == 0) {
                    return T{ 0 };
                }
                else if constexpr (N == size<T>()) {
                    return value;
                }
                else {
                    return static_cast<T>(value & ((T{ 1 } << N) - T{ 1 }));
                }
            }

            template<size_t I, size_t N, Storage T>
            constexpr auto fits_mask(T value) -> bool
            {
                return value == masked_value<I, N, T>(value);
            }

            template<size_t I, size_t N, Storage T>
            constexpr auto set_masked(T& data, T value) -> void
            {
                constexpr auto field_mask{ mask<I, N, T>() };
                data =
                  static_cast<T>((data & ~field_mask) | ((masked_value<I, N, T>(value) << I) & field_mask));
            }

            template<size_t I, size_t N, Storage T>
            constexpr auto set_masked_checked(T& data, T value) -> bool
            {
                if (!fits_mask<I, N, T>(value)) {
                    return false;
                }
                set_masked<I, N, T>(data, value);
                return true;
            }

            template<size_t I, size_t N, Storage T>
            constexpr auto get_masked(T data) -> T
            {
                constexpr auto field_mask{ mask<I, N, T>() };
                return static_cast<T>((data & field_mask) >> I);
            }

            template<Storage T>
            constexpr auto bits(T data) -> std::array<bool, size<T>()>
            {
                return [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return std::array<bool, size<T>()>{ check<Is, T>(data)... };
                }(std::make_index_sequence<size<T>()>{});
            }

            template<Storage T>
            constexpr auto to_bitset(T data) -> std::bitset<size<T>()>
            {
                return [&]<size_t... Is>(std::index_sequence<Is...>) {
                    std::bitset<size<T>()> result{};
                    ((result.set(Is, check<Is, T>(data))), ...);
                    return result;
                }(std::make_index_sequence<size<T>()>{});
            }
        }
    }
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

#if defined(PNM_ENABLE_ASSERTS)
#if defined(PNM_PLATFORM_WINDOWS)
#include <intrin.h>
#define PNM_DEBUG_BREAK() __debugbreak()
#elif defined(PNM_PLATFORM_POSIX)
#include <csignal>
#define PNM_DEBUG_BREAK() std::raise(SIGTRAP)
#elif defined(PNM_PLATFORM_GENERIC) && defined(PNM_ARCH_ARM)
#if defined(__GNUC__) && !defined(__llvm__) // GCC
#define PNM_DEBUG_BREAK() __asm volatile("bkpt #0")
#elif defined(__llvm__) // Clang
#define PNM_DEBUG_BREAK() __builtin_trap()
#elif defined(__CC_ARM) // Keil MDK
#define PNM_DEBUG_BREAK() __bkpt(0)
#elif defined(__ICCARM__) // IAR
#define PNM_DEBUG_BREAK() __DebugBreak()
#else
#define PNM_DEBUG_BREAK() std::abort()
#endif
#else
#define PNM_DEBUG_BREAK() std::abort()
#endif
#define PNM_ASSERT(x, msg, ...)                                                                              \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::fprintf(stderr,                                                                             \
                         "Assertion failed (%s) at %s:%d: " msg "\n",                                       \
                         #x,                                                                                 \
                         __FILE__,                                                                           \
                         __LINE__ __VA_OPT__(, ) __VA_ARGS__);                                               \
            PNM_DEBUG_BREAK();                                                                               \
        }                                                                                                    \
    } while (false)
#else
#define PNM_ASSERT(x, msg, ...) ((void)0)
#endif

#if defined(_MSC_VER)
#define PNM_PACK_BEGIN_N(n) __pragma(pack(push, n))
#define PNM_PACK_BEGIN __pragma(pack(push, 1))
#define PNM_PACK_END __pragma(pack(pop))
#elif defined(__GNUC__) || defined(__clang__)
#define PNM_PACK_BEGIN_N(n) _Pragma("pack(push, n)")
#define PNM_PACK_BEGIN _Pragma("pack(push, 1)")
#define PNM_PACK_END _Pragma("pack(pop)")
#else
#define PNM_PACK_BEGIN_N(n)
#define PNM_PACK_BEGIN
#define PNM_PACK_END
#endif

// NOLINTEND(cppcoreguidelines-macro-usage)
