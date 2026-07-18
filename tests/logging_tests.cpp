#include "pneumo/logging.hpp"

// NOLINTBEGIN

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(PNM_PLATFORM_POSIX)
#include <sys/wait.h>
#endif

namespace
{
#if defined(PNM_PLATFORM_POSIX)
    std::filesystem::path g_test_binary;
#endif

    struct ThrowingLogValue
    {
    };

    class RecordingSink : public pnm::log::SinkBase<RecordingSink>
    {
      public:
        auto isOpen() const -> bool override { return true; }
        auto open() -> pnm::Result<> override { return {}; }
        auto close() -> pnm::Result<> override { return {}; }

        auto write(std::span<const std::byte> data) -> pnm::Result<size_t> override
        {
            writes.emplace_back(reinterpret_cast<const char*>(data.data()), data.size());
            return data.size();
        }

        auto flush() -> pnm::Result<> override
        {
            ++flushes;
            return {};
        }

        std::vector<std::string> writes;
        size_t flushes{ 0 };
    };

    class PartialWriteSink : public pnm::log::SinkBase<PartialWriteSink>
    {
      public:
        auto isOpen() const -> bool override { return true; }
        auto open() -> pnm::Result<> override { return {}; }
        auto close() -> pnm::Result<> override { return {}; }

        auto write(std::span<const std::byte> data) -> pnm::Result<size_t> override
        {
            const auto size{ std::min<size_t>(3, data.size()) };
            output.append(reinterpret_cast<const char*>(data.data()), size);
            ++writes;
            return size;
        }

        auto flush() -> pnm::Result<> override { return {}; }

        std::string output;
        size_t writes{};
    };

    class ThrowingSink : public pnm::log::SinkBase<ThrowingSink>
    {
      public:
        auto isOpen() const -> bool override { return true; }
        auto open() -> pnm::Result<> override { return {}; }
        auto close() -> pnm::Result<> override { return {}; }

        auto write(std::span<const std::byte>) -> pnm::Result<size_t> override
        {
            throw std::runtime_error{ "sink write failed" };
        }

        auto flush() -> pnm::Result<> override { return {}; }
    };

#if defined(PNM_PLATFORM_POSIX)
    auto shell_quote(std::string_view value) -> std::string
    {
        std::string quoted{ "'" };
        for (const auto ch : value) {
            if (ch == '\'') {
                quoted += "'\\''";
            }
            else {
                quoted += ch;
            }
        }
        quoted += "'";
        return quoted;
    }

    auto read_file(const std::filesystem::path& path) -> std::string
    {
        std::ifstream file{ path };
        return { std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
    }

    auto make_temp_log_path(std::string_view name) -> std::filesystem::path
    {
        const auto stamp{ std::chrono::steady_clock::now().time_since_epoch().count() };
        return std::filesystem::temp_directory_path() / std::format("pneumo-{}-{}.log", name, stamp);
    }
    auto write_text(pnm::log::ISink& sink, std::string_view text) -> pnm::Result<size_t>
    {
        return sink.write({ reinterpret_cast<const std::byte*>(text.data()), text.size() });
    }
#endif
}

template<>
struct std::formatter<ThrowingLogValue>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(const ThrowingLogValue&, std::format_context&) const -> std::format_context::iterator
    {
        throw std::runtime_error{ "format failed" };
    }
};

PNM_META_SOURCE_EMBED_CURRENT

