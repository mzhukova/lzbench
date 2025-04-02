/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 *  Intel® Query Processing Library (Intel® QPL)
 *  Middle Layer API (private C++ API)
 */

#ifndef QPL_COMPRESSION_DICTIONARY_DICTIONARY_UTILS_HPP_
#define QPL_COMPRESSION_DICTIONARY_DICTIONARY_UTILS_HPP_

#include "common/defs.hpp"
#include "dictionary_defs.hpp"

namespace qpl::ml::compression {
auto convert_public_hw_dict_level_to_internal(hw_compression_level hw_dict_level) noexcept -> hardware_dictionary_level;

auto get_history_size_for_dictionary(hardware_dictionary_level hw_dict_level) noexcept -> size_t;

auto get_dictionary_size_in_aecs(qpl_dictionary& dictionary) noexcept -> uint32_t;

auto get_load_dictionary_flag(qpl_dictionary& dictionary) noexcept -> uint8_t;

auto get_dictionary_size(software_compression_level sw_level, hardware_dictionary_level hw_dict_level,
                         size_t raw_dictionary_size) noexcept -> size_t;

auto build_dictionary(qpl_dictionary& dictionary, software_compression_level sw_level,
                      hardware_dictionary_level hw_dict_level, const uint8_t* raw_dict_ptr,
                      size_t raw_dict_size) noexcept -> qpl_ml_status;

auto get_dictionary_data(qpl_dictionary& dictionary) noexcept -> uint8_t*;

auto get_dictionary_hw_hash_table(qpl_dictionary& dictionary) noexcept -> uint8_t*;
} // namespace qpl::ml::compression

#endif // QPL_COMPRESSION_DICTIONARY_DICTIONARY_UTILS_HPP_
