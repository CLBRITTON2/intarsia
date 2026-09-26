// Test IPC server for the stress tests in scripts/smoke.ps1. It answers the bar's queries the way GlazeWM does, plus
// the load or faults a scenario names. Built as glazewm.exe, the image name the bar's peer check requires.
//
//   glazewm.exe --port <n> --scenario <normal|flood|malformed|stall|flap|reject>

#include "../platform/Loopback.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

    enum class Scenario : std::uint8_t {
        Normal,    // answers everything and sends an event a second
        Flood,     // answers everything and sends events as fast as the socket takes them
        Malformed, // answers, then sends one kind of broken message per connection
        Stall,     // accepts the connection and never answers the upgrade
        Flap,      // answers, then drops the connection after a random moment
        Reject     // answers every query with a failure
    };

    struct Options {
        USHORT   port;
        Scenario scenario;
    };

    Scenario ParseScenario(std::string_view name) {
        constexpr std::array<std::pair<std::string_view, Scenario>, 6> kScenarios{{{"normal", Scenario::Normal},
                                                                                   {"flood", Scenario::Flood},
                                                                                   {"malformed", Scenario::Malformed},
                                                                                   {"stall", Scenario::Stall},
                                                                                   {"flap", Scenario::Flap},
                                                                                   {"reject", Scenario::Reject}}};
        for (const auto& [known, scenario] : kScenarios) {
            if (known == name) {
                return scenario;
            }
        }
        throw std::invalid_argument(std::format("unknown scenario '{}', use normal, flood, malformed, stall, flap or reject", name));
    }

    Options ParseOptions(int argc, char** argv) {
        std::optional<USHORT>   port;
        std::optional<Scenario> scenario;
        for (int index = 1; index + 1 < argc; index += 2) {
            const std::string_view name = argv[index];
            if (name == "--port") {
                port = static_cast<USHORT>(std::stoi(argv[index + 1]));
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

    // Real top-level windows, so the bar's windows module reads real icons.
    BOOL CALLBACK CollectWindow(HWND window, LPARAM windows) {
        auto* collected = reinterpret_cast<std::vector<HWND>*>(windows); // NOLINT(performance-no-int-to-ptr)
        if (IsWindowVisible(window) && GetWindowTextLengthW(window) > 0 && collected->size() < 12) {
            collected->push_back(window);
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
        return std::format(R"({{"messageType":"client_response","clientMessage":"{}","success":false,"error":"refused by the smoke scenario","data":null}})", clientMessage);
    }

    std::string Event(std::string_view eventType) {
        return std::format(R"({{"messageType":"event_subscription","subscriptionId":"s","success":true,"error":null,"data":{{"eventType":"{}"}}}})", eventType);
    }

    // Two workspaces on the primary monitor, the displayed one holding real windows inside a split.
    std::string Monitors(size_t focused) {
        const auto        monitor = reinterpret_cast<std::intptr_t>(MonitorFromPoint(POINT{.x = 0, .y = 0}, MONITOR_DEFAULTTOPRIMARY));
        std::vector<HWND> windows;
        EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&windows));
        std::string children;
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

    // Serves one connection until the bar closes it or the scenario drops it.
    void Serve(const loopback::Socket& peer, Scenario scenario, size_t connection) {
        if (scenario == Scenario::Stall) {
            loopback::ReceiveHeaders(peer);
            loopback::DrainUntilClosed(peer);
            return;
        }
        loopback::AnswerUpgrade(peer);

        std::mt19937 random(static_cast<unsigned>(connection));
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
                if (scenario == Scenario::Reject && text.starts_with("query ")) {
                    loopback::SendText(peer, Failure(text));
                } else if (text.starts_with("sub ")) {
                    loopback::SendText(peer, Response(text, R"({"subscriptionId":"s"})"));
                } else if (text == "query monitors") {
                    loopback::SendText(peer, Response(text, Monitors(focused)));
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
            if (std::chrono::steady_clock::now() >= nextEvent) {
                ++focused;
                for (const std::string_view eventType : {"focus_changed", "binding_modes_changed", "tiling_direction_changed"}) {
                    loopback::SendText(peer, Event(eventType));
                }
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
        for (size_t connection = 0;; ++connection) {
            const loopback::Socket peer = loopback::Accept(listener);
            try {
                Serve(peer, options.scenario, connection);
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
