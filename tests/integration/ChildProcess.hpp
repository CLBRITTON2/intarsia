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

    // A child process with stdin on a pipe and stdout and stderr in files, killed with its job if the test ends without
    // closing it.
    class ChildProcess {
      public:
        ChildProcess(const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::filesystem::path& stdoutPath,
                     const std::filesystem::path& stderrPath);

        std::uint32_t Id() const;
        // Posts WM_CLOSE to every top-level window of the process, hidden ones included, as taskkill without /F does,
        // then waits for it to exit. The exit code, or nullopt when it is still running after limit.
        std::optional<std::uint32_t> Close(std::chrono::milliseconds limit) const;
        void                         Kill() const;
        void                         WriteLine(std::string_view line) const;

      private:
        UniqueHandle  _job;
        UniqueHandle  _input;
        UniqueHandle  _process;
        std::uint32_t _id;
    };

} // namespace integration
