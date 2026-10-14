#pragma once

// Its helpers define operator== on D3D11_RECT, which is RECT, so EXPECT_EQ compares RECTs and an optional<RECT> with a
// RECT. A second definition here would collide wherever d3d11.h is already included.
#include <d3d11.h>
