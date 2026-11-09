#pragma once

#include "sanskritservice.h"

namespace sanskrit::detail {
struct GlossRequest
{
    Request stage;
    AnalysisWithGlosses result;
    Completion<AnalysisWithGlosses> completion;
};
} // namespace sanskrit::detail
