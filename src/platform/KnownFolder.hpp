#pragma once

#include <windows.h>

#include <shlobj.h>

#include <filesystem>
#include <string_view>

// The folder's path for this user. name is the folder's id as the error names it, such as "FOLDERID_Profile".
std::filesystem::path KnownFolderPath(const KNOWNFOLDERID& folder, std::string_view name);
