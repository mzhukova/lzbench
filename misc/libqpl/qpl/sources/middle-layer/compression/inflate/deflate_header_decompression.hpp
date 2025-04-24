/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef QPL_MIDDLE_LAYER_COMPRESSION_INFLATE_DEFLATE_HEADER_DECOMPRESSION_HPP
#define QPL_MIDDLE_LAYER_COMPRESSION_INFLATE_DEFLATE_HEADER_DECOMPRESSION_HPP

#include <cstdint>

#include "common/defs.hpp"
#include "inflate_defs.hpp"

namespace qpl::ml::compression {
auto read_header_stateful(isal_inflate_state& inflate_state) noexcept -> qpl_ml_status;
}
#endif // QPL_MIDDLE_LAYER_COMPRESSION_INFLATE_DEFLATE_HEADER_DECOMPRESSION_HPP
