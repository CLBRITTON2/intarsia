#pragma once

// Its operator== on D3D11_RECT, which is RECT, serves EXPECT_EQ. A definition here would collide with it.
#include <d3d11.h>
