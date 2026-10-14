#pragma once

#include "platform/Handle.hpp"

#include <windows.h>

#include <pdh.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// A failed PDH call: the message names the call and carries PDH's status and its text.
class PdhError : public std::runtime_error {
  public:
    PdhError(std::string_view call, PDH_STATUS status);
};

// One instance's value at a sample, such as logical processor "3" or "_Total".
struct CounterInstance {
    std::wstring name;
    double       value;
};

// One counter by its English path, such as "\GPU Engine(*engtype_3D)\Utilization Percentage", on a PDH query of its
// own. A * in the instance names every matching instance, matched again on each Collect, so instances that come and go
// with processes, disks and adapters are followed.
class PerformanceCounter {
  public:
    // Throws PdhError when the path is malformed or names an object or counter this system does not have.
    explicit PerformanceCounter(const std::wstring& path);

    // Takes a sample. A rate or percent of time is the change between two samples, so it has values only from the
    // second on.
    void Collect();

    // Every instance's value at the last sample, leaving out instances that just appeared. None when the sample has no
    // value (a rate's first sample, or one PDH could not calculate), which the next sample can have, and empty when a
    // wildcard matches no instance. Throws PdhError when PDH cannot format them.
    std::optional<std::vector<double>> Values() const;

    // Values, with the instance each belongs to, in PDH's order.
    std::optional<std::vector<CounterInstance>> Instances() const;

  private:
    UniqueReleased<PDH_HQUERY, &PdhCloseQuery> _query;
    PDH_HCOUNTER                               _counter = nullptr;
};
