#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <limits>
#include <meta>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace pnm
{
    template<typename T = void>
    using Result = std::expected<T, std::error_code>;

    constexpr auto success() -> Result<void> { return {}; }

    namespace utils
    {
        namespace memory
        {
            // Adapters provide bufferSize(const T&), serialize(const T&, span<byte>), and
            // deserialize(span<const byte>, T&). The latter two return void and may throw.
            // Deserialization validates its encoding and reconstructs the destination;
            // bufferSize describes an encoded value, not the destination's capacity.
            template<typename T>
            struct SerializationAdapter;

            namespace detail
            {
                // NOLINTBEGIN(readability-identifier-naming)

                template<typename T>
                using SerializationAdapterFor = SerializationAdapter<std::remove_cvref_t<T>>;

                template<typename T>
                concept HasSerializationAdapter = requires(const std::remove_cvref_t<T>& src_value,
                                                           std::remove_cvref_t<T>& dest_value,
                                                           std::span<const std::byte> src,
                                                           std::span<std::byte> dest) {
                    sizeof(SerializationAdapterFor<T>);
                    { SerializationAdapterFor<T>::serialize(src_value, dest) } -> std::same_as<void>;
                    { SerializationAdapterFor<T>::deserialize(src, dest_value) } -> std::same_as<void>;
                    { SerializationAdapterFor<T>::bufferSize(src_value) } -> std::convertible_to<size_t>;
                };

                template<typename T>
                struct is_span : std::false_type
                {
                };

                template<typename T, size_t Extent>
                struct is_span<std::span<T, Extent>> : std::true_type
                {
                };

                template<typename T>
                inline constexpr bool is_span_v = is_span<std::remove_cvref_t<T>>::value;

                template<typename T>
                consteval auto is_trivially_serializable() -> bool
                {
                    using U = std::remove_cvref_t<T>;

                    if constexpr (!std::is_trivially_copyable_v<U> || std::is_pointer_v<U> ||
                                  std::is_member_pointer_v<U> || std::is_reference_v<T> || is_span_v<U>) {
                        return false;
                    }
                    else if constexpr (std::is_array_v<U>) {
                        return is_trivially_serializable<std::remove_extent_t<U>>();
                    }
                    else if constexpr (std::is_class_v<U> || std::is_union_v<U>) {
                        if constexpr (std::is_class_v<U>) {
                            static constexpr auto bases{ std::define_static_array(
                              std::meta::bases_of(^^U, std::meta::access_context::unchecked())) };
                            // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
                            template for (constexpr auto base : bases)
                            {
                                using BaseType = [:std::meta::type_of(base):];
                                if constexpr (!is_trivially_serializable<BaseType>()) {
                                    return false;
                                }
                            }
                        }

                        static constexpr auto members{ std::define_static_array(
                          std::meta::nonstatic_data_members_of(^^U,
                                                               std::meta::access_context::unchecked())) };
                        // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
                        template for (constexpr auto member : members)
                        {
                            using MemberType = [:std::meta::type_of(member):];
                            if constexpr (!is_trivially_serializable<MemberType>()) {
                                return false;
                            }
                        }
                        return true;
                    }
                    else {
                        return true;
                    }
                }

                // NOLINTEND(readability-identifier-naming)
            }

            // Raw copies cannot apply adapters to individual members or base classes.
            template<typename T>
            concept Serializable =
              detail::HasSerializationAdapter<T> || detail::is_trivially_serializable<T>();

            template<Serializable T>
            auto serialize(const T& src, std::span<std::byte> dest_bytes) -> bool
            {
                if constexpr (detail::HasSerializationAdapter<T>) {
                    if (dest_bytes.size() < detail::SerializationAdapterFor<T>::bufferSize(src)) {
                        return false;
                    }
                    detail::SerializationAdapterFor<T>::serialize(src, dest_bytes);
                }
                else {
                    auto src_bytes{ std::as_bytes(std::span{ &src, 1 }) };
                    if (dest_bytes.size() < src_bytes.size()) {
                        return false;
                    }
                    std::ranges::copy(src_bytes, dest_bytes.begin());
                }
                return true;
            }

            template<Serializable T>
            auto serialize(const T& src) -> std::vector<std::byte>
            {
                if constexpr (detail::HasSerializationAdapter<T>) {
                    std::vector<std::byte> buffer{ detail::SerializationAdapterFor<T>::bufferSize(src) };
                    detail::SerializationAdapterFor<T>::serialize(src, buffer);
                    return buffer;
                }
                else {
                    std::vector<std::byte> buffer{ sizeof(T) };
                    auto src_bytes{ std::as_bytes(std::span{ &src, 1 }) };
                    std::ranges::copy(src_bytes, buffer.begin());
                    return buffer;
                }
            }

            template<Serializable T>
            auto deserialize(std::span<const std::byte> src_bytes, T& dest) -> bool
            {
                if constexpr (detail::HasSerializationAdapter<T>) {
                    detail::SerializationAdapterFor<T>::deserialize(src_bytes, dest);
                }
                else {
                    auto dest_bytes{ std::as_writable_bytes(std::span{ &dest, 1 }) };
                    if (dest_bytes.size() != src_bytes.size()) {
                        return false;
                    }
                    std::ranges::copy(src_bytes, dest_bytes.begin());
                }
                return true;
            }

            template<Serializable T, Serializable U>
            auto copy(T& dest, const U& src) -> bool
            {
                auto bytes{ serialize(src) };
                return deserialize(bytes, dest);
            }

            namespace detail
            {
                // NOLINTBEGIN(readability-identifier-naming)

                template<typename T>
                consteval auto is_serializable_field() -> bool
                {
                    if constexpr (std::is_reference_v<T> || std::is_const_v<T> || std::is_volatile_v<T>) {
                        return false;
                    }
                    else if constexpr (std::is_array_v<T>) {
                        return std::is_bounded_array_v<T> && is_serializable_field<std::remove_extent_t<T>>();
                    }
                    else {
                        return Serializable<T>;
                    }
                }

                template<typename T>
                consteval auto can_generate_serialization_adapter() -> bool
                {
                    if constexpr (requires { sizeof(T); }) {
                        if constexpr (std::is_class_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> &&
                                      !std::is_trivially_copyable_v<T>) {
                            static constexpr auto bases{ std::define_static_array(
                              std::meta::bases_of(^^T, std::meta::access_context::unchecked())) };
                            // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
                            template for (constexpr auto base : bases)
                            {
                                using BaseType = [:std::meta::type_of(base):];
                                // Match the unchecked base conversion used during traversal.
                                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                                if constexpr (!requires(T& value) { (BaseType&)value; } ||
                                              !is_serializable_field<BaseType>()) {
                                    return false;
                                }
                            }
                            static constexpr auto members{ std::define_static_array(
                              std::meta::nonstatic_data_members_of(^^T,
                                                                   std::meta::access_context::unchecked())) };
                            // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
                            template for (constexpr auto member : members)
                            {
                                using MemberType = [:std::meta::type_of(member):];
                                if constexpr (!is_serializable_field<MemberType>()) {
                                    return false;
                                }
                            }
                            return true;
                        }
                    }
                    return false;
                }

                template<typename T, typename Func>
                auto visit_serialization_field(T& field, const Func& function) -> void
                {
                    if constexpr (std::is_array_v<T>) {
                        for (auto& element : field) {
                            visit_serialization_field(element, function);
                        }
                    }
                    else {
                        function(field);
                    }
                }

                template<typename T, typename Func>
                auto for_each_serialization_field(T& value, const Func& function) -> void
                {
                    static constexpr auto bases{ std::define_static_array(
                      std::meta::bases_of(^^std::remove_cv_t<T>, std::meta::access_context::unchecked())) };
                    // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
                    template for (constexpr auto base : bases)
                    {
                        using BaseType = [:std::meta::type_of(base):];
                        using QualifiedBase =
                          std::conditional_t<std::is_const_v<T>, const BaseType, BaseType>;
                        // Clang lacks base splices; this conversion supports private bases too.
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                        visit_serialization_field((QualifiedBase&)value, function);
                    }
                    static constexpr auto members{ std::define_static_array(
                      std::meta::nonstatic_data_members_of(^^std::remove_cv_t<T>,
                                                           std::meta::access_context::unchecked())) };
                    // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
                    template for (constexpr auto member : members)
                    {
                        if constexpr (std::meta::is_bit_field(member)) {
                            if constexpr (std::meta::has_identifier(member)) {
                                auto field = value.[:member:];
                                function(field);
                                if constexpr (!std::is_const_v<T>) {
                                    value.[:member:] = field;
                                }
                            }
                        }
                        else {
                            visit_serialization_field(value.[:member:], function);
                        }
                    }
                }

                inline auto add_serialized_size(size_t& total, size_t size) -> void
                {
                    if (size > std::numeric_limits<size_t>::max() - total) {
                        throw std::length_error{ "Serialized data is too large" };
                    }
                    total += size;
                }

                template<typename T>
                auto serialized_field_size(const T& value) -> size_t
                {
                    if constexpr (HasSerializationAdapter<T>) {
                        return static_cast<size_t>(SerializationAdapterFor<T>::bufferSize(value));
                    }
                    else {
                        return sizeof(T);
                    }
                }

                template<typename T>
                auto serialize_field(const T& value, std::span<std::byte>& output) -> void
                {
                    const size_t size{ serialized_field_size(value) };
                    if constexpr (HasSerializationAdapter<T>) {
                        if (output.size() < sizeof(size)) {
                            throw std::length_error{ "Serialization buffer is too small" };
                        }
                        std::ranges::copy(std::as_bytes(std::span{ &size, 1 }), output.begin());
                        output = output.subspan(sizeof(size));
                    }
                    if (size > output.size() || !memory::serialize(value, output.first(size))) {
                        throw std::length_error{ "Serialization buffer is too small" };
                    }
                    output = output.subspan(size);
                }

                template<typename T>
                auto deserialize_field(std::span<const std::byte>& input, T& value) -> void
                {
                    size_t size{ sizeof(T) };
                    if constexpr (HasSerializationAdapter<T>) {
                        if (input.size() < sizeof(size)) {
                            throw std::invalid_argument{ "Missing serialized member length" };
                        }
                        std::ranges::copy(input.first(sizeof(size)),
                                          std::as_writable_bytes(std::span{ &size, 1 }).begin());
                        input = input.subspan(sizeof(size));
                    }
                    if (size > input.size() || !memory::deserialize(input.first(size), value)) {
                        throw std::invalid_argument{ "Invalid serialized member size" };
                    }
                    input = input.subspan(size);
                }

                // NOLINTEND(readability-identifier-naming)
            }

            // Explicit specializations override this fallback. Only non-trivial classes qualify.
            // Bases precede members; adapter-backed fields carry native size_t byte lengths.
            template<typename T>
                requires(detail::can_generate_serialization_adapter<T>())
            struct SerializationAdapter<T>
            {
                static auto bufferSize(const T& src) -> size_t
                {
                    size_t size{};
                    detail::for_each_serialization_field(src, [&](const auto& field) {
                        using Field = std::remove_cvref_t<decltype(field)>;
                        if constexpr (detail::HasSerializationAdapter<Field>) {
                            detail::add_serialized_size(size, sizeof(size_t));
                        }
                        detail::add_serialized_size(size, detail::serialized_field_size(field));
                    });
                    return size;
                }

                static auto serialize(const T& src, std::span<std::byte> dest) -> void
                {
                    if (dest.size() < bufferSize(src)) {
                        throw std::length_error{ "Serialization buffer is too small" };
                    }
                    detail::for_each_serialization_field(
                      src, [&](const auto& field) { detail::serialize_field(field, dest); });
                }

                // Malformed input throws; previously decoded fields may already have changed.
                static auto deserialize(std::span<const std::byte> src, T& dest) -> void
                {
                    detail::for_each_serialization_field(
                      dest, [&](auto& field) { detail::deserialize_field(src, field); });
                    if (!src.empty()) {
                        throw std::invalid_argument{ "Trailing serialized data" };
                    }
                }
            };

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

            template<typename Rep, typename Period>
            auto sleep_for(const std::chrono::duration<Rep, Period>& duration,
                           const std::stop_token& stop = {}) -> bool
            {
                if (stop.stop_possible()) {
                    std::mutex mutex;
                    std::unique_lock lock{ mutex };
                    std::condition_variable_any{}.wait_for(lock, stop, duration, [] { return false; });
                    return !stop.stop_requested();
                }

                std::this_thread::sleep_for(duration);
                return true;
            }

            template<typename Clock, typename Duration>
            auto sleep_until(const std::chrono::time_point<Clock, Duration>& time,
                             const std::stop_token& stop = {}) -> bool
            {
                if (stop.stop_possible()) {
                    std::mutex mutex;
                    std::unique_lock lock{ mutex };
                    std::condition_variable_any{}.wait_until(lock, stop, time, [] { return false; });
                    return !stop.stop_requested();
                }

                std::this_thread::sleep_until(time);
                return true;
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
                         "Assertion failed (%s) at %s:%d: " msg "\n",                                        \
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
