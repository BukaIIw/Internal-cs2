#pragma once

namespace misc
{
    struct Bind;
}

namespace settings
{
    void Bool(const char* name, bool* value);
    void Int(const char* name, int* value);
    void Float(const char* name, float* value);
    void Color(const char* name, float* rgba);
    void Key(const char* name, misc::Bind* bind);

    bool Save();
    bool Load();
    const char* Path();
}
