// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "region_plan.hpp"
namespace exbox::editor::render {
// Synchronous command sink boundary. A null region means whole target; no call
// means no work. The callback must consume the region before returning and must
// not retain its pointer. Binding, barriers, clear values and ownership remain
// the caller's responsibility; this function cannot submit or destroy resources.
template<class Sink>
void dispatch_clear(const ClearPlan& plan, Sink&& emit) {
    switch(plan.coverage) {
    case ClearCoverage::whole_target: emit(static_cast<const Region*>(nullptr)); break;
    case ClearCoverage::rectangles:
        for(const auto& region:plan.rectangles) emit(&region);
        break;
    case ClearCoverage::none: break;
    }
}
}
