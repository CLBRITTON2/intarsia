#include "platform/ComScope.hpp"

#include "platform/Errors.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <objbase.h>

#include <optional>
#include <string>
#include <thread>

namespace {

    // The calling thread's apartment, or nullopt outside COM.
    std::optional<APTTYPE> Apartment() {
        APTTYPE          type      = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        if (CoGetApartmentType(&type, &qualifier) == CO_E_NOTINITIALIZED) {
            return std::nullopt;
        }
        return type;
    }

} // namespace

// Each runs on a thread of its own, so no other test's apartment is in the way.
TEST(ComScope, EntersTheApartmentAndLeavesIt) {
    std::thread([] {
        {
            const ComScope single(ComThreading::Single, "test single");
            EXPECT_EQ(Apartment(), std::optional<APTTYPE>(APTTYPE_MAINSTA));
        }
        EXPECT_EQ(Apartment(), std::nullopt);
        {
            const ComScope multi(ComThreading::Multi, "test multi");
            EXPECT_EQ(Apartment(), std::optional<APTTYPE>(APTTYPE_MTA));
        }
        EXPECT_EQ(Apartment(), std::nullopt);
    }).join();
}

TEST(ComScope, RefusesTheOtherApartmentOnTheSameThread) {
    std::thread([] {
        const ComScope single(ComThreading::Single, "test single");
        try {
            const ComScope multi(ComThreading::Multi, "test multi");
            ADD_FAILURE() << "entered the multithreaded apartment from a single-threaded one";
        } catch (const Win32Error& error) {
            EXPECT_EQ(error.Result(), RPC_E_CHANGED_MODE);
            EXPECT_NE(std::string(error.what()).find("CoInitializeEx(test multi)"), std::string::npos);
        }
    }).join();
}