int main(int argc, char* argv[])
{
#if defined(PNM_PLATFORM_POSIX)
    if (const auto* log_path{ std::getenv("PNM_LOGGING_ASYNC_DRAIN_FILE") }; log_path != nullptr) {
        pnm::log::info(
          pnm::log::file(log_path).sourceInfo(pnm::log::SourceField::FileName, pnm::log::SourceField::Line),
          "async-drain-probe {}",
          42);
        return 0;
    }

    g_test_binary = std::filesystem::absolute(argv[0]);
#endif

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(LoggingTests, InfoStdou) { SUCCEED(); }

TEST(LoggingTests, ThrowingFormatterDoesNotEscapeNoexceptLogApi)
{
    testing::internal::CaptureStderr();
    pnm::log::info(pnm::log::immediate, pnm::log::std_out, "{}", ThrowingLogValue{});
    const auto stderr_output{ testing::internal::GetCapturedStderr() };

    EXPECT_NE(stderr_output.find("pneumo logging: dropped"), std::string::npos);
}

TEST(LoggingTests, ExplicitStdOutSinkDoesNotAlsoUseDefaultSink)
{
    testing::internal::CaptureStdout();
    pnm::log::info(pnm::log::immediate, pnm::log::std_out, "explicit-route-only");
    const auto stdout_output{ testing::internal::GetCapturedStdout() };

    EXPECT_NE(stdout_output.find("explicit-route-only"), std::string::npos);
    EXPECT_EQ(stdout_output.find("logging_tests.cpp"), std::string::npos);
    EXPECT_EQ(std::ranges::count(stdout_output, '\n'), 1);
}

TEST(LoggingTests, PublicStdOutSinkConfiguresTheDefaultRoute)
{
    static_assert(requires {
        pnm::log::std_out->isOpen();
        pnm::log::std_out->flush();
        pnm::log::std_err->isOpen();
        pnm::log::std_err->flush();
    });

    const auto previous_error_sink{ pnm::log::get_default_sink(pnm::log::Level::Error) };
    ASSERT_TRUE(pnm::log::set_default_sink(pnm::log::Level::Error, pnm::log::std_out));
    pnm::log::std_out->minLevel(pnm::log::Level::Error);

    testing::internal::CaptureStdout();
    pnm::log::info(pnm::log::immediate, "filtered default stdout message");
    pnm::log::error(pnm::log::immediate, "visible default stdout message");
    const auto stdout_output{ testing::internal::GetCapturedStdout() };

    pnm::log::std_out->minLevel(pnm::log::Level::Trace);
    ASSERT_TRUE(pnm::log::set_default_sink(pnm::log::Level::Error, previous_error_sink));

    EXPECT_EQ(stdout_output.find("filtered default stdout message"), std::string::npos);
    EXPECT_NE(stdout_output.find("visible default stdout message"), std::string::npos);
}

TEST(LoggingTests, BuiltInDefaultSinksSplitNormalAndErrorLevels)
{
    pnm::log::remove_all_global_sinks();
    pnm::log::reset_default_sinks();

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    pnm::log::trace(pnm::log::immediate, "default trace route");
    pnm::log::warn(pnm::log::immediate, "default warning route");
    pnm::log::error(pnm::log::immediate, "default error route");
    pnm::log::critical(pnm::log::immediate, "default critical route");
    const auto stderr_output{ testing::internal::GetCapturedStderr() };
    const auto stdout_output{ testing::internal::GetCapturedStdout() };

    EXPECT_NE(stdout_output.find("default trace route"), std::string::npos);
    EXPECT_NE(stdout_output.find("default warning route"), std::string::npos);
    EXPECT_EQ(stdout_output.find("default error route"), std::string::npos);
    EXPECT_EQ(stdout_output.find("default critical route"), std::string::npos);
    EXPECT_EQ(stderr_output.find("default trace route"), std::string::npos);
    EXPECT_EQ(stderr_output.find("default warning route"), std::string::npos);
    EXPECT_NE(stderr_output.find("default error route"), std::string::npos);
    EXPECT_NE(stderr_output.find("default critical route"), std::string::npos);
}

TEST(LoggingTests, DefaultSinkCanBeSetForALevelRange)
{
    auto normal_sink{ std::make_shared<RecordingSink>() };
    auto error_sink{ std::make_shared<RecordingSink>() };

    ASSERT_TRUE(pnm::log::set_default_sink(pnm::log::Level::Trace, pnm::log::Level::Warn, normal_sink));
    ASSERT_TRUE(pnm::log::set_default_sink(pnm::log::Level::Error, pnm::log::Level::Critical, error_sink));

    pnm::log::trace(pnm::log::immediate, "custom trace route");
    pnm::log::warn(pnm::log::immediate, "custom warning route");
    pnm::log::error(pnm::log::immediate, "custom error route");
    pnm::log::critical(pnm::log::immediate, "custom critical route");

    EXPECT_EQ(pnm::log::get_default_sink(pnm::log::Level::Trace), normal_sink);
    ASSERT_TRUE(pnm::log::remove_default_sink(pnm::log::Level::Warn));
    pnm::log::warn(pnm::log::immediate, "removed warning route");

    pnm::log::reset_default_sinks();

    ASSERT_EQ(normal_sink->writes.size(), 2);
    EXPECT_NE(normal_sink->writes[0].find("custom trace route"), std::string::npos);
    EXPECT_NE(normal_sink->writes[1].find("custom warning route"), std::string::npos);
    ASSERT_EQ(error_sink->writes.size(), 2);
    EXPECT_NE(error_sink->writes[0].find("custom error route"), std::string::npos);
    EXPECT_NE(error_sink->writes[1].find("custom critical route"), std::string::npos);

    EXPECT_FALSE(pnm::log::set_default_sink(pnm::log::Level::Off, normal_sink));
    EXPECT_FALSE(pnm::log::set_default_sink(pnm::log::Level::Critical, pnm::log::Level::Trace, normal_sink));
}

TEST(LoggingTests, SourceMetadataCapturesTheCallSite)
{
    auto sink{ std::make_shared<RecordingSink>() };
    sink->sourceInfo(pnm::log::SourceField::FileName,
                     pnm::log::SourceField::Line,
                     pnm::log::SourceField::Column,
                     pnm::log::SourceField::Function);

    const auto expected_line{ __LINE__ + 1U };
    pnm::log::info(pnm::log::immediate, sink, "source metadata payload");

    ASSERT_EQ(sink->writes.size(), 1);
    const auto& output{ sink->writes.front() };
    const auto expected_location{ std::format("logging_tests.cpp:{}:", expected_line) };
    EXPECT_NE(output.find(expected_location), std::string::npos);
    EXPECT_NE(output.find("SourceMetadataCapturesTheCallSite"), std::string::npos);
    EXPECT_NE(output.find("source metadata payload"), std::string::npos);
}

TEST(LoggingTests, SourceMetadataCanPrintTheFullFilePath)
{
    auto sink{ std::make_shared<RecordingSink>() };
    sink->sourceInfo(pnm::log::SourceField::FilePath);

    pnm::log::info(pnm::log::immediate, sink, "full source path");

    ASSERT_EQ(sink->writes.size(), 1);
    EXPECT_NE(sink->writes.front().find(__FILE__), std::string::npos);
}

TEST(LoggingTests, SourceExcerptUsesEmbeddedSource)
{
    auto sink{ std::make_shared<RecordingSink>() };
    sink->sourceInfo(pnm::log::SourceField::FileName, pnm::log::SourceField::Line).sourceExcerpt(1);

    const auto expected_line{ __LINE__ + 1U };
    pnm::log::error(pnm::log::immediate, sink, "embedded source excerpt");

    ASSERT_EQ(sink->writes.size(), 1);
    const auto& output{ sink->writes.front() };
    EXPECT_NE(output.find(std::format("> {} |", expected_line)), std::string::npos);
    EXPECT_NE(output.find("pnm::log::error"), std::string::npos);
}

TEST(LoggingTests, SinkCanHideTheLevel)
{
    auto sink{ std::make_shared<RecordingSink>() };
    sink->showLevel(false);

    pnm::log::error(pnm::log::immediate, sink, "message without rendered severity");

    ASSERT_EQ(sink->writes.size(), 1);
    EXPECT_EQ(sink->writes.front().find("Error"), std::string::npos);
    EXPECT_NE(sink->writes.front().find("message without rendered severity"), std::string::npos);
}

TEST(LoggingTests, SinkCanUseACompileTimeCheckedTimestampFormat)
{
    auto sink{ std::make_shared<RecordingSink>() };
    sink->timestampFormat("timestamp={:%Y-%m-%d}");

    pnm::log::info(pnm::log::immediate, sink, "custom timestamp format");

    ASSERT_EQ(sink->writes.size(), 1);
    EXPECT_TRUE(sink->writes.front().starts_with("[timestamp="));
    EXPECT_NE(sink->writes.front().find("] Info: custom timestamp format"), std::string::npos);
}

TEST(LoggingTests, NullExplicitSinkDoesNotFallBackToGlobalSinks)
{
    const std::shared_ptr<RecordingSink> sink;

    testing::internal::CaptureStdout();
    pnm::log::info(pnm::log::immediate, sink, "must not reach stdout");
    const auto stdout_output{ testing::internal::GetCapturedStdout() };

    EXPECT_TRUE(stdout_output.empty());
}

TEST(LoggingTests, PartialSinkWritesAreCompleted)
{
    auto sink{ std::make_shared<PartialWriteSink>() };

    pnm::log::info(pnm::log::immediate, sink, "partial write payload");

    EXPECT_GT(sink->writes, 1);
    EXPECT_NE(sink->output.find("partial write payload"), std::string::npos);
    ASSERT_FALSE(sink->output.empty());
    EXPECT_EQ(sink->output.back(), '\n');
}

TEST(LoggingTests, ThrowingSinkDoesNotBlockLaterGlobalSinks)
{
    pnm::log::remove_all_global_sinks();
    auto throwing_sink{ std::make_shared<ThrowingSink>() };
    auto recording_sink{ std::make_shared<RecordingSink>() };
    const auto throwing_handle{ pnm::log::add_global_sink(throwing_sink) };
    const auto recording_handle{ pnm::log::add_global_sink(recording_sink) };

    testing::internal::CaptureStderr();
    pnm::log::info(pnm::log::immediate, "message after throwing sink");
    const auto stderr_output{ testing::internal::GetCapturedStderr() };

    pnm::log::remove_all_global_sinks();
    pnm::log::reset_default_sinks();

    EXPECT_TRUE(throwing_handle.valid());
    EXPECT_TRUE(recording_handle.valid());
    ASSERT_EQ(recording_sink->writes.size(), 1);
    EXPECT_NE(recording_sink->writes.front().find("message after throwing sink"), std::string::npos);
    EXPECT_NE(stderr_output.find("a sink dropped a message"), std::string::npos);
}

TEST(LoggingTests, SinkConfigurationAndPublicEnumsFollowNamingConventions)
{
    static_assert(std::same_as<pnm::log::FileMode, pnm::log::detail::FileMode>);

    auto sink{ std::make_shared<RecordingSink>() };
    sink->minLevel(pnm::log::Level::Error).flushOn(pnm::log::Level::Error);

    pnm::log::info(pnm::log::immediate, sink, "filtered message");
    pnm::log::error(pnm::log::immediate, sink, "visible message");

    ASSERT_EQ(sink->writes.size(), 1);
    EXPECT_NE(sink->writes.front().find("visible message"), std::string::npos);
    EXPECT_EQ(sink->flushes, 1);
}

TEST(LoggingTests, GlobalSinkHandlesAddAndRemoveRoutes)
{
    auto sink{ std::make_shared<RecordingSink>() };
    const auto handle{ pnm::log::add_global_sink(sink) };

    ASSERT_TRUE(handle.valid());
    testing::internal::CaptureStdout();
    pnm::log::info(pnm::log::immediate, "first global message");
    EXPECT_TRUE(pnm::log::remove_global_sink(handle));
    EXPECT_FALSE(pnm::log::remove_global_sink(handle));
    pnm::log::info(pnm::log::immediate, "second global message");
    (void)testing::internal::GetCapturedStdout();

    ASSERT_EQ(sink->writes.size(), 1);
    EXPECT_NE(sink->writes.front().find("first global message"), std::string::npos);
}

TEST(LoggingTests, RemoveAllGlobalSinksStopsAllRegisteredRoutes)
{
    auto first_sink{ std::make_shared<RecordingSink>() };
    auto second_sink{ std::make_shared<RecordingSink>() };

    pnm::log::remove_all_global_sinks();
    const auto first_handle{ pnm::log::add_global_sink(first_sink) };
    const auto second_handle{ pnm::log::add_global_sink(second_sink) };
    pnm::log::remove_all_global_sinks();
    pnm::log::info(pnm::log::immediate, "message without routes");
    pnm::log::reset_default_sinks();

    EXPECT_TRUE(first_handle.valid());
    EXPECT_TRUE(second_handle.valid());
    EXPECT_TRUE(first_sink->writes.empty());
    EXPECT_TRUE(second_sink->writes.empty());
}

TEST(LoggingTests, SinkMinLevelFiltersMessages)
{
    auto sink{ std::make_shared<RecordingSink>() };
    sink->minLevel(pnm::log::Level::Error);

    testing::internal::CaptureStdout();
    pnm::log::info(pnm::log::immediate, sink, "filtered");
    pnm::log::error(pnm::log::immediate, sink, "visible");
    (void)testing::internal::GetCapturedStdout();

    ASSERT_EQ(sink->writes.size(), 1);
    EXPECT_NE(sink->writes.front().find("visible"), std::string::npos);
}

TEST(LoggingTests, SinkFlushOnFlushesAtOrAboveConfiguredLevel)
{
    auto sink{ std::make_shared<RecordingSink>() };
    sink->flushOn(pnm::log::Level::Warn);

    testing::internal::CaptureStdout();
    pnm::log::info(pnm::log::immediate, sink, "not flushed");
    pnm::log::warn(pnm::log::immediate, sink, "flushed");
    pnm::log::error(pnm::log::immediate, sink, "also flushed");
    (void)testing::internal::GetCapturedStdout();

    EXPECT_EQ(sink->writes.size(), 3);
    EXPECT_EQ(sink->flushes, 2);
}

TEST(LoggingTests, FileBuilderPatternAppliesLevelModeAndFlush)
{
#if !defined(PNM_PLATFORM_POSIX)
    GTEST_SKIP() << "temporary file probe currently uses POSIX-friendly filesystem behavior";
#else
    const auto log_path{ make_temp_log_path("file-builder") };

    testing::internal::CaptureStdout();
    pnm::log::info(pnm::log::immediate,
                   pnm::log::file(log_path)
                     .minLevel(pnm::log::Level::Error)
                     .flushOn(pnm::log::Level::Error)
                     .mode(pnm::log::detail::FileMode::Overwrite),
                   "filtered file message");
    pnm::log::error(pnm::log::immediate,
                    pnm::log::file(log_path)
                      .minLevel(pnm::log::Level::Error)
                      .flushOn(pnm::log::Level::Error)
                      .mode(pnm::log::detail::FileMode::Overwrite),
                    "visible file message");
    (void)testing::internal::GetCapturedStdout();

    const auto file_contents{ read_file(log_path) };
    EXPECT_EQ(file_contents.find("filtered file message"), std::string::npos);
    EXPECT_NE(file_contents.find("visible file message"), std::string::npos);

    std::filesystem::remove(log_path);
#endif
}

TEST(LoggingTests, FileSinkCloseReleasesCachedHandle)
{
#if !defined(PNM_PLATFORM_POSIX)
    GTEST_SKIP() << "temporary file probe currently uses POSIX-friendly filesystem behavior";
#else
    const auto log_path{ make_temp_log_path("file-close") };

    auto sink{ pnm::log::file(log_path).mode(pnm::log::detail::FileMode::Overwrite) };
    ASSERT_TRUE(sink.open());
    ASSERT_TRUE(sink.open());
    ASSERT_TRUE(write_text(sink, "first\n"));
    ASSERT_TRUE(sink.close());
    ASSERT_TRUE(sink.close());

    auto overwrite_sink{ pnm::log::file(log_path).mode(pnm::log::detail::FileMode::Overwrite) };
    ASSERT_TRUE(overwrite_sink.open());
    ASSERT_TRUE(write_text(overwrite_sink, "second\n"));
    ASSERT_TRUE(overwrite_sink.close());

    const auto file_contents{ read_file(log_path) };
    EXPECT_EQ(file_contents.find("first"), std::string::npos);
    EXPECT_NE(file_contents.find("second"), std::string::npos);

    std::filesystem::remove(log_path);
#endif
}

TEST(LoggingTests, AsyncBackendDrainsOnProcessExit)
{
#if !defined(PNM_PLATFORM_POSIX)
    GTEST_SKIP() << "child-process shutdown probe currently uses POSIX process status";
#else
    const auto log_path{ make_temp_log_path("async-drain") };
    const auto command{ std::format("PNM_LOGGING_ASYNC_DRAIN_FILE={} {}",
                                    shell_quote(log_path.string()),
                                    shell_quote(g_test_binary.string())) };

    const auto status{ std::system(command.c_str()) };
    ASSERT_NE(status, -1);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    const auto file_contents{ read_file(log_path) };
    EXPECT_NE(file_contents.find("async-drain-probe 42"), std::string::npos);
    EXPECT_NE(file_contents.find("logging_tests.cpp"), std::string::npos);

    std::filesystem::remove(log_path);
#endif
}

// NOLINTEND
