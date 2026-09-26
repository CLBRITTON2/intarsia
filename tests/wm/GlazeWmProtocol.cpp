#include "wm/GlazeWmProtocol.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

    // Trimmed from a real "query monitors" response from GlazeWM 3.10.
    constexpr std::string_view kMonitorsResponse = R"json({
        "messageType": "client_response", "clientMessage": "query monitors", "success": true, "error": null,
        "data": { "monitors": [
            { "type": "monitor", "handle": 65537, "deviceName": "\\\\.\\DISPLAY1", "children": [
                { "type": "workspace", "name": "1", "displayName": null, "hasFocus": false, "isDisplayed": false, "children": [] },
                { "type": "workspace", "name": "5", "displayName": "web", "hasFocus": true, "isDisplayed": true, "children": [] }
            ] },
            { "type": "monitor", "handle": 131073, "deviceName": "\\\\.\\DISPLAY2", "children": [
                { "type": "workspace", "name": "9", "displayName": "", "hasFocus": false, "isDisplayed": true, "children": [] }
            ] }
        ] }
    })json";

    constexpr char             kFirstWindowId[] = "0b7c6a8e-5f0e-4c1a-9d3b-2a1f0e9c8b7a";

    // Windows directly under a displayed workspace and inside a split, and one on a hidden workspace the bar skips.
    constexpr std::string_view kWindowsResponse = R"json({
        "messageType": "client_response", "clientMessage": "query monitors", "success": true, "error": null,
        "data": { "monitors": [
            { "type": "monitor", "handle": 65537, "children": [
                { "type": "workspace", "name": "1", "displayName": null, "hasFocus": false, "isDisplayed": false, "children": [
                    { "type": "window", "id": "11111111-1111-1111-1111-111111111111", "handle": 9999, "hasFocus": false }
                ] },
                { "type": "workspace", "name": "2", "displayName": null, "hasFocus": true, "isDisplayed": true, "children": [
                    { "type": "window", "id": "0b7c6a8e-5f0e-4c1a-9d3b-2a1f0e9c8b7a", "handle": 1001, "hasFocus": false },
                    { "type": "split", "children": [
                        { "type": "window", "id": "22222222-2222-2222-2222-222222222222", "handle": 1002, "hasFocus": true },
                        { "type": "window", "id": "33333333-3333-3333-3333-333333333333", "handle": 1003, "hasFocus": false }
                    ] }
                ] }
            ] }
        ] }
    })json";

} // namespace

TEST(GlazeWmProtocol, ParsesAResponse) {
    const glazewm::ServerMessage message = glazewm::ParseServerMessage(kMonitorsResponse);
    EXPECT_EQ(message.kind, glazewm::MessageKind::Response);
    EXPECT_EQ(message.clientMessage, "query monitors");
    EXPECT_TRUE(message.success);
}

TEST(GlazeWmProtocol, ParsesAnEvent) {
    const glazewm::ServerMessage message = glazewm::ParseServerMessage(
        R"({"messageType":"event_subscription","subscriptionId":"abc","success":true,"error":null,"data":{"eventType":"workspace_activated","activatedWorkspace":{}}})");
    EXPECT_EQ(message.kind, glazewm::MessageKind::Event);
    EXPECT_EQ(message.eventType, "workspace_activated");
}

TEST(GlazeWmProtocol, KeepsTheErrorOfAFailedResponse) {
    const glazewm::ServerMessage message =
        glazewm::ParseServerMessage(R"({"messageType":"client_response","clientMessage":"query nope","success":false,"error":"unrecognized subcommand","data":null})");
    EXPECT_FALSE(message.success);
    EXPECT_EQ(message.error, "unrecognized subcommand");
}

TEST(GlazeWmProtocol, RejectsTextThatIsNotAServerMessage) {
    EXPECT_THROW(glazewm::ParseServerMessage("not json"), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::ParseServerMessage(R"({"success":true})"), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::ParseServerMessage(R"({"messageType":"mystery","success":true})"), glazewm::ProtocolError);
}

