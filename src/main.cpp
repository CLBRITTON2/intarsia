#include "App.hpp"
#include "config/Config.hpp"

#include <windows.h>

#include <cstdio>
#include <exception>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        App app(instance, DefaultConfigPath());
        return app.Run();
    } catch (const std::exception& error) {
        // stderr reaches a launching shell that redirected it, the message box covers a plain double-click.
        std::fprintf(stderr, "intarsia: %s\n", error.what());
        MessageBoxA(nullptr, error.what(), "intarsia", MB_ICONERROR | MB_OK);
        return 1;
    }
}
