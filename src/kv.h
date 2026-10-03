#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace kv
{
    bool Equal(std::string_view a, std::string_view b);

    struct Node
    {
        std::string_view key;
        std::string_view value;
        std::vector<Node> children;

        const Node* Find(std::string_view k) const
        {
            for (auto& c : children)
                if (Equal(c.key, k))
                    return &c;
            return nullptr;
        }

        std::string_view Get(std::string_view k) const
        {
            auto n = Find(k);
            return n ? n->value : std::string_view{};
        }

        template <typename F>
        void Each(std::string_view k, F&& f) const
        {
            for (auto& c : children)
                if (Equal(c.key, k))
                    f(c);
        }
    };

    bool Parse(std::string& text, Node& root);
}
