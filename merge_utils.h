#pragma once
#include <vector>
#include <memory>
#include "types.h"

std::vector<std::shared_ptr<LSMEntry>> MergeRanges(
    const std::vector<std::vector<std::shared_ptr<LSMEntry>>>& ranges);
