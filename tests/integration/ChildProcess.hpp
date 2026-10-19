#pragma once

#include "platform/Handle.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace integration {

    // Killed with its job if the test ends without closing it.
    class ChildProcess {
      public:
        ChildProcess(const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::filesystem::path& stdoutPath,
                     const std::filesystem::path& stderrPath);

        std::uint32_t Id() const;
        // WM_CLOSE to every top-level window, hidden ones too, as taskkill without /F. nullopt if still running.
        std::optional<std::uint32_t> Close(std::chrono::milliseconds limit) const;
        void Kill() const;
        void WriteLine(std::string_view line) const;

      private:
        UniqueHandle _job;
        UniqueHandle _input;
        UniqueHandle _process;
        std::uint32_t _id;
    };

} // namespace integration
