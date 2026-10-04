#include "fonts.h"
#include "typography.h"
#include "metrics.h"
#include <Windows.h>
#include <array>
#include <string>

namespace
{
    std::array<ImFont*, static_cast<int>(ui::font_role::count)> g_fonts{};

    std::string fonts_directory()
    {
        char dir[MAX_PATH]{};
        const UINT length = GetWindowsDirectoryA(dir, MAX_PATH);
        if (!length || length >= MAX_PATH)
            return "C:\\Windows\\Fonts\\";
        return std::string(dir) + "\\Fonts\\";
    }

    bool exists(const std::string& path)
    {
        const DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    ImFont* load(const std::string& primary, const std::string& fallback, const std::string& symbols, float size)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        const std::string& base = exists(primary) ? primary : fallback;
        if (!exists(base))
            return nullptr;
        ImFont* font = io.Fonts->AddFontFromFileTTF(base.c_str(), size, &config);
        if (!font)
            return nullptr;
        ImFontConfig merge = config;
        merge.MergeMode = true;
        if (base != fallback && exists(fallback))
            io.Fonts->AddFontFromFileTTF(fallback.c_str(), size, &merge);
        if (exists(symbols))
            io.Fonts->AddFontFromFileTTF(symbols.c_str(), size, &merge);
        return font;
    }
}

void ui::initialize_fonts()
{
    ImGuiIO& io = ImGui::GetIO();
    const std::string dir = fonts_directory();
    const std::string segoe = dir + "segoeui.ttf";
    const std::string symbols = dir + "seguisym.ttf";
    const metrics& base = g_base_metrics;

    ImFont* body = load(dir + "bahnschrift.ttf", segoe, symbols, base.font_body);
    if (!body)
        body = io.Fonts->AddFontDefault();
    ImFont* strong = load(dir + "seguisb.ttf", segoe, symbols, base.font_strong);
    if (!strong)
        strong = body;
    ImFont* mono = load(dir + "consola.ttf", segoe, symbols, base.font_mono);
    if (!mono)
        mono = body;

    g_fonts[static_cast<int>(font_role::body)] = body;
    g_fonts[static_cast<int>(font_role::compact)] = body;
    g_fonts[static_cast<int>(font_role::caption)] = body;
    g_fonts[static_cast<int>(font_role::badge)] = body;
    g_fonts[static_cast<int>(font_role::body_strong)] = strong;
    g_fonts[static_cast<int>(font_role::title)] = strong;
    g_fonts[static_cast<int>(font_role::display)] = strong;
    g_fonts[static_cast<int>(font_role::mono)] = mono;
    io.FontDefault = body;
}

ImFont* ui::font(font_role role)
{
    const int index = static_cast<int>(role);
    ImFont* f = index >= 0 && index < static_cast<int>(font_role::count) ? g_fonts[index] : nullptr;
    return f ? f : ImGui::GetIO().FontDefault ? ImGui::GetIO().FontDefault : ImGui::GetFont();
}

float ui::font_size(font_role role)
{
    const metrics& s = m();
    switch (role)
    {
    case font_role::body_strong:
        return s.font_strong;
    case font_role::compact:
        return s.font_compact;
    case font_role::caption:
        return s.font_caption;
    case font_role::badge:
        return s.font_badge;
    case font_role::title:
        return s.font_title;
    case font_role::display:
        return s.font_display;
    case font_role::mono:
        return s.font_mono;
    default:
        return s.font_body;
    }
}

ImVec2 ui::text_size(font_role role, const char* text, const char* text_end)
{
    return font(role)->CalcTextSizeA(font_size(role), FLT_MAX, 0.f, text, text_end);
}

ui::font_scope::font_scope(font_role role)
{
    ImGui::PushFont(font(role), font_size(role));
}

ui::font_scope::~font_scope()
{
    ImGui::PopFont();
}
