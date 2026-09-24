#pragma once

#include "pneumo/formatting.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <ranges>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(PNM_PLATFORM_POSIX)
#include <unistd.h>
#endif

namespace pnm::log
{

    enum class Level : uint8_t
    {
        Off = 0,
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical
    };

    enum class Color : uint8_t
    {
        None,
        Black,
        Red,
        Green,
        Yellow,
        Blue,
        Magenta,
        Cyan,
        White,
        BrightBlack,
        BrightRed,
        BrightGreen,
        BrightYellow,
        BrightBlue,
        BrightMagenta,
        BrightCyan,
        BrightWhite
    };

    enum class ColorMode : uint8_t
    {
        Auto,
        Always,
        Never
    };

    constexpr auto default_color(Level level) noexcept -> Color
    {
        switch (level) {
            case Level::Off:
                return Color::None;
            case Level::Trace:
                return Color::BrightBlack;
            case Level::Debug:
                return Color::Cyan;
            case Level::Info:
                return Color::Green;
            case Level::Warn:
                return Color::Yellow;
            case Level::Error:
                return Color::Red;
            case Level::Critical:
                return Color::BrightRed;
            default:
                return Color::None;
        }
    }

    enum class SourceField : uint8_t
    {
        None = 0,
        FileName = utils::bit::get < 0,
        uint8_t>(),
        FilePath = utils::bit::get < 1,
        uint8_t>(),
        Line = utils::bit::get < 2,
        uint8_t>(),
        Column = utils::bit::get < 3,
        uint8_t>(),
        Function = utils::bit::get < 4,
        uint8_t>(),
        Excerpt = utils::bit::get < 5,
        uint8_t>()
    };

    struct SourceInfo
    {
        constexpr auto contains(SourceField field) const noexcept -> bool
        {
            return (fields & std::to_underlying(field)) != 0U;
        }

        constexpr auto enable(SourceField field) noexcept -> void { fields |= std::to_underlying(field); }

        uint8_t fields{};
        size_t excerpt_context{};
    };

    template<typename... Args>
    class BasicLogFormatString
    {
      public:
        template<typename String>
            requires std::convertible_to<const String&, std::string_view>
        consteval BasicLogFormatString(const String& format,
                                       std::source_location location = std::source_location::current())
          : m_format{ format }
          , m_location{ location }
        {
        }

        constexpr BasicLogFormatString(std::format_string<Args...> format,
                                       std::source_location location = std::source_location::current())
          : m_format{ format }
          , m_location{ location }
        {
        }

        constexpr auto getFormat() const -> std::format_string<Args...> { return m_format; }
        constexpr auto getLocation() const noexcept -> std::source_location { return m_location; }

      private:
        std::format_string<Args...> m_format;
        std::source_location m_location;
    };

    template<typename... Args>
    using LogFormatString = BasicLogFormatString<std::type_identity_t<Args>...>;

    class ISink
    {
      public:
        virtual ~ISink() = default;

        virtual auto isOpen() const -> bool = 0;
        virtual auto open() -> pnm::Result<> = 0;
        virtual auto close() -> pnm::Result<> = 0;
        virtual auto write(std::span<const std::byte> data) -> pnm::Result<size_t> = 0;
        virtual auto flush() -> pnm::Result<> = 0;
        virtual auto getMinLevel() const -> Level { return Level::Trace; }
        virtual auto getFlushLevel() const -> Level { return Level::Off; }
        virtual auto getSourceInfo() const -> SourceInfo { return {}; }
        virtual auto getShowLevel() const -> bool { return true; }
        virtual auto getTimestampFormat() const -> std::string_view { return "{:%H:%M:%S}"; }
        virtual auto getColorMode() const -> ColorMode { return ColorMode::Auto; }
        virtual auto getColor(Level level) const -> Color { return default_color(level); }
        virtual auto isTerminal() const -> bool { return false; }

      protected:
        ISink() = default;
        ISink(const ISink&) = default;
        auto operator=(const ISink&) -> ISink& = default;
        ISink(ISink&&) noexcept = default;
        auto operator=(ISink&&) noexcept -> ISink& = default;
    };

    template<typename Sink>
    class SinkBase : public ISink
    {
      public:
        ~SinkBase() override = default;

        auto minLevel(Level level) -> Sink&
        {
            m_minLevel = level;
            return static_cast<Sink&>(*this);
        }

        auto flushOn(Level level) -> Sink&
        {
            m_flushLevel = level;
            return static_cast<Sink&>(*this);
        }

        template<typename... Fields>
            requires(sizeof...(Fields) > 0U &&
                     (std::same_as<std::remove_cvref_t<Fields>, SourceField> && ...))
        auto sourceInfo(Fields... fields) -> Sink&
        {
            m_sourceInfo = {};
            (m_sourceInfo.enable(fields), ...);
            return static_cast<Sink&>(*this);
        }

