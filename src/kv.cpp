#include "kv.h"

namespace
{
    struct Parser
    {
        char* p;
        char* end;

        static bool Space(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool Skip()
        {
            while (p < end)
            {
                if (Space(*p))
                    ++p;
                else if (*p == '/' && p + 1 < end && p[1] == '/')
                    while (p < end && *p != '\n')
                        ++p;
                else
                    return true;
            }
            return false;
        }

        bool Token(std::string_view& out, bool& quoted)
        {
            if (!Skip())
                return false;
            if (*p == '"')
            {
                quoted = true;
                char* start = ++p;
                char* w = start;
                while (p < end && *p != '"')
                {
                    if (*p == '\\' && p + 1 < end)
                    {
                        const char c = p[1];
                        *w++ = c == 'n' ? '\n' : c == 't' ? '\t' : c;
                        p += 2;
                        continue;
                    }
                    *w++ = *p++;
                }
                out = std::string_view(start, w - start);
                if (p < end)
                    ++p;
                return true;
            }
            quoted = false;
            if (*p == '{' || *p == '}')
            {
                out = std::string_view(p++, 1);
                return true;
            }
            char* start = p;
            while (p < end && !Space(*p) && *p != '"' && *p != '{' && *p != '}')
                ++p;
            out = std::string_view(start, p - start);
            return true;
        }

        void SkipCondition()
        {
            char* save = p;
            if (Skip() && *p == '[')
            {
                std::string_view t;
                bool q;
                Token(t, q);
            }
            else
                p = save;
        }

        bool Block(kv::Node& parent, bool root)
        {
            std::string_view tok;
            bool q;
            while (Token(tok, q))
            {
                if (!q && tok == "}")
                    return !root;
                if (!q && (tok == "{" || (!tok.empty() && tok[0] == '[')))
                    continue;
                kv::Node node;
                node.key = tok;
                SkipCondition();
                std::string_view val;
                bool vq;
                if (!Token(val, vq))
                    return root;
                if (!vq && val == "{")
                {
                    if (!Block(node, false))
                        return false;
                }
                else
                {
                    node.value = val;
                    SkipCondition();
                }
                parent.children.push_back(std::move(node));
            }
            return root;
        }
    };
}

bool kv::Equal(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return false;
    }
    return true;
}

bool kv::Parse(std::string& text, Node& root)
{
    size_t start = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        start = 3;
    Parser p{ text.data() + start, text.data() + text.size() };
    return p.Block(root, true);
}
