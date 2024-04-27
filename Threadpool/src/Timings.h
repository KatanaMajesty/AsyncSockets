#pragma once

#include <chrono>

using Milliseconds = std::chrono::milliseconds;

using PrimaryClockType  = std::chrono::system_clock;
using TimepointMs       = std::chrono::time_point<PrimaryClockType>;

namespace Timings
{

    inline TimepointMs Now()
    {
        return PrimaryClockType::now();
    }

    inline Milliseconds Elapsed(const TimepointMs& begin, const TimepointMs& end)
    {
        return std::chrono::duration_cast<Milliseconds>(end - begin);
    }

}