        auto sourceExcerpt(size_t context_size = 0) -> Sink&
        {
            m_sourceInfo.enable(SourceField::Excerpt);
            m_sourceInfo.excerpt_context = context_size;
            return static_cast<Sink&>(*this);
        }

        auto showLevel(bool show_level) -> Sink&
        {
            m_showLevel = show_level;
            return static_cast<Sink&>(*this);
        }

        auto timestampFormat(std::format_string<std::chrono::system_clock::time_point> format) -> Sink&
        {
            m_timestampFormat = format.get();
            return static_cast<Sink&>(*this);
        }

        auto colors(ColorMode mode = ColorMode::Always) -> Sink&
        {
            m_colorMode = mode;
            return static_cast<Sink&>(*this);
        }

        auto color(Level level, Color color) -> Sink&
        {
            if (level < Level::Trace || level > Level::Critical) {
                throw std::invalid_argument{ "Cannot set color for an invalid log level" };
            }
            m_colors[std::to_underlying(level)] = color;
            return static_cast<Sink&>(*this);
        }

        auto resetColors() -> Sink&
        {
            m_colors = DEFAULT_COLORS;
            return static_cast<Sink&>(*this);
        }

        auto getMinLevel() const -> Level override { return m_minLevel; }
        auto getFlushLevel() const -> Level override { return m_flushLevel; }
        auto getSourceInfo() const -> SourceInfo override { return m_sourceInfo; }
        auto getShowLevel() const -> bool override { return m_showLevel; }
        auto getTimestampFormat() const -> std::string_view override { return m_timestampFormat; }
        auto getColorMode() const -> ColorMode override { return m_colorMode; }
        auto getColor(Level level) const -> Color override
        {
            if (level < Level::Trace || level > Level::Critical) {
                return Color::None;
            }
            return m_colors[std::to_underlying(level)];
        }

      private:
        friend Sink;

        SinkBase() = default;
        SinkBase(const SinkBase&) = default;
        auto operator=(const SinkBase&) -> SinkBase& = default;
        SinkBase(SinkBase&&) noexcept = default;
        auto operator=(SinkBase&&) noexcept -> SinkBase& = default;

        Level m_minLevel{ Level::Trace };
        Level m_flushLevel{ Level::Off };
        SourceInfo m_sourceInfo{};
        bool m_showLevel{ true };
        std::string m_timestampFormat{ "{:%H:%M:%S}" };
        static constexpr std::array DEFAULT_COLORS{
            default_color(Level::Off),     default_color(Level::Trace), default_color(Level::Debug),
            default_color(Level::Info),    default_color(Level::Warn),  default_color(Level::Error),
            default_color(Level::Critical)
        };
        ColorMode m_colorMode{ ColorMode::Auto };
        std::array<Color, DEFAULT_COLORS.size()> m_colors{ DEFAULT_COLORS };
    };

    namespace detail
    {
        inline auto stream_is_terminal(std::FILE* stream) -> bool
        {
#if defined(PNM_PLATFORM_POSIX)
            return ::isatty(::fileno(stream)) == 1;
#else
            static_cast<void>(stream);
            return false;
#endif
        }

        constexpr auto color_sequence(Color color) noexcept -> std::string_view
        {
            switch (color) {
                case Color::None:
                    return "\x1b[0m";
                case Color::Black:
                    return "\x1b[30m";
                case Color::Red:
                    return "\x1b[31m";
                case Color::Green:
                    return "\x1b[32m";
                case Color::Yellow:
                    return "\x1b[33m";
                case Color::Blue:
                    return "\x1b[34m";
                case Color::Magenta:
                    return "\x1b[35m";
                case Color::Cyan:
                    return "\x1b[36m";
                case Color::White:
                    return "\x1b[37m";
                case Color::BrightBlack:
                    return "\x1b[90m";
                case Color::BrightRed:
                    return "\x1b[91m";
                case Color::BrightGreen:
                    return "\x1b[92m";
                case Color::BrightYellow:
                    return "\x1b[93m";
                case Color::BrightBlue:
                    return "\x1b[94m";
                case Color::BrightMagenta:
                    return "\x1b[95m";
                case Color::BrightCyan:
                    return "\x1b[96m";
                case Color::BrightWhite:
                    return "\x1b[97m";
                default:
                    return {};
            }
        }

        inline auto unique_id() noexcept -> uint64_t
        {
            static std::atomic_uint64_t next{ 1 };
            return next.fetch_add(1, std::memory_order_relaxed);
        }

        inline auto report_internal_error(std::string_view message) noexcept -> void
        {
            static_cast<void>(std::fwrite(message.data(), sizeof(char), message.size(), stderr));
            static_cast<void>(std::fputc('\n', stderr));
            static_cast<void>(std::fflush(stderr));
        }

        constexpr auto should_log(Level message_level, Level min_level) noexcept -> bool
        {
            return min_level != Level::Off &&
                   std::to_underlying(message_level) >= std::to_underlying(min_level);
        }

