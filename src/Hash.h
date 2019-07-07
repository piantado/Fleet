#pragma once

inline size_t hash_combine(size_t a, size_t b) {
    return (a^(a + 0x9e3779b9 + (b<<6) + (b>>2)));
}