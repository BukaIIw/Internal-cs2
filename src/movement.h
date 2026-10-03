#pragma once

namespace movement
{
    inline bool bhop = false;
    inline bool autostrafe = false;
    inline int strafeMode = 1;
    inline bool installed = false;

    struct Debug
    {
        int calls = 0;
        int cmds = 0;
        int edits = 0;
        bool valid = false;
        bool ground = false;
        bool held = false;
        float speed = 0.f;
        float punch = 0.f;
        int steps = 0;
        int stage = 0;
        unsigned long fault = 0;
        int faultStage = 0;
    };
    inline Debug debug{};

    void Install();
}
