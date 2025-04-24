/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 *  Intel® Query Processing Library (Intel® QPL)
 *  Middle Layer API (private C++ API)
 */

#include "compression/huffman_only/huffman_only.hpp"
#include "compression/huffman_only/huffman_only_compression_state.hpp"
#include "compression/huffman_only/huffman_only_implementation.hpp"
#include "compression/huffman_table/huffman_table_utils.hpp"
#include "util/descriptor_processing.hpp"

namespace qpl::ml::compression {

template <>
auto compress_huffman_only<execution_path_t::software>(uint8_t* begin, const uint32_t size,
                                                       huffman_only_state<execution_path_t::software>& stream) noexcept
        -> compression_operation_result_t {
    compression_operation_result_t result;
    auto                           output_begin_ptr = stream.isal_stream_ptr_->next_out;

    stream.set_input(begin, size);

    auto                implementation = build_huffman_only_implementation();
    compression_state_t state          = compression_state_t::init_compression;

    do { //NOLINT(cppcoreguidelines-avoid-do-while)
        result.status_code_ = implementation.execute(stream, state);
    } while (!result.status_code_ && state != compression_state_t::finish_compression_process);

    if (stream.endianness_ == big_endian && !result.status_code_) {
        result.status_code_ = convert_output_to_big_endian(stream, state);
    }

    if (!result.status_code_) {
        result.checksums_.crc32_ =
                util::crc32_gzip(stream.source_begin_ptr_, stream.isal_stream_ptr_->next_in, stream.checksum().crc32);
        result.checksums_.xor_ = 0;
    }

    if (stream.compression_mode_ == dynamic_mode) {
        huffman_table_convert(*stream.isal_stream_ptr_->hufftables,
                              *stream.huffman_table_ptr_->get_sw_compression_table());
        // @todo Is it need to set huffman_header off?
    }

    result.completed_bytes_ = stream.isal_stream_ptr_->total_in;
    result.output_bytes_    = stream.isal_stream_ptr_->total_out;
    result.last_bit_offset  = stream.last_bits_offset_;

    // output_bytes_ and last_bit_offset need to be changed for BE16 format:
    // If output_bytes_ is odd, add 1 to it because the size of a compressed stream in BE16 format has to be
    // even. And if last_bit_offset is 0, it needs to be adjusted to 8.
    // If output_bytes_ is even, add 8 to last_bit_offset to represent the bits written in the last word,
    // instead of last byte.
    if (stream.endianness_ == big_endian) {
        if (result.output_bytes_ % 2 == 1) {
            result.output_bytes_ = result.output_bytes_ + 1;
            if (result.last_bit_offset == 0) { result.last_bit_offset = 8; }
        } else if (result.last_bit_offset != 0) {
            result.last_bit_offset = result.last_bit_offset + 8;
        }
    }

    if (result.status_code_ == status_list::ok && stream.is_verification_enabled_ &&
        stream.compression_mode_ != fixed_mode) {
        huffman_only_decompression_state<execution_path_t::software> verify_state(stream.allocator_);

        stream.huffman_table_ptr_->enable_sw_compression_table();

        std::array<uint8_t, sizeof(qplc_huffman_table_flat_format)> decompression_table_buffer {};

        decompression_huffman_table decompression_table(decompression_table_buffer.data(), nullptr, nullptr, nullptr);
        decompression_table.enable_sw_decompression_table();

        result.status_code_ = huffman_table_convert(*stream.huffman_table_ptr_, decompression_table);

        if (result.status_code_ != status_list::ok) { return result; }

        verify_state.input(output_begin_ptr, output_begin_ptr + result.output_bytes_)
                .crc_seed(stream.crc_seed_)
                .endianness(stream.endianness_)
                .last_bits_offset(result.last_bit_offset);

        result.status_code_ = verify_huffman_only<qpl::ml::execution_path_t::software>(
                verify_state, decompression_table, result.checksums_.crc32_);
    }

    return result;
}

template <>
auto compress_huffman_only<execution_path_t::hardware>(uint8_t* begin, const uint32_t size,
                                                       huffman_only_state<execution_path_t::hardware>& stream) noexcept
        -> compression_operation_result_t {
    compression_operation_result_t result;

    // Collect statistic
    if (stream.descriptor_collect_statistic_) {
        hw_iaa_descriptor_set_input_buffer(stream.descriptor_collect_statistic_, begin, size);

        result = util::process_descriptor<compression_operation_result_t, util::execution_mode_t::sync>(
                stream.descriptor_collect_statistic_, stream.completion_record_);

        if (result.status_code_) { return result; }

        hw_iaa_aecs_compress_set_huffman_only_huffman_table_from_histogram(stream.compress_aecs_,
                                                                           &stream.compress_aecs_->histogram);

        hw_iaa_aecs_compress_store_huffman_only_huffman_table(stream.compress_aecs_,
                                                              stream.huffman_table_ptr_->get_sw_compression_table());

        hw_iaa_descriptor_compress_set_aecs(stream.descriptor_compress_, stream.compress_aecs_, hw_aecs_access_read,
                                            stream.is_gen1_hw_);
    }

    // Compress
    hw_iaa_descriptor_set_input_buffer(stream.descriptor_compress_, begin, size);

    result = util::process_descriptor<compression_operation_result_t, util::execution_mode_t::sync>(
            stream.descriptor_compress_, stream.completion_record_);

    if (result.status_code_ == status_list::ok) { result.completed_bytes_ = size; }

    return result;
}

} // namespace qpl::ml::compression