        constexpr auto should_flush(Level message_level, Level flush_level) noexcept -> bool
        {
            return flush_level != Level::Off &&
                   std::to_underlying(message_level) >= std::to_underlying(flush_level);
        }

        template<meta::string::FixedString Lvl>
        consteval auto level_from_string() -> Level
        {
            static_assert(Lvl.size() > 0, "Log level string cannot be empty");
            std::string str(Lvl);
            std::ranges::transform(str | std::views::take(1), str.begin(), [](char c) {
                return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
            });
            const auto level{ meta::enumeration::from_string<Level>(str) };
            if (!level) {
                throw std::invalid_argument{ "Unknown log level" };
            }
            return *level;
        }

        class StdOutSink : public SinkBase<StdOutSink>
        {
          public:
            auto isTerminal() const -> bool override { return stream_is_terminal(stdout); }

            auto isOpen() const -> bool override { return true; }

            auto open() -> pnm::Result<> override { return {}; }

            auto close() -> pnm::Result<> override { return {}; }

            auto write(std::span<const std::byte> data) -> pnm::Result<size_t> override
            {
                const auto written{ std::fwrite(data.data(), sizeof(char), data.size(), stdout) };
                if (written != data.size()) {
                    return std::unexpected(std::make_error_code(std::errc::io_error));
                }
                return written;
            }

            auto flush() -> pnm::Result<> override
            {
                if (std::fflush(stdout) != 0) {
                    return std::unexpected(std::make_error_code(std::errc::io_error));
                }
                return {};
            }
        };

        inline auto standard_output_sink() -> const std::shared_ptr<StdOutSink>&
        {
            static const auto sink{ std::make_shared<StdOutSink>() };
            return sink;
        }

        class StdErrSink : public SinkBase<StdErrSink>
        {
          public:
            auto isTerminal() const -> bool override { return stream_is_terminal(stderr); }

            auto isOpen() const -> bool override { return true; }

            auto open() -> pnm::Result<> override { return {}; }

            auto close() -> pnm::Result<> override { return {}; }

            auto write(std::span<const std::byte> data) -> pnm::Result<size_t> override
            {
                const auto written{ std::fwrite(data.data(), sizeof(char), data.size(), stderr) };
                if (written != data.size()) {
                    return std::unexpected(std::make_error_code(std::errc::io_error));
                }
                return written;
            }

            auto flush() -> pnm::Result<> override
            {
                if (std::fflush(stderr) != 0) {
                    return std::unexpected(std::make_error_code(std::errc::io_error));
                }
                return {};
            }
        };

        inline auto standard_error_sink() -> const std::shared_ptr<StdErrSink>&
        {
            static const auto sink{ std::make_shared<StdErrSink>() };
            return sink;
        }

        enum class FileMode : uint8_t
        {
            Append = 0,
            Overwrite
        };

        class FileSink : public SinkBase<FileSink>
        {
          public:
            explicit FileSink(std::filesystem::path path)
              : m_path(std::move(path))
            {
            }

            auto isOpen() const -> bool override
            {
                auto handle_result{ getHandle() };
                if (!handle_result) {
                    return false;
                }

                const auto& handle{ handle_result.value() };
                std::scoped_lock lock{ handle->mutex };
                return handle->stream.is_open();
            }

            auto open() -> pnm::Result<> override
            {
                auto registered_handle_result{ getOrCreateHandle() };
                if (!registered_handle_result) {
                    return std::unexpected(registered_handle_result.error());
                }

                auto registered_handle{ std::move(registered_handle_result).value() };
                const auto& handle{ registered_handle.handle };
                std::scoped_lock lock{ handle->mutex };

                if (handle->stream.is_open()) {
                    return {};
                }

                std::ios_base::openmode mode{
                    std::ios::out | (handle->mode == FileMode::Append ? std::ios::app : std::ios::trunc)
                };

                try {
                    handle->stream.clear();
                    handle->stream.open(handle->path, mode);
                } catch (const std::bad_alloc&) {
                    s_fileHandles.erase(registered_handle.handle_it);
                    return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
                }

                if (!handle->stream.is_open() || handle->stream.fail()) {
                    s_fileHandles.erase(registered_handle.handle_it);
                    return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
                }

                return {};
            }

            auto close() -> pnm::Result<> override
            {
                std::scoped_lock handles_lock{ s_fileHandlesMutex };

                auto handle_it{ s_fileHandles.find(m_path) };
                if (handle_it == s_fileHandles.end()) {
                    return {};
                }

                auto handle{ handle_it->second };
                std::scoped_lock lock{ handle->mutex };
                if (handle->stream.is_open()) {
                    handle->stream.close();
                    if (handle->stream.is_open() || handle->stream.fail()) {
                        return std::unexpected(std::make_error_code(std::errc::io_error));
                    }
                }

                s_fileHandles.erase(handle_it);
                return {};
            }

            auto write(std::span<const std::byte> data) -> pnm::Result<size_t> override
            {
                return withValidHandle([data](FileHandle& handle) -> pnm::Result<size_t> {
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                    handle.stream.write(reinterpret_cast<const char*>(data.data()),
                                        static_cast<std::streamsize>(data.size()));
                    return data.size();
                });
            }

