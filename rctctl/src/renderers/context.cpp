#include "rctctl/renderers/context.hpp"

namespace rctctl::renderers {
namespace {
thread_local const RenderContext* g_currentContext = nullptr;
const RenderContext g_defaultContext{};
}

ScopedRenderContext::ScopedRenderContext(const RenderContext& ctx)
    : m_previous(g_currentContext)
{
    g_currentContext = &ctx;
}

ScopedRenderContext::~ScopedRenderContext()
{
    g_currentContext = m_previous;
}

const RenderContext& CurrentRenderContext()
{
    if (g_currentContext)
    {
        return *g_currentContext;
    }
    return g_defaultContext;
}

} // namespace rctctl::renderers

