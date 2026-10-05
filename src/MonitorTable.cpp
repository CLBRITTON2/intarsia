#include "MonitorTable.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {

    constexpr size_t kColumns   = 5;
    constexpr size_t kColumnGap = 2;

    using Row = std::array<std::string, kColumns>;

    std::vector<Row> Rows(const std::vector<MonitorDescription>& monitors, const BarConfig& bar) {
        std::vector<Row> rows{Row{"id", "name", "connection", "primary", "bar"}};
        for (const MonitorDescription& monitor : monitors) {
            const std::string primary = monitor.primary ? "yes" : "no";
            const std::string shown   = ShowsBarOn(bar, monitor) ? "yes" : "no";
            if (monitor.targets.empty()) {
                rows.push_back(Row{"none", "none", "none", primary, shown});
            }
            for (const DisplayTarget& target : monitor.targets) {
                rows.push_back(Row{target.id.value_or("none"), target.name.value_or("none"), target.connection, primary, shown});
            }
        }
        return rows;
    }

} // namespace

std::string MonitorTable(const std::vector<MonitorDescription>& monitors, const BarConfig& bar) {
    const std::vector<Row>       rows = Rows(monitors, bar);
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
