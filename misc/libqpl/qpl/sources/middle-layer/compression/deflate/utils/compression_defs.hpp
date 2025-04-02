/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef MIDDLE_LAYER_CONSTANTS_HPP
#define MIDDLE_LAYER_CONSTANTS_HPP

#include <array>
#include <cstdint>

namespace qpl::ml::compression {

enum header_t : uint32_t { no_header_t, gzip_header_t, zlib_header_t };

enum compression_mode_t : uint32_t { dynamic_mode, fixed_mode, static_mode, canned_mode };

enum compression_level_t : uint32_t {
    level_1       = 1U,
    level_2       = 2U,
    level_3       = 3U,
    level_4       = 4U,
    level_5       = 5U,
    level_6       = 6U,
    level_7       = 7U,
    level_8       = 8U,
    level_9       = 9U,
    default_level = level_1,
    high_level    = level_3
};

enum endianness_t : uint32_t { little_endian, big_endian };

// In case of changing the number of states, update @compression_states constant
enum class compression_state_t {
    init_compression,
    preprocess_new_block,
    start_new_block,
    compression_body,
    compress_rest_data,
    create_icf_header,
    write_buffered_icf_header,
    flush_icf_buffer,
    write_stored_block_header,
    write_stored_block,
    flush_bit_buffer,
    flush_write_buffer,
    finish_deflate_block,
    finish_compression_process,
    count
};

enum mini_block_size_t : uint32_t {
    mini_block_size_none = 0U, /**< No mini-blocks */
    mini_block_size_512  = 1U, /**< Each 512 bytes are compressed independently */
    mini_block_size_1k   = 2U, /**< Each 1 kb is compressed independently */
    mini_block_size_2k   = 3U, /**< Each 2 kb are compressed independently */
    mini_block_size_4k   = 4U, /**< Each 4 kb are compressed independently */
    mini_block_size_8k   = 5U, /**< Each 8 kb are compressed independently */
    mini_block_size_16k  = 6U, /**< Each 16 kb are compressed independently */
    mini_block_size_32k  = 7U  /**< Each 32 kb are compressed independently */
};

enum class block_type_t { deflate_block, mini_block };

enum class mini_blocks_support_t { disabled, enabled };

enum class dictionary_support_t { disabled, enabled };

struct chunk_type {
    bool is_first = false;
    bool is_last  = false;
};

/**
 * Number of bits that bounds an ISA-L to create offsets more than 4k
 */
constexpr uint32_t isal_history_size_boundary = 12U;

/**
 * Size of hash table that is used during match searching
 */
constexpr uint32_t high_hash_table_size = 4096U;

constexpr uint32_t byte_bit_size   = 8U;
constexpr uint32_t uint32_bit_size = 32U;
constexpr uint32_t max_uint8       = 0xFFU;

constexpr uint32_t stored_header_length          = 4U;
constexpr uint32_t stored_block_header_length    = 5U;
constexpr uint32_t stored_block_max_length       = 0xFFFFU;
constexpr uint32_t number_of_length_codes        = 21U;
constexpr uint32_t max_ll_code_index             = 285U;
constexpr uint32_t max_d_code_index              = 29U;
constexpr uint32_t bit_buffer_slope_bytes        = 8U;
constexpr uint32_t bit_buffer_slope_bits         = bit_buffer_slope_bytes * byte_bit_size;
constexpr uint32_t end_of_block_code_index       = 256U;
constexpr uint32_t minimal_mini_block_size_power = 8U;

// clang-format off

constexpr std::array<uint8_t, 19> code_length_code_order = {16U, 17U, 18U, 0U, 8U, 7U, 9U, 6U, 10U, 5U,
                                                            11U, 4U, 12U, 3U, 13U, 2U, 14U, 1U, 15U};

constexpr std::array<uint32_t, 29> length_code_extra_bits = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                                             0x1, 0x1, 0x1, 0x1, 0x2, 0x2, 0x2, 0x2,
                                                             0x3, 0x3, 0x3, 0x3, 0x4, 0x4, 0x4, 0x4,
                                                             0x5, 0x5, 0x5, 0x5, 0x0};

constexpr std::array<uint32_t, 30> distance_code_extra_bits = {0x0, 0x0, 0x0, 0x0, 0x1, 0x1, 0x2, 0x2,
                                                               0x3, 0x3, 0x4, 0x4, 0x5, 0x5, 0x6, 0x6,
                                                               0x7, 0x7, 0x8, 0x8, 0x9, 0x9, 0xa, 0xa,
                                                               0xb, 0xb, 0xc, 0xc, 0xd, 0xd};

// clang-format on

} // namespace qpl::ml::compression

#endif // MIDDLE_LAYER_CONSTANTS_HPP