            auto flush() -> pnm::Result<> override
            {
                return withValidHandle([](FileHandle& handle) -> pnm::Result<> {
                    handle.stream.flush();
                    return {};
                });
            }

            auto mode(FileMode file_mode) -> FileSink&
            {
                m_mode = file_mode;
                return *this;
            }

            static auto closeAll() noexcept -> void
            {
                try {
                    std::map<std::filesystem::path, std::shared_ptr<FileHandle>> handles;
                    {
                        std::scoped_lock lock{ s_fileHandlesMutex };
                        handles.swap(s_fileHandles);
                    }

                    for (auto& item : handles) {
                        auto& handle{ item.second };
                        std::scoped_lock lock{ handle->mutex };
                        if (handle->stream.is_open()) {
                            handle->stream.close();
                        }
                    }
                } catch (...) {
                    report_internal_error("pneumo logging: failed to close cached file handles");
                }
            }

          private:
            std::filesystem::path m_path;
            FileMode m_mode{ FileMode::Append };

            struct FileHandle
            {
                FileHandle() = delete;
                explicit FileHandle(std::filesystem::path file_path, FileMode file_mode)
                  : path{ std::move(file_path) }
                  , mode{ file_mode }
                {
                }

                std::filesystem::path path;
                const FileMode mode;
                std::mutex mutex;
                std::ofstream stream;
            };

            using FileHandleMap = std::map<std::filesystem::path, std::shared_ptr<FileHandle>>;

            struct RegisteredFileHandle
            {
                std::unique_lock<std::mutex> handles_lock;
                FileHandleMap::iterator handle_it;
                std::shared_ptr<FileHandle> handle;
            };

            auto getOrCreateHandle() -> pnm::Result<RegisteredFileHandle>
            {
                std::unique_lock handles_lock{ s_fileHandlesMutex };

                try {
                    auto handle_it{ s_fileHandles.find(m_path) };
                    if (handle_it == s_fileHandles.end()) {
                        auto handle{ std::make_shared<FileHandle>(m_path, m_mode) };
                        handle_it = s_fileHandles.emplace(m_path, std::move(handle)).first;
                    }

                    return RegisteredFileHandle{ .handles_lock = std::move(handles_lock),
                                                 .handle_it = handle_it,
                                                 .handle = handle_it->second };
                } catch (const std::bad_alloc&) {
                    return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
                }
            }

            auto getHandle() const -> pnm::Result<std::shared_ptr<FileHandle>>
            {
                std::scoped_lock lock{ s_fileHandlesMutex };

                auto handle{ s_fileHandles.find(m_path) };
                if (handle == s_fileHandles.end()) {
                    return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
                }
                return handle->second;
            }

            template<typename Func>
            auto withValidHandle(Func&& func) -> decltype(func(std::declval<FileHandle&>()))
            {
                return getHandle().and_then(
                  [func = std::forward<Func>(func)](
                    const std::shared_ptr<FileHandle>& handle) -> decltype(func(*handle)) {
                    std::scoped_lock lock{ handle->mutex };

                    if (!handle->stream.is_open()) {
                        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
                    }

                    auto result{ func(*handle) };

                    if (handle->stream.fail() || handle->stream.bad()) {
                        return std::unexpected(std::make_error_code(std::errc::io_error));
                    }

                    return result;
                });
            }

            inline static std::mutex s_fileHandlesMutex;
            inline static FileHandleMap s_fileHandles;
        };

        struct LogRecord
        {
            Level level{};
            std::string message;
            std::chrono::system_clock::time_point timestamp;
            std::source_location source;
            std::optional<Color> color;
        };

        struct LogEntry
        {
            LogRecord record;
            std::vector<std::shared_ptr<ISink>> sinks;
        };

        constexpr auto source_file_name(std::string_view path) -> std::string_view
        {
            const auto separator{ path.find_last_of("/\\") };
            return separator == std::string_view::npos ? path : path.substr(separator + 1U);
        }

        inline auto format_source(const LogRecord& record, SourceInfo info) -> std::string
        {
            std::string source;

            if (info.contains(SourceField::FilePath)) {
                source = record.source.file_name();
            }
            else if (info.contains(SourceField::FileName)) {
                source = source_file_name(record.source.file_name());
            }

            if (info.contains(SourceField::Line)) {
                source += source.empty() ? "line " : ":";
                source += std::to_string(record.source.line());
            }

            if (info.contains(SourceField::Column)) {
                if (info.contains(SourceField::Line)) {
                    source += ":";
                }
                else {
                    source += source.empty() ? "column " : ":column ";
                }
                source += std::to_string(record.source.column());
            }

            if (info.contains(SourceField::Function)) {
                if (!source.empty()) {
                    source += ' ';
                }
                source += record.source.function_name();
            }

            return source;
        }

