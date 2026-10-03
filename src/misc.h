#pragma once

namespace misc
{
    struct Bind
    {
        int key = 0;
        bool toggled = false;
        int mode = 0;
    };

    enum BindMode { Toggle, Hold, Always };

    inline bool thirdperson = false;
    inline float thirdDistance = 120.f;
    inline bool watermark = true;
    inline bool keybinds = true;

    inline Bind thirdBind{ 'V', false };
    inline Bind bhopBind{};
    inline Bind strafeBind{};

    inline Bind* capturing = nullptr;

    inline bool cameraHooked = false;

    void Install();
    bool Active(bool enabled, const Bind& bind);
    bool Pressed(const Bind& bind);
    void Register(Bind* bind);
    bool OnKey(int vk);
    void OnCreateMove(void* input);
    void RenderHud();
    void Cleanup();
    const char* KeyName(int vk);
}
