#include "platform/KnownFolder.hpp"

#include <gtest/gtest.h>

#include <filesystem>

TEST(KnownFolder, FindsThisUsersProfile) {
    const std::filesystem::path profile = KnownFolderPath(FOLDERID_Profile, "FOLDERID_Profile");
    EXPECT_TRUE(profile.is_absolute());
    EXPECT_TRUE(std::filesystem::is_directory(profile));
}