TEST(GlazeWmProtocol, WorkspacesCarryTheirMonitorAndPreferTheDisplayName) {
    const std::vector<Workspace> workspaces = glazewm::WorkspacesFromMonitors(glazewm::ParseServerMessage(kMonitorsResponse).data);
    ASSERT_EQ(workspaces.size(), 3u);
    EXPECT_EQ(workspaces[0].label, L"1");
    EXPECT_EQ(workspaces[0].monitor, 65537);
    EXPECT_FALSE(workspaces[0].focused);
    EXPECT_EQ(workspaces[1].label, L"web");
    EXPECT_EQ(workspaces[1].name, "5");
    EXPECT_TRUE(workspaces[1].focused);
    // An empty display name falls back to the name, as a null one does.
    EXPECT_EQ(workspaces[2].label, L"9");
    EXPECT_EQ(workspaces[2].monitor, 131073);
}

TEST(GlazeWmProtocol, OnlyWorkspaceAndFocusEventsTriggerARequery) {
    EXPECT_TRUE(glazewm::ChangesWorkspaces("focus_changed"));
    EXPECT_TRUE(glazewm::ChangesWorkspaces("workspace_deactivated"));
    EXPECT_TRUE(glazewm::ChangesWorkspaces("monitor_removed"));
    EXPECT_FALSE(glazewm::ChangesWorkspaces("binding_modes_changed"));
    EXPECT_FALSE(glazewm::ChangesWorkspaces("user_config_changed"));
}

TEST(GlazeWmProtocol, SubscribesToExactlyTheWorkspaceEvents) {
    EXPECT_EQ(glazewm::SubscribeCommand(),
              "sub --events focus_changed workspace_activated workspace_deactivated workspace_updated monitor_added monitor_removed monitor_updated window_managed "
              "window_unmanaged focused_container_moved binding_modes_changed tiling_direction_changed");
}

TEST(GlazeWmProtocol, BindingModesPreferTheDisplayName) {
    const glazewm::ServerMessage message =
        glazewm::ParseServerMessage(R"({"messageType":"client_response","clientMessage":"query binding-modes","success":true,"error":null,"data":{"bindingModes":[)"
                                    R"({"name":"resize","displayName":null,"keybindings":[]},{"name":"pause","displayName":"Paused","keybindings":[]}]}})");
    EXPECT_EQ(glazewm::BindingModesFromResponse(message.data), (std::vector<std::wstring>{L"resize", L"Paused"}));
}

TEST(GlazeWmProtocol, TilingDirectionIsHorizontalOrVertical) {
    const auto direction = [](std::string_view value) {
        return glazewm::TilingDirectionFromResponse(nlohmann::json{{"tilingDirection", value}, {"directionContainer", nlohmann::json::object()}});
    };
    EXPECT_EQ(direction("horizontal"), TilingDirection::Horizontal);
    EXPECT_EQ(direction("vertical"), TilingDirection::Vertical);
    EXPECT_THROW(direction("diagonal"), glazewm::ProtocolError);
}

TEST(GlazeWmProtocol, FocusAndDirectionEventsRequeryTheDirection) {
    EXPECT_TRUE(glazewm::ChangesTilingDirection("tiling_direction_changed"));
    EXPECT_TRUE(glazewm::ChangesTilingDirection("focus_changed"));
    EXPECT_TRUE(glazewm::ChangesTilingDirection("focused_container_moved"));
    EXPECT_FALSE(glazewm::ChangesTilingDirection("binding_modes_changed"));
    EXPECT_FALSE(glazewm::ChangesWorkspaces("binding_modes_changed"));
}

TEST(GlazeWmProtocol, WindowsComeFromDisplayedWorkspacesInTreeOrderThroughSplits) {
    const std::vector<ManagedWindow> windows = glazewm::WindowsFromMonitors(glazewm::ParseServerMessage(kWindowsResponse).data);
    ASSERT_EQ(windows.size(), 3u);
    EXPECT_EQ(windows[0].id, kFirstWindowId);
    EXPECT_EQ(windows[0].handle, 1001);
    EXPECT_FALSE(windows[0].focused);
    EXPECT_EQ(windows[1].handle, 1002);
    EXPECT_TRUE(windows[1].focused);
    EXPECT_EQ(windows[2].handle, 1003);
    EXPECT_EQ(windows[2].monitor, 65537);
}

TEST(GlazeWmProtocol, AWorkspaceHoldingAnUnknownContainerIsAProtocolError) {
    const std::string response = std::string(kWindowsResponse).replace(std::string(kWindowsResponse).find("\"split\""), 7, "\"mystery\"");
    EXPECT_THROW(glazewm::WindowsFromMonitors(glazewm::ParseServerMessage(response).data), glazewm::ProtocolError);
}