        inline auto format_record(const LogRecord& record, const ISink& sink) -> std::string
        {
            const auto source_info{ sink.getSourceInfo() };
            const auto color_mode{ sink.getColorMode() };
            const auto use_color{ color_mode == ColorMode::Always ||
                                  (color_mode == ColorMode::Auto && sink.isTerminal()) };
            const auto color{ use_color
                                ? color_sequence(record.color ? *record.color : sink.getColor(record.level))
                                : std::string_view{} };
            auto output{ std::string{ color } };
            output += '[';
            output += std::vformat(sink.getTimestampFormat(), std::make_format_args(record.timestamp));
            output += ']';

            if (sink.getShowLevel()) {
                output += std::format(" {}", record.level);
            }

            if (auto source{ format_source(record, source_info) }; !source.empty()) {
                output += std::format(" [{}]", source);
            }

            output += ": ";
            if (!color.empty()) {
                output += color_sequence(Color::None);
            }
            output += record.message;
            output += '\n';

            if (source_info.contains(SourceField::Excerpt)) {
                if (auto excerpt{ meta::source::excerpt(
                      record.source.file_name(), record.source.line(), source_info.excerpt_context) }) {
                    output += *excerpt;
                }
            }

            return output;
        }

        inline auto write_all(ISink& sink, std::span<const std::byte> data) -> bool
        {
            while (!data.empty()) {
                const auto result{ sink.write(data) };
                if (!result || result.value() == 0 || result.value() > data.size()) {
                    return false;
                }
                data = data.subspan(result.value());
            }
            return true;
        }

        class Backend
        {
          public:
            Backend(const Backend&) = delete;
            auto operator=(const Backend&) -> Backend& = delete;
            Backend(Backend&&) = delete;
            auto operator=(Backend&&) -> Backend& = delete;

            auto emit(Level level,
                      std::string message,
                      std::source_location source,
                      bool sync,
                      std::shared_ptr<ISink> sink = nullptr,
                      std::optional<Color> color = std::nullopt) -> void
            {
                std::vector<std::shared_ptr<ISink>> sinks;
                if (sink) {
                    sinks.push_back(std::move(sink));
                }
                else {
                    std::scoped_lock lock{ m_routingMutex };
                    sinks.reserve(m_globalSinks.size() + 1U);
                    const auto& default_sink{ m_defaultSinks[std::to_underlying(level)] };
                    if (default_sink) {
                        sinks.push_back(default_sink);
                    }
                    for (const auto& item : m_globalSinks) {
                        sinks.push_back(item.second);
                    }
                }

                LogEntry entry{ .record = { .level = level,
                                            .message = std::move(message),
                                            .timestamp = std::chrono::system_clock::now(),
                                            .source = source,
                                            .color = color },
                                .sinks = std::move(sinks) };

                if (sync) {
                    write(entry);
                    return;
                }

                utils::queue::push(m_queue, std::move(entry), m_mutex);
                m_cv.notify_one();
            }

            auto addGlobalSink(uint64_t id, std::shared_ptr<ISink> sink) -> bool
            {
                if (id == 0 || !sink) {
                    return false;
                }

                std::scoped_lock lock{ m_routingMutex };
                return m_globalSinks.emplace(id, std::move(sink)).second;
            }

            auto removeGlobalSink(uint64_t id) -> bool
            {
                std::scoped_lock lock{ m_routingMutex };
                return m_globalSinks.erase(id) > 0;
            }

            auto removeAllGlobalSinks() -> void
            {
                std::scoped_lock lock{ m_routingMutex };
                m_globalSinks.clear();
                m_defaultSinks.fill(nullptr);
            }

            auto setDefaultSink(Level first, Level last, std::shared_ptr<ISink> sink) -> bool
            {
                if (!validLogLevel(first) || !validLogLevel(last) || first > last || !sink) {
                    return false;
                }

                std::scoped_lock lock{ m_routingMutex };
                const auto last_level{ std::to_underlying(last) };
                for (auto level{ std::to_underlying(first) }; level <= last_level; ++level) {
                    m_defaultSinks[level] = level == last_level ? std::move(sink) : sink;
                }
                return true;
            }

            auto removeDefaultSink(Level first, Level last) -> bool
            {
                if (!validLogLevel(first) || !validLogLevel(last) || first > last) {
                    return false;
                }

                std::scoped_lock lock{ m_routingMutex };
                for (auto level{ std::to_underlying(first) }; level <= std::to_underlying(last); ++level) {
                    m_defaultSinks[level].reset();
                }
                return true;
            }

            auto getDefaultSink(Level level) const -> std::shared_ptr<ISink>
            {
                if (!validLogLevel(level)) {
                    return nullptr;
                }

                std::scoped_lock lock{ m_routingMutex };
                return m_defaultSinks[std::to_underlying(level)];
            }

            auto resetDefaultSinks() -> void
            {
                std::scoped_lock lock{ m_routingMutex };
                m_defaultSinks = makeDefaultSinks();
            }

