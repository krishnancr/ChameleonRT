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
    if (slang_display && slang_display->device) {
        return std::make_unique<RenderSlang>(slang_display->device);
    }
    return nullptr;
}

POPULATE_PLUGIN_FUNCTIONS(get_sdl_window_flags, set_imgui_context, make_display, make_renderer)
