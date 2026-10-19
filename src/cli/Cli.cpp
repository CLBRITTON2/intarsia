#include "cli/Cli.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

    constexpr size_t kColumns = 5;
    constexpr size_t kColumnGap = 2;

    using Row = std::array<std::string, kColumns>;

    // Windows takes the friendly name from the monitor's own EDID, so an escape sequence there must not reach the
    // user's terminal. The id is already printable ASCII (platform/Monitors).
    std::string PrintableName(const DisplayTarget& target) {
        if (!target.name.has_value()) {
            return "none";
        }
        return ToUtf8(DrawableText(std::string_view(*target.name)));
    }

    std::string YesOrNo(bool value) {
        if (value) {
            return "yes";
        }
        return "no";
    }

    std::vector<Row> Rows(const std::vector<MonitorDescription>& descriptions, const BarConfig& bar) {
        std::vector<Row> rows{Row{"id", "name", "connection", "primary", "bar"}};
        for (const MonitorDescription& description : descriptions) {
            const std::string primaryCell = YesOrNo(description.primary);
            const std::string barCell = YesOrNo(ShowsBarOn(bar, description));
            if (description.targets.empty()) {
                rows.push_back(Row{"none", "none", "none", primaryCell, barCell});
            }
            for (const DisplayTarget& target : description.targets) {
                rows.push_back(Row{target.id.value_or("none"), PrintableName(target), target.connection, primaryCell, barCell});
            }
        }
        return rows;
    }

    CliCommand ParseCommand(const std::wstring& name) {
        if (name == L"check") {
            return CliCommand::Check;
        }
        if (name == L"monitors") {
            return CliCommand::Monitors;
        }
        throw CliOptionsError(std::format("unknown command '{}', intarsia-cli takes check or monitors", ToUtf8(name)));
    }

} // namespace

CliOptions ParseCliOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath) {
    if (arguments.empty()) {
        throw CliOptionsError("intarsia-cli needs a command");
    }
    CliOptions options{.command = ParseCommand(arguments[0]), .configPath = defaultConfigPath};
    for (size_t index = 1; index < arguments.size(); index += 2) {
        const std::wstring& name = arguments[index];
        if (name != L"--config") {
            throw CliOptionsError(std::format("unknown option '{}', {} takes --config <path>", ToUtf8(name), ToUtf8(arguments[0])));
        }
        if (index + 1 >= arguments.size()) {
            throw CliOptionsError("--config needs a value");
        }
        options.configPath = arguments[index + 1];
    }
    return options;
}

std::string MonitorTable(const std::vector<MonitorDescription>& descriptions, const BarConfig& bar) {
    const std::vector<Row> rows = Rows(descriptions, bar);
    std::array<size_t, kColumns> widths{};
    for (const Row& row : rows) {
        for (size_t column = 0; column < kColumns; ++column) {
            widths[column] = std::max(widths[column], row[column].size());
        }
    }
    std::string table;
    for (const Row& row : rows) {
        std::string line;
        for (size_t column = 0; column < kColumns; ++column) {
            line += row[column];
            if (column + 1 < kColumns) {
                line.append(widths[column] - row[column].size() + kColumnGap, ' ');
            }
        }
        table += line + "\n";
    }
    return table;
}
