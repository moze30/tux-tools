/*
 * Tux-Tools - native Win32 edition
 *
 * Collector entry points.
 */

#pragma once

#include "report.h"

namespace viewer {

Report collectOpenGL();
Report collectVulkan();

}  // namespace viewer
