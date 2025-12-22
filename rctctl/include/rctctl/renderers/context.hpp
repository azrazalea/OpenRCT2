#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rctctl::renderers {

struct RenderContext
{
    const std::vector<std::string>* columns = nullptr;
    const std::optional<std::string>* filter = nullptr;
    bool watch = false;
    std::optional<int> watchIntervalSeconds;
};

class ScopedRenderContext
{
public:
    explicit ScopedRenderContext(const RenderContext& ctx);
    ~ScopedRenderContext();

private:
    const RenderContext* m_previous;
};

const RenderContext& CurrentRenderContext();

} // namespace rctctl::renderers