          private:
            static constexpr auto NUM_LEVELS{ std::to_underlying(Level::Critical) + 1U };
            using DefaultSinks = std::array<std::shared_ptr<ISink>, NUM_LEVELS>;

            static constexpr auto validLogLevel(Level level) noexcept -> bool
            {
                return level >= Level::Trace && level <= Level::Critical;
            }

            static auto makeDefaultSinks() -> DefaultSinks
            {
                const auto& output{ standard_output_sink() };
                const auto& error{ standard_error_sink() };
                return { nullptr, output, output, output, output, error, error };
            }

            Backend()
              : m_worker{ utils::concurrent::spawn_thread<std::jthread>(
                  [this](const std::stop_token& token) { doWork(token); }) }
            {
            }
            ~Backend()
            {
                if (m_worker.joinable()) {
                    m_worker.request_stop();
                    m_cv.notify_all();
                    m_worker.join();
                }
                FileSink::closeAll();
            }

            static auto write(const LogEntry& entry) -> void
            {
                for (const auto& sink : entry.sinks) {
                    try {
                        if (!should_log(entry.record.level, sink->getMinLevel())) {
                            continue;
                        }

                        if (!sink->isOpen()) {
                            if (!sink->open()) {
                                continue;
                            }
                        }

                        const auto msg{ format_record(entry.record, *sink) };
                        const auto data{ std::as_bytes(std::span<const char>{ msg.data(), msg.size() }) };
                        if (write_all(*sink, data) &&
                            should_flush(entry.record.level, sink->getFlushLevel())) {
                            if (!sink->flush()) {
                                report_internal_error("pneumo logging: failed to flush a sink");
                            }
                        }
                    } catch (...) {
                        report_internal_error("pneumo logging: a sink dropped a message after an exception");
                    }
                }
            }

            auto doWork(const std::stop_token& token) -> void
            {
                while (true) {
                    LogEntry entry;
                    {
                        std::unique_lock lock{ m_mutex };

                        auto has_entry{ m_cv.wait(lock, token, [this] { return !m_queue.empty(); }) };
                        if (!has_entry) {
                            break;
                        }

                        auto next_entry{ utils::queue::pop(m_queue) };
                        if (!next_entry) {
                            continue;
                        }
                        entry = std::move(*next_entry);
                    }

                    write(entry);
                }
            }

            mutable std::mutex m_mutex;
            mutable std::mutex m_routingMutex;
            std::condition_variable_any m_cv;
            std::deque<LogEntry> m_queue;
            DefaultSinks m_defaultSinks{ makeDefaultSinks() };
            std::map<uint64_t, std::shared_ptr<ISink>> m_globalSinks;
            std::jthread m_worker;

            friend auto backend() -> Backend&;
        };

        inline auto backend() -> Backend&
        {
            static Backend instance{};
            return instance;
        }

        struct Immediate
        {
        };

        template<meta::string::FixedString Lvl, bool Sync, typename... Args>
        auto log_message(std::optional<Color> color, LogFormatString<Args...> fmt, Args&&... args) noexcept
          -> void
        {
            constexpr auto level{ level_from_string<Lvl>() };
            try {
                backend().emit(level,
                               std::format(fmt.getFormat(), std::forward<Args>(args)...),
                               fmt.getLocation(),
                               Sync,
                               nullptr,
                               color);
            } catch (...) {
                report_internal_error("pneumo logging: dropped a message after an internal exception");
            }
        }

        template<meta::string::FixedString Lvl, bool Sync, typename... Args>
        auto log_message_to_sink(std::shared_ptr<ISink> sink,
                                 std::optional<Color> color,
                                 LogFormatString<Args...> fmt,
                                 Args&&... args) noexcept -> void
        {
            if (!sink) {
                return;
            }

            constexpr auto level{ level_from_string<Lvl>() };
            try {
                backend().emit(level,
                               std::format(fmt.getFormat(), std::forward<Args>(args)...),
                               fmt.getLocation(),
                               Sync,
                               std::move(sink),
                               color);
            } catch (...) {
                report_internal_error("pneumo logging: dropped a routed message after an internal exception");
            }
        }

        template<typename T>
        concept Sink = std::derived_from<T, SinkBase<T>>;

        template<typename T>
        concept SinkObject = Sink<std::remove_cvref_t<T>>;

        template<SinkObject Sink>
        auto make_sink_ptr(Sink&& sink) -> std::shared_ptr<ISink>
        {
            return std::make_shared<std::remove_cvref_t<Sink>>(std::forward<Sink>(sink));
        }

        template<meta::string::FixedString Lvl, bool Sync, SinkObject Sink, typename... Args>
        auto log_message_to_sink_object(Sink&& sink,
                                        std::optional<Color> color,
                                        LogFormatString<Args...> fmt,
                                        Args&&... args) noexcept -> void
        {
            try {
                log_message_to_sink<Lvl, Sync>(
                  make_sink_ptr(std::forward<Sink>(sink)), color, fmt, std::forward<Args>(args)...);
            } catch (...) {
                report_internal_error("pneumo logging: failed to create a routed sink");
            }
        }

    }