TEST(GlazeWmProtocol, DeeplyNestedSplitsDoNotOverflowTheStack) {
    std::string nested = R"({"type":"window","id":"x","handle":1,"hasFocus":false})";
    for (int depth = 0; depth < 20'000; ++depth) {
        nested = R"({"type":"split","children":[)" + nested + "]}";
    }
    const std::string response =
        R"({"messageType":"client_response","clientMessage":"query monitors","success":true,"error":null,"data":{"monitors":[{"type":"monitor","handle":1,)"
        R"("children":[{"type":"workspace","name":"1","isDisplayed":true,"children":[)" +
        nested + "]}]}]}}";
    EXPECT_EQ(glazewm::WindowsFromMonitors(glazewm::ParseServerMessage(response).data).size(), 1u);
}

TEST(GlazeWmProtocol, CommandMessageSendsAConfigCommandAsWritten) {
    EXPECT_EQ(glazewm::CommandMessage("focus --next-workspace"), "command focus --next-workspace");
    EXPECT_TRUE(glazewm::IsCommand(glazewm::CommandMessage("focus --next-workspace")));
    EXPECT_THROW(glazewm::CommandMessage(""), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::CommandMessage("focus\n--workspace 2"), glazewm::ProtocolError);
}

TEST(GlazeWmProtocol, FocusWindowCommandAddressesTheContainerById) {
    EXPECT_EQ(glazewm::FocusWindowCommand(kFirstWindowId), std::string("command focus --container-id ") + kFirstWindowId);
    EXPECT_TRUE(glazewm::IsCommand(glazewm::FocusWindowCommand(kFirstWindowId)));
}

TEST(GlazeWmProtocol, FocusWindowCommandRefusesIdsThatAreNotUuids) {
    EXPECT_THROW(glazewm::FocusWindowCommand(""), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::FocusWindowCommand("0b7c6a8e-5f0e-4c1a-9d3b-2a1f0e9c8b7a --workspace 2"), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::FocusWindowCommand("0b7c6a8e 5f0e-4c1a-9d3b-2a1f0e9c8b7a"), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::FocusWindowCommand("0b7c6a8e-5f0e-4c1a-9d3b-2a1f0e9c8b7g"), glazewm::ProtocolError);
}

TEST(GlazeWmProtocol, ErrorsQuoteOnlyAShortEscapedExcerpt) {
    const std::string hostile = "\"\n" + std::string(10'000, 'x');
    try {
        glazewm::ParseServerMessage(hostile);
        FAIL() << "expected ProtocolError";
    } catch (const glazewm::ProtocolError& error) {
        const std::string message = error.what();
        EXPECT_LT(message.size(), 400u);
        EXPECT_EQ(message.find('\n'), std::string::npos);
        EXPECT_NE(message.find("\\\"\\x0a"), std::string::npos) << message;
    }
}

TEST(GlazeWmProtocol, GateSendsTheFirstQueryAndHoldsTheRestOfABurst) {
    glazewm::QueryGate gate;
    EXPECT_TRUE(gate.RequestQuery());
    EXPECT_FALSE(gate.RequestQuery());
    EXPECT_FALSE(gate.RequestQuery());
    // Events arrived while the query was out, so exactly one more goes out.
    EXPECT_TRUE(gate.Answered());
    EXPECT_FALSE(gate.Answered());
    EXPECT_TRUE(gate.RequestQuery());
}

TEST(GlazeWmProtocol, FocusCommandAddressesTheWorkspaceByName) {
    EXPECT_EQ(glazewm::FocusWorkspaceCommand("5"), "command focus --workspace 5");
    EXPECT_TRUE(glazewm::IsCommand(glazewm::FocusWorkspaceCommand("5")));
    EXPECT_FALSE(glazewm::IsCommand(glazewm::kQueryMonitors));
}

TEST(GlazeWmProtocol, FocusCommandRefusesNamesThatWouldSplitIntoMoreArguments) {
    EXPECT_THROW(glazewm::FocusWorkspaceCommand("1 --workspace 2"), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::FocusWorkspaceCommand("1\tclose"), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::FocusWorkspaceCommand("1\n"), glazewm::ProtocolError);
    EXPECT_THROW(glazewm::FocusWorkspaceCommand(""), glazewm::ProtocolError);
}

TEST(GlazeWmProtocol, GateSendsNothingMoreWhenTheBurstIsOver) {
    glazewm::QueryGate gate;
    EXPECT_TRUE(gate.RequestQuery());
    EXPECT_FALSE(gate.Answered());
    EXPECT_TRUE(gate.RequestQuery());
}
