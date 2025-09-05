#include <SDL.h>
#include "slangdisplay.h"
#include "imgui.h"
#include "render_slang.h"
#include "render_plugin.h"

uint32_t get_sdl_window_flags()
{
    // No special flags needed for gfx abstraction
    return 0;
}

void set_imgui_context(ImGuiContext* context)
{
    ImGui::SetCurrentContext(context);
}

std::unique_ptr<Display> make_display(SDL_Window* window)
{
    return std::make_unique<SlangDisplay>(window);
}

std::unique_ptr<RenderBackend> make_renderer(Display* display)
{
    auto* slang_display = dynamic_cast<SlangDisplay*>(display);
    if (slang_display) {
        auto renderer = std::make_unique<RenderSlang>();
        // Set the display reference so the renderer can access framebuffer resources
        renderer->set_display(slang_display);
        return renderer;
    }
    return nullptr;
}

POPULATE_PLUGIN_FUNCTIONS(get_sdl_window_flags, set_imgui_context, make_display, make_renderer)