    using FileMode = detail::FileMode;

    inline auto initialize() -> void { (void)detail::backend(); }

    // NOLINTBEGIN(readability-identifier-naming)

    inline constexpr detail::Immediate immediate{};
    inline constexpr auto no_color{ Color::None };
    inline constexpr auto black{ Color::Black };
    inline constexpr auto red{ Color::Red };
    inline constexpr auto green{ Color::Green };
    inline constexpr auto yellow{ Color::Yellow };
    inline constexpr auto blue{ Color::Blue };
    inline constexpr auto magenta{ Color::Magenta };
    inline constexpr auto cyan{ Color::Cyan };
    inline constexpr auto white{ Color::White };
    inline constexpr auto bright_black{ Color::BrightBlack };
    inline constexpr auto bright_red{ Color::BrightRed };
    inline constexpr auto bright_green{ Color::BrightGreen };
    inline constexpr auto bright_yellow{ Color::BrightYellow };
    inline constexpr auto bright_blue{ Color::BrightBlue };
    inline constexpr auto bright_magenta{ Color::BrightMagenta };
    inline constexpr auto bright_cyan{ Color::BrightCyan };
    inline constexpr auto bright_white{ Color::BrightWhite };

    inline const auto std_out{ detail::standard_output_sink() };
    inline const auto std_err{ detail::standard_error_sink() };
    inline const auto file{ [](auto path) { return detail::FileSink{ path }; } };

    // NOLINTEND(readability-identifier-naming)

    inline auto set_default_sink(Level first, Level last, std::shared_ptr<ISink> sink) -> bool
    {
        return detail::backend().setDefaultSink(first, last, std::move(sink));
    }

    inline auto set_default_sink(Level level, std::shared_ptr<ISink> sink) -> bool
    {
        return set_default_sink(level, level, std::move(sink));
    }

    template<detail::SinkObject Sink>
    auto set_default_sink(Level first, Level last, Sink&& sink) -> bool
    {
        return set_default_sink(first, last, detail::make_sink_ptr(std::forward<Sink>(sink)));
    }

    template<detail::SinkObject Sink>
    auto set_default_sink(Level level, Sink&& sink) -> bool
    {
        return set_default_sink(level, level, std::forward<Sink>(sink));
    }

    inline auto remove_default_sink(Level first, Level last) -> bool
    {
        return detail::backend().removeDefaultSink(first, last);
    }

    inline auto remove_default_sink(Level level) -> bool { return remove_default_sink(level, level); }

    inline auto get_default_sink(Level level) -> std::shared_ptr<ISink>
    {
        return detail::backend().getDefaultSink(level);
    }

    inline auto reset_default_sinks() -> void { detail::backend().resetDefaultSinks(); }

    struct SinkHandle
    {
        uint64_t id{};
        static auto create() noexcept -> SinkHandle { return SinkHandle{ .id = detail::unique_id() }; }
        constexpr auto valid() const noexcept -> bool { return id > 0; }
        constexpr auto operator<=>(const SinkHandle&) const = default;
    };

    template<std::derived_from<ISink> Sink>
    auto add_global_sink(std::shared_ptr<Sink> sink) -> SinkHandle
    {
        if (!sink) {
            return {};
        }

        auto handle{ SinkHandle::create() };
        if (!detail::backend().addGlobalSink(handle.id, std::static_pointer_cast<ISink>(std::move(sink)))) {
            return {};
        }
        return handle;
    }

    template<detail::SinkObject Sink>
    auto add_global_sink(Sink&& sink) -> SinkHandle
    {
        auto handle{ SinkHandle::create() };
        if (!detail::backend().addGlobalSink(handle.id, detail::make_sink_ptr(std::forward<Sink>(sink)))) {
            return {};
        }
        return handle;
    }

    inline auto remove_global_sink(SinkHandle handle) -> bool
    {
        return handle.valid() && detail::backend().removeGlobalSink(handle.id);
    }

