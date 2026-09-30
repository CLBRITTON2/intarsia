// Test IPC server for the stress tests in scripts/stress.ps1 and the integration tests. It answers the bar's queries the
// way GlazeWM does, plus the load or faults a scenario names, and prints every command it receives to stdout on a line
// of its own. Built as glazewm.exe, the image name the bar's peer check requires.
//
//   glazewm.exe --port <n> --scenario <normal|flood|malformed|stall|flap|reject|integration>
//
// Under integration, a stdin line "focus <n>" focuses the nth listed window and sends the events a focus change does,
// then prints "pushed focus <n>" once they are sent.

#include "../platform/Loopback.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

    enum class Scenario : std::uint8_t {
        Normal,     // answers everything and sends an event a second
        Flood,      // answers everything and sends events as fast as the socket takes them
        Malformed,  // answers, then sends one kind of broken message per connection
        Stall,      // accepts the connection and never answers the upgrade
        Flap,       // answers, then drops the connection after a random moment
        Reject,     // answers every query with a failure
        Integration // answers everything, sends events only when stdin asks, and lists only the integration tests' own windows
    };

    // Focus changes read from stdin, waiting for a connection to send them on.
    class PushedFocus {
      public:
        void Add(size_t index) {
            const std::scoped_lock lock(_mutex);
            _pending.push_back(index);
        }

        std::optional<size_t> Next() {
            const std::scoped_lock lock(_mutex);
            if (_pending.empty()) {
                return std::nullopt;
            }
            const size_t index = _pending.front();
            _pending.pop_front();
            return index;
        }

      private:
        std::mutex         _mutex;
        std::deque<size_t> _pending;
    };

    void ReadPushedFocus(PushedFocus& pushed) {
        constexpr std::string_view kFocus = "focus ";
        std::string                line;
        while (std::getline(std::cin, line)) {
            if (!line.starts_with(kFocus)) {
                std::cerr << std::format("unknown stdin line '{}', use focus <n>", line) << std::endl;
                std::exit(1);
            }
            pushed.Add(std::stoul(line.substr(kFocus.size())));
        }
    }

    struct Options {
        std::uint16_t port;
        Scenario      scenario;
    };

    Scenario ParseScenario(std::string_view name) {
        constexpr std::array<std::pair<std::string_view, Scenario>, 7> kScenarios{{{"normal", Scenario::Normal},
                                                                                   {"flood", Scenario::Flood},
                                                                                   {"malformed", Scenario::Malformed},
                                                                                   {"stall", Scenario::Stall},
                                                                                   {"flap", Scenario::Flap},
                                                                                   {"reject", Scenario::Reject},
                                                                                   {"integration", Scenario::Integration}}};
        for (const auto& [known, scenario] : kScenarios) {
            if (known == name) {
                return scenario;
            }
        }
        throw std::invalid_argument(std::format("unknown scenario '{}', use normal, flood, malformed, stall, flap, reject or integration", name));
    }

    Options ParseOptions(std::int32_t argc, char** argv) {
        std::optional<std::uint16_t> port;
        std::optional<Scenario>      scenario;
        for (std::int32_t index = 1; index + 1 < argc; index += 2) {
            const std::string_view name = argv[index];
            if (name == "--port") {
                port = static_cast<std::uint16_t>(std::stoi(argv[index + 1]));
            } else if (name == "--scenario") {
                scenario = ParseScenario(argv[index + 1]);
            } else {
                throw std::invalid_argument(std::format("unknown option '{}'", name));
            }
        }
        if (!port.has_value() || !scenario.has_value()) {
            throw std::invalid_argument("usage: glazewm.exe --port <n> --scenario <name>");
        }
        return Options{.port = *port, .scenario = *scenario};
    }

    // Every window the integration tests open has a title starting with this (tests/integration/Harness.hpp).
    constexpr std::wstring_view kIntegrationTitle = L"intarsia integration";

    struct WindowCollection {
        bool              integrationOnly;
        std::vector<HWND> windows;
    };

    // Real top-level windows, so the bar's windows module reads real icons.
    BOOL CALLBACK CollectWindow(HWND window, LPARAM collection) {
        auto*                   collected = reinterpret_cast<WindowCollection*>(collection); // NOLINT(performance-no-int-to-ptr)
        std::array<wchar_t, 64> title{};
        GetWindowTextW(window, title.data(), static_cast<std::int32_t>(title.size()));
        const bool wanted = !collected->integrationOnly || std::wstring_view(title.data()).starts_with(kIntegrationTitle);
        if (IsWindowVisible(window) && GetWindowTextLengthW(window) > 0 && wanted && collected->windows.size() < 12) {
            collected->windows.push_back(window);
        }
        return TRUE;
    }

    std::string Uuid(size_t index) {
        return std::format("00000000-0000-4000-8000-{:012x}", index);
    }

    std::string Response(std::string_view clientMessage, std::string_view data) {
        return std::format(R"({{"messageType":"client_response","clientMessage":"{}","success":true,"error":null,"data":{}}})", clientMessage, data);
    }

    std::string Failure(std::string_view clientMessage) {
        return std::format(R"({{"messageType":"client_response","clientMessage":"{}","success":false,"error":"refused by the stress scenario","data":null}})", clientMessage);
    }

    std::string Event(std::string_view eventType) {
        return std::format(R"({{"messageType":"event_subscription","subscriptionId":"s","success":true,"error":null,"data":{{"eventType":"{}"}}}})", eventType);
    }

    // Two workspaces on the primary monitor, the displayed one holding real windows inside a split.
    std::string Monitors(size_t focused, bool integrationOnly) {
        const auto       monitor = reinterpret_cast<std::intptr_t>(MonitorFromPoint(POINT{.x = 0, .y = 0}, MONITOR_DEFAULTTOPRIMARY));
        WindowCollection collection{.integrationOnly = integrationOnly, .windows = {}};
        EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&collection));
        const std::vector<HWND>& windows = collection.windows;
        std::string              children;
        for (size_t index = 0; index < windows.size(); ++index) {
            children += std::format(R"({}{{"type":"window","id":"{}","handle":{},"hasFocus":{}}})", index == 0 ? "" : ",", Uuid(index),
                                    reinterpret_cast<std::intptr_t>(windows[index]), index == focused % std::max<size_t>(windows.size(), 1) ? "true" : "false");
        }
        return std::format(R"({{"monitors":[{{"type":"monitor","handle":{},"children":[)"
                           R"({{"type":"workspace","name":"1","displayName":null,"hasFocus":false,"isDisplayed":false,"children":[]}},)"
                           R"({{"type":"workspace","name":"2","displayName":"web","hasFocus":true,"isDisplayed":true,"children":[{{"type":"split","children":[{}]}}]}}]}}]}})",
                           monitor, children);
    }

    // What the bar must survive without crashing, one per connection: it drops the connection and retries each time.
    std::string Broken(size_t connection) {
        switch (connection % 7) {
            case 0: return "not json at all";
            case 1: return R"({"success":true})";
            case 2: return Response("query monitors", R"({"monitors":[{"type":"monitor","handle":"not a number","children":[]}]})");
            case 3: return Response("query monitors", std::string(100'000, '[') + std::string(100'000, ']'));
            case 4: return std::string(size_t{2} * 1024 * 1024, 'x');
            case 5: return "{\"messageType\":\"\xff\xfe\",\"success\":true}";
            default: return R"({"messageType":"client_response","clientMessage":"query monitors","success":true,"error":null,"data":{"monitors":"\ud800"}})";
        }
    }

    // The focused window, binding modes and tiling direction all follow the focus index, so a change sends all three.
    void SendFocusEvents(const loopback::Socket& peer) {
        for (const std::string_view eventType : {"focus_changed", "binding_modes_changed", "tiling_direction_changed"}) {
            loopback::SendText(peer, Event(eventType));
        }
    }

    // Serves one connection until the bar closes it or the scenario drops it.
    void Serve(const loopback::Socket& peer, Scenario scenario, size_t connection, PushedFocus& pushed) {
        if (scenario == Scenario::Stall) {
            loopback::ReceiveHeaders(peer);
            loopback::DrainUntilClosed(peer);
            return;
        }
        loopback::AnswerUpgrade(peer);

        std::mt19937 random(static_cast<std::uint32_t>(connection));
        const auto   dropAt     = std::chrono::steady_clock::now() + std::chrono::milliseconds(200 + (random() % 1800));
        auto         nextEvent  = std::chrono::steady_clock::now();
        size_t       focused    = 0;
        bool         sentBroken = false;
        while (true) {
            if (scenario == Scenario::Flap && std::chrono::steady_clock::now() >= dropAt) {
                return;
            }
            const std::chrono::milliseconds wait = scenario == Scenario::Flood ? std::chrono::milliseconds(0) : std::chrono::milliseconds(50);
            if (loopback::Readable(peer, wait)) {
                const std::optional<std::string> request = loopback::ReceiveText(peer);
                if (!request.has_value()) {
                    return;
                }
                const std::string_view text = *request;
                if (text.starts_with("command ")) {
                    std::cout << text << std::endl;
                }
                if (scenario == Scenario::Reject && text.starts_with("query ")) {
                    loopback::SendText(peer, Failure(text));
                } else if (text.starts_with("sub ")) {
                    loopback::SendText(peer, Response(text, R"({"subscriptionId":"s"})"));
                } else if (text == "query monitors") {
                    loopback::SendText(peer, Response(text, Monitors(focused, scenario == Scenario::Integration)));
                } else if (text == "query binding-modes") {
                    loopback::SendText(peer, Response(text, focused % 2 == 0 ? R"({"bindingModes":[]})" : R"({"bindingModes":[{"name":"resize","displayName":null}]})"));
                } else if (text == "query tiling-direction") {
                    loopback::SendText(peer, Response(text, std::format(R"({{"tilingDirection":"{}","directionContainer":{{}}}})", focused % 2 == 0 ? "horizontal" : "vertical")));
                } else {
                    loopback::SendText(peer, Response(text, R"({"subscriptionId":null})"));
                }
                continue;
            }
            if (scenario == Scenario::Malformed && !sentBroken) {
                loopback::SendText(peer, Broken(connection));
                sentBroken = true;
            }
            if (scenario == Scenario::Integration) {
                if (const std::optional<size_t> index = pushed.Next(); index.has_value()) {
                    focused = *index;
                    SendFocusEvents(peer);
                    std::cout << std::format("pushed focus {}", focused) << std::endl;
                }
            } else if (std::chrono::steady_clock::now() >= nextEvent) {
                ++focused;
                SendFocusEvents(peer);
                nextEvent = scenario == Scenario::Flood ? std::chrono::steady_clock::now() : std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }
    }

} // namespace

int main(int argc, char** argv) {
    try {
        const Options           options = ParseOptions(argc, argv);
        const loopback::Winsock winsock;
        const loopback::Socket  listener = loopback::ListenOn(options.port);
        std::cout << std::format("listening on 127.0.0.1:{}", options.port) << std::endl;
        PushedFocus pushed;
        if (options.scenario == Scenario::Integration) {
            // Ends with the process, blocked in getline while stdin stays open.
            std::thread(ReadPushedFocus, std::ref(pushed)).detach();
        }
        for (size_t connection = 0;; ++connection) {
            const loopback::Socket peer = loopback::Accept(listener);
            try {
                Serve(peer, options.scenario, connection, pushed);
            } catch (const std::exception& error) {
                // The bar dropping the connection mid-send is the point of most scenarios.
                std::cerr << std::format("connection {} ended: {}", connection, error.what()) << std::endl;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
