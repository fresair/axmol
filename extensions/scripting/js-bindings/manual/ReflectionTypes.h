/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace axjs::reflection
{
struct MethodDescriptor
{
    std::vector<std::string> arguments;
    std::string result;
};

inline bool parseType(std::string_view text, std::size_t& offset, bool allowVoid, std::string& type)
{
    auto first = offset;
    if (offset >= text.size()) return false;
    unsigned rank = 0;
    while (offset < text.size() && text[offset] == '[') { ++offset; if (++rank > 255) return false; }
    if (offset >= text.size()) return false;
    char code = text[offset++];
    if (code == 'L')
    {
        auto nameStart = offset;
        bool segment = false;
        for (; offset < text.size() && text[offset] != ';'; ++offset)
        {
            char c = text[offset];
            if (c == '/') { if (!segment) return false; segment = false; }
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$') segment = true;
            else return false;
        }
        if (offset == nameStart || !segment || offset == text.size()) return false;
        ++offset;
    }
    else if (std::string_view("ZBCSIJFD").find(code) == std::string_view::npos &&
        !(code == 'V' && allowVoid && rank == 0)) return false;
    type = std::string(text.substr(first, offset - first));
    return true;
}

inline bool parseMethodDescriptor(std::string_view text, MethodDescriptor& method)
{
    method = {};
    if (text.empty() || text[0] != '(') return false;
    std::size_t offset = 1;
    unsigned slots = 0;
    while (offset < text.size() && text[offset] != ')')
    {
        std::string type;
        if (!parseType(text, offset, false, type)) return false;
        slots += type == "J" || type == "D" ? 2 : 1;
        if (slots > 255) return false;
        method.arguments.push_back(std::move(type));
    }
    if (offset >= text.size() || text[offset++] != ')') return false;
    return parseType(text, offset, true, method.result) && offset == text.size();
}

template<class T> bool checkedInteger(double input, T& result)
{
    if (!std::isfinite(input) || std::trunc(input) != input) return false;
    long double value = input;
    if (value < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
        value > static_cast<long double>(std::numeric_limits<T>::max())) return false;
    if constexpr (sizeof(T) >= 8)
        if (value < -9007199254740991.0L || value > 9007199254740991.0L) return false;
    result = static_cast<T>(input);
    return true;
}
}  // namespace axjs::reflection
