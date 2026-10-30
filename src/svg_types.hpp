#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
namespace pb {
struct SvgCommand {
    char op;
    float values[7];
};
struct SvgElement {
    const SvgCommand *commands;
    size_t count;
    uint32_t fill, stroke;
    float width;
    int cap, join;
    bool tint;
    float transform[6];
};
struct SvgAsset {
    const SvgElement *elements;
    size_t count;
    float view[4];
};
} // namespace pb
