#include "platform/WindowProc.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdint>
#include <exception>
#include <stdexcept>

namespace {

    constexpr wchar_t kOwnerClass[] = L"IntarsiaWindowProcTest";

    struct OwnerState {
        std::exception_ptr error;
    };

    // Holds its window the way App holds its control window, and throws once the window exists, as App's constructor
    // can.
    class FailingOwner {
      public:
        FailingOwner(HWND& created, std::int32_t& messages, std::int32_t& messagesBeforeThrow) : _messages(messages), _window(Create(this)) {
            created             = _window.get();
            messagesBeforeThrow = _messages;
            throw std::runtime_error("failed after creating its window");
        }

      private:
        friend LRESULT CALLBACK ::OwnedWindowProc<FailingOwner>(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

        static HWND    Create(FailingOwner* owner) {
            return CreateWindowExW(0, kOwnerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), owner);
        }

        LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
            ++_messages;
            return DefWindowProcW(window, message, wParam, lParam);
        }

        std::int32_t&     _messages;
        OwnerState        _state;
        UniqueOwnedWindow _window;
    };

} // namespace

// A window left behind by a constructor that threw would call into the freed owner the next time any message loop ran.
TEST(WindowProc, AnOwnedWindowIsDestroyedWithoutItsOwnerWhenTheOwnerFailsToConstruct) {
    WNDCLASSEXW registration{};
    registration.cbSize        = sizeof(registration);
    registration.lpfnWndProc   = OwnedWindowProc<FailingOwner>;
    registration.hInstance     = GetModuleHandleW(nullptr);
    registration.lpszClassName = kOwnerClass;
    ASSERT_NE(RegisterClassExW(&registration), 0);

    HWND         created             = nullptr;
    std::int32_t messages            = 0;
    std::int32_t messagesBeforeThrow = 0;
    EXPECT_THROW(FailingOwner(created, messages, messagesBeforeThrow), std::runtime_error);
    ASSERT_NE(created, nullptr);
    EXPECT_FALSE(IsWindow(created));
    EXPECT_EQ(messages, messagesBeforeThrow) << "destroying the window reached its owner";
    UnregisterClassW(kOwnerClass, GetModuleHandleW(nullptr));
}
