#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace agm
{
// JUCE serializes decimal values independently of the operating-system locale.
// Keep typed values and state validation on that same grammar; reject NaN,
// infinity, partial numbers and hex literals before they reach parameters.
inline bool parseFiniteDecimal(const juce::String& input, double& value, juce::String* suffix = nullptr)
{
    const auto text = input.trim();
    if (text.length() > 128) return false;
    int position = 0, digits = 0;
    auto digit = [](juce::juce_wchar c) { return c >= '0' && c <= '9'; };
    if (text[position] == '+' || text[position] == '-') ++position;
    while (digit(text[position])) { ++digits; ++position; }
    if (text[position] == '.')
    {
        ++position;
        while (digit(text[position])) { ++digits; ++position; }
    }
    if (digits == 0) return false;
    if (text[position] == 'e' || text[position] == 'E')
    {
        ++position;
        if (text[position] == '+' || text[position] == '-') ++position;
        const int start = position;
        int exponent = 0;
        while (digit(text[position]))
        {
            exponent = exponent * 10 + (int)(text[position] - '0');
            if (exponent > 999) return false; // Bound JUCE's signed exponent accumulator before conversion.
            ++position;
        }
        if (position == start) return false;
    }
    const auto remainder = text.substring(position).trim();
    if (suffix == nullptr && remainder.isNotEmpty()) return false;
    value = text.substring(0, position).getDoubleValue();
    if (!std::isfinite(value)) return false;
    if (suffix != nullptr) *suffix = remainder;
    return true;
}
}