    inline auto remove_all_global_sinks() -> void { detail::backend().removeAllGlobalSinks(); }

#define PNM_DEFINE_LOG_LEVEL(level)                                                                          \
    template<typename... Args>                                                                               \
    inline auto level(LogFormatString<Args...> fmt, Args&&... args) noexcept -> void                         \
    {                                                                                                        \
        detail::log_message<#level, false>(std::nullopt, fmt, std::forward<Args>(args)...);                  \
    }                                                                                                        \
                                                                                                             \
    template<std::derived_from<ISink> Sink, typename... Args>                                                \
    inline auto level(std::shared_ptr<Sink> sink, LogFormatString<Args...> fmt, Args&&... args) noexcept     \
      -> void                                                                                                \
    {                                                                                                        \
        detail::log_message_to_sink<#level, false>(                                                          \
          std::static_pointer_cast<ISink>(std::move(sink)), std::nullopt, fmt, std::forward<Args>(args)...); \
    }                                                                                                        \
                                                                                                             \
    template<detail::SinkObject Sink, typename... Args>                                                      \
    inline auto level(Sink&& sink, LogFormatString<Args...> fmt, Args&&... args) noexcept -> void            \
    {                                                                                                        \
        detail::log_message_to_sink_object<#level, false>(                                                   \
          std::forward<Sink>(sink), std::nullopt, fmt, std::forward<Args>(args)...);                         \
    }                                                                                                        \
                                                                                                             \
    template<typename... Args>                                                                               \
    inline auto level(detail::Immediate, LogFormatString<Args...> fmt, Args&&... args) noexcept -> void      \
    {                                                                                                        \
        detail::log_message<#level, true>(std::nullopt, fmt, std::forward<Args>(args)...);                   \
    }                                                                                                        \
                                                                                                             \
    template<std::derived_from<ISink> Sink, typename... Args>                                                \
    inline auto level(                                                                                       \
      detail::Immediate, std::shared_ptr<Sink> sink, LogFormatString<Args...> fmt, Args&&... args) noexcept  \
      -> void                                                                                                \
    {                                                                                                        \
        detail::log_message_to_sink<#level, true>(                                                           \
          std::static_pointer_cast<ISink>(std::move(sink)), std::nullopt, fmt, std::forward<Args>(args)...); \
    }                                                                                                        \
                                                                                                             \
    template<detail::SinkObject Sink, typename... Args>                                                      \
    inline auto level(detail::Immediate, Sink&& sink, LogFormatString<Args...> fmt, Args&&... args) noexcept \
      -> void                                                                                                \
    {                                                                                                        \
        detail::log_message_to_sink_object<#level, true>(                                                    \
          std::forward<Sink>(sink), std::nullopt, fmt, std::forward<Args>(args)...);                         \
    }                                                                                                        \
                                                                                                             \
    template<typename... Args>                                                                               \
    inline auto level(Color color, LogFormatString<Args...> fmt, Args&&... args) noexcept -> void            \
    {                                                                                                        \
        detail::log_message<#level, false>(color, fmt, std::forward<Args>(args)...);                         \
    }                                                                                                        \
                                                                                                             \
    template<std::derived_from<ISink> Sink, typename... Args>                                                \
    inline auto level(                                                                                       \
      std::shared_ptr<Sink> sink, Color color, LogFormatString<Args...> fmt, Args&&... args) noexcept        \
      -> void                                                                                                \
    {                                                                                                        \
        detail::log_message_to_sink<#level, false>(                                                          \
          std::static_pointer_cast<ISink>(std::move(sink)), color, fmt, std::forward<Args>(args)...);        \
    }                                                                                                        \
                                                                                                             \
    template<detail::SinkObject Sink, typename... Args>                                                      \
    inline auto level(Sink&& sink, Color color, LogFormatString<Args...> fmt, Args&&... args) noexcept       \
      -> void                                                                                                \
    {                                                                                                        \
        detail::log_message_to_sink_object<#level, false>(                                                   \
          std::forward<Sink>(sink), color, fmt, std::forward<Args>(args)...);                                \
    }                                                                                                        \
                                                                                                             \
    template<typename... Args>                                                                               \
    inline auto level(detail::Immediate, Color color, LogFormatString<Args...> fmt, Args&&... args) noexcept \
      -> void                                                                                                \
    {                                                                                                        \
        detail::log_message<#level, true>(color, fmt, std::forward<Args>(args)...);                          \
    }                                                                                                        \
                                                                                                             \
    template<std::derived_from<ISink> Sink, typename... Args>                                                \
    inline auto level(detail::Immediate,                                                                     \
                      std::shared_ptr<Sink> sink,                                                            \
                      Color color,                                                                           \
                      LogFormatString<Args...> fmt,                                                          \
                      Args&&... args) noexcept -> void                                                       \
    {                                                                                                        \
        detail::log_message_to_sink<#level, true>(                                                           \
          std::static_pointer_cast<ISink>(std::move(sink)), color, fmt, std::forward<Args>(args)...);        \
    }                                                                                                        \
                                                                                                             \
    template<detail::SinkObject Sink, typename... Args>                                                      \
    inline auto level(                                                                                       \
      detail::Immediate, Sink&& sink, Color color, LogFormatString<Args...> fmt, Args&&... args) noexcept    \
      -> void                                                                                                \
    {                                                                                                        \
        detail::log_message_to_sink_object<#level, true>(                                                    \
          std::forward<Sink>(sink), color, fmt, std::forward<Args>(args)...);                                \
    }

    PNM_DEFINE_LOG_LEVEL(trace)
    PNM_DEFINE_LOG_LEVEL(debug)
    PNM_DEFINE_LOG_LEVEL(info)
    PNM_DEFINE_LOG_LEVEL(warn)
    PNM_DEFINE_LOG_LEVEL(error)
    PNM_DEFINE_LOG_LEVEL(critical)
}
