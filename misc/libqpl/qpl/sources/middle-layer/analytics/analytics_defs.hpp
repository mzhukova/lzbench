/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef ANALYTIC_DEFS_HPP
#define ANALYTIC_DEFS_HPP

#include <climits>
#include <cstdint>

#include "common/defs.hpp"

namespace qpl::ml::analytics {

enum class stream_format_t { le_format, be_format, prle_format };

// Output stream supports the following output bit width formats:
enum class output_bit_width_format_t : uint32_t {
    same_as_input = 0U, // Input bit width is same as input stream bit width
    bits_8        = 1U, // 8 bits
    bits_16       = 2U, // 16 bits
    bits_32       = 3U  // 32 bits
};

enum class analytic_pipeline { simple, prle, inflate, inflate_prle };

struct analytic_operation_result_t {
    uint32_t     status_code_     = 0U;
    uint32_t     output_bytes_    = 0U;
    uint8_t      last_bit_offset_ = 0U;
    aggregates_t aggregates_;
    checksums_t  checksums_;
};

void inline aggregates_empty_callback(const uint8_t* UNREFERENCED_PARAMETER(src_ptr),
                                      uint32_t       UNREFERENCED_PARAMETER(length),
                                      uint32_t*      UNREFERENCED_PARAMETER(min_value_ptr),
                                      uint32_t*      UNREFERENCED_PARAMETER(max_value_ptr),
                                      uint32_t*      UNREFERENCED_PARAMETER(sum_ptr),
                                      uint32_t*      UNREFERENCED_PARAMETER(index_ptr)) {
    // Don't do anything, this is just a stub
}

} // namespace qpl::ml::analytics

#endif // ANALYTIC_DEFS_HPP
