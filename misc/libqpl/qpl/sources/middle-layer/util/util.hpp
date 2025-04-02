/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef MIDDLE_LAYER_UTIL_HPP
#define MIDDLE_LAYER_UTIL_HPP

#include "common/defs.hpp"

extern "C" const uint8_t own_reversed_bits_table[];

namespace qpl::ml {

/**
 * Operator that converts integer to number of kilobytes
 *
 * @param value integer to convert
 * @return number of kilobytes
 */
constexpr auto operator"" _kb(unsigned long long value) -> uint32_t {
    return static_cast<uint32_t>(value * 1024U);
}

namespace util {

constexpr const uint32_t default_alignment = 64U;

[[nodiscard]] constexpr auto align_size(size_t size, uint32_t align = default_alignment) noexcept -> size_t {
    return (((static_cast<uint32_t>(size)) + (align)-1U) & ~((align)-1U));
}

static inline auto round_to_nearest_multiple(uint32_t number_to_round, uint32_t multiple) {
    uint32_t result = number_to_round + multiple / 2;
    result -= result % multiple;

    return result;
}

template <class iterator_t>
constexpr auto is_random_access_iterator_v() -> bool {
    return !static_cast<bool>((!std::is_same<typename std::iterator_traits<iterator_t>::iterator_category,
                                             std::random_access_iterator_tag>::value) ||
                              (!std::is_same<typename std::iterator_traits<iterator_t>::value_type, uint8_t>::value));
}

inline uint32_t bit_width_to_bits(const uint32_t value) noexcept {
    if (value > 0U && value <= 8U) {
        return byte_bits_size;
    } else if (value > 8U && value <= 16U) {
        return short_bits_size;
    } else {
        return int_bits_size;
    }
}

inline uint32_t bit_width_to_bytes(const uint32_t value) noexcept {
    return std::min(1U << ((value - 1U) >> 3U), 4U);
}

inline uint32_t bit_to_byte(const uint32_t value) noexcept {
    if (value > std::numeric_limits<uint32_t>::max() - max_bit_index) { return 1U << 29U; }

    const uint32_t bytes = (((value) + max_bit_index) >> bit_len_to_byte_shift_offset);

    return bytes;
}

inline size_t bit_to_byte(const size_t value) noexcept {
    if (value > std::numeric_limits<size_t>::max() - max_bit_index) { return 1LLU << 61U; }

    const size_t bytes = (((value) + max_bit_index) >> bit_len_to_byte_shift_offset);

    return bytes;
}

template <class T>
inline T revert_bits(T value) noexcept;

template <>
inline uint8_t revert_bits<uint8_t>(uint8_t value) noexcept {
    return own_reversed_bits_table[value];
}

template <>
inline uint16_t revert_bits<uint16_t>(uint16_t value) noexcept {
    union {
        uint16_t uint;
        uint8_t  ubyte[2];
    } y, z;

    y.uint     = value;
    z.ubyte[0] = revert_bits(y.ubyte[1]);
    z.ubyte[1] = revert_bits(y.ubyte[0]);

    return z.uint;
}

template <class mask_type, uint32_t number_of_bits>
constexpr mask_type build_mask() {
    if constexpr (std::is_same<mask_type, uint8_t>::value && number_of_bits <= 8U) {
        return static_cast<uint8_t>(1U << number_of_bits) - 1;
    } else if constexpr (std::is_same<mask_type, uint16_t>::value && number_of_bits <= 16U) {
        return static_cast<uint16_t>(1U << number_of_bits) - 1;
    } else if constexpr (std::is_same<mask_type, uint32_t>::value && number_of_bits <= 32U) {
        return static_cast<uint32_t>(1U << number_of_bits) - 1;
    } else if constexpr (std::is_same<mask_type, uint64_t>::value && number_of_bits <= 64U) {
        return static_cast<uint64_t>(1ULL << number_of_bits) - 1;
    }
}

template <class mask_type>
mask_type build_mask(uint32_t number_of_bits) {
    if constexpr (std::is_same<mask_type, uint8_t>::value) {
        return static_cast<uint8_t>(1U << number_of_bits) - 1;
    } else if constexpr (std::is_same<mask_type, uint16_t>::value) {
        return static_cast<uint16_t>(1U << number_of_bits) - 1;
    } else if constexpr (std::is_same<mask_type, uint32_t>::value) {
        return static_cast<uint32_t>(1U << number_of_bits) - 1;
    } else if constexpr (std::is_same<mask_type, uint64_t>::value) {
        return static_cast<uint64_t>(1ULL << number_of_bits) - 1;
    }
}

struct bitmask128 {
    uint64_t low;
    uint64_t high;

    bitmask128() noexcept : low(0U), high(0U) {}
    bitmask128(const uint32_t size) noexcept {
        if (size < 64) {
            low  = (1ULL << size) - 1U;
            high = 0U;
        } else if (size < 128) {
            low  = UINT64_MAX;
            high = (1ULL << (size - 64)) - 1U;
        } else {
            low  = UINT64_MAX;
            high = UINT64_MAX;
        }
    }

    bool operator[](const uint32_t idx) const noexcept {
        if (idx < 64) {
            return (low & (static_cast<uint64_t>(1U) << idx)) != 0U;
        } else if (idx < 128) {
            return (high & (static_cast<uint64_t>(1U) << (idx - 64))) != 0U;
        } else {
            return false;
        }
    }

    bool operator==(const uint64_t& rhs) const noexcept { return low == rhs && high == 0U; }
};

} // namespace util
} // namespace qpl::ml

#endif // MIDDLE_LAYER_UTIL_HPP
