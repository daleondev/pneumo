#include "pneumo/logging.hpp"

#include <atomic>
#include <memory>

// NOLINTBEGIN

PNM_META_SOURCE_EMBED_CURRENT

class MyCustomSink : public pnm::log::SinkBase<MyCustomSink>
{
  public:
    auto isOpen() const -> bool override { return m_isOpen.load(); }

    auto open() -> pnm::Result<> override
    {
        m_isOpen.store(true);
        return {};
    }

    auto close() -> pnm::Result<> override
    {
        m_isOpen.store(false);
        return {};
    }

    auto write(std::span<const std::byte> data) -> pnm::Result<size_t> override
    {
        // Forward the bytes to any user-owned transport here: serial, network, flash, etc.
        m_bytesWritten.fetch_add(data.size());
        return data.size();
    }

    auto flush() -> pnm::Result<> override { return {}; }

  private:
    std::atomic_bool m_isOpen{};
    std::atomic_size_t m_bytesWritten{};
};

int main()
{
    pnm::log::initialize();

    pnm::log::std_out->sourceInfo(pnm::log::SourceField::FileName, pnm::log::SourceField::Line)
      .timestampFormat("{:%Y-%m-%d %H:%M:%S}");
    pnm::log::std_err
      ->sourceInfo(
        pnm::log::SourceField::FileName, pnm::log::SourceField::Line, pnm::log::SourceField::Function)
      .showLevel(false);

    // These are the built-in defaults and can be replaced with any user-defined sink.
    pnm::log::set_default_sink(pnm::log::Level::Trace, pnm::log::Level::Warn, pnm::log::std_out);
    pnm::log::set_default_sink(pnm::log::Level::Error, pnm::log::Level::Critical, pnm::log::std_err);

    pnm::log::trace("Hello {}", 6);

    pnm::log::info(pnm::log::std_out, "Hello {}", 6);
    pnm::log::info(pnm::log::file("debug.log")
                     .flushOn(pnm::log::Level::Error)
                     .sourceInfo(pnm::log::SourceField::FileName, pnm::log::SourceField::Line)
                     .sourceExcerpt(1)
                     .mode(pnm::log::detail::FileMode::Overwrite),
                   "Hello {}",
                   6);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto handle = pnm::log::add_global_sink(pnm::log::file("debug.log")
                                              .mode(pnm::log::FileMode::Append)
                                              .flushOn(pnm::log::Level::Error)
                                              .sourceInfo(pnm::log::SourceField::FilePath,
                                                          pnm::log::SourceField::Line,
                                                          pnm::log::SourceField::Function));

    auto dummy{ 50 };
    auto value{ 12.5 };
    auto custom_sink{ std::make_shared<MyCustomSink>() };

    pnm::log::info("Started");

    pnm::log::info("Dummy number is {}", dummy);
    pnm::log::error("Error occurred with dummy id: {}", dummy);

    pnm::log::info(pnm::log::std_err, "Forced to stderr: {}", dummy);

    pnm::log::info(pnm::log::immediate, "Written synchronously: {}", dummy);

    pnm::log::info(custom_sink, "Written asynchronously to a custom sink: {}", dummy);
    pnm::log::info(pnm::log::immediate, custom_sink, "Written synchronously to a custom sink: {}", dummy);

    auto custom_handle{ pnm::log::add_global_sink(custom_sink) };
    pnm::log::info("Also routed through the custom global sink: {}", dummy);

    pnm::log::info(pnm::log::file("measurements.log"),
                   "Value = {}",
                   value); // cache already open files in backend, dont reopen every time

    pnm::log::info(pnm::log::immediate, pnm::log::file("debug.log"), "Immediate file write: {}", value);

    pnm::log::remove_global_sink(custom_handle);
    pnm::log::remove_global_sink(handle);
    pnm::log::remove_all_global_sinks();

    // Ensure you heavily constrain the tags and sinks with concepts so the compiler can easily isolate
    // "routing arguments" from "formatting arguments"

    return 0;
}

// NOLINTEND
