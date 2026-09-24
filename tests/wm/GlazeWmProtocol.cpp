#include "wm/GlazeWmProtocol.hpp"

#include <gtest/gtest.h>

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
              "window_unmanaged");
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
