#include "render/Graphics.hpp"

#include "platform/Errors.hpp"

#include <gtest/gtest.h>

TEST(OutputForMonitor, ThrowsMonitorGoneForAHandleNamingNoMonitor) {
    EXPECT_THROW(OutputForMonitor(nullptr), MonitorGoneError);
}
