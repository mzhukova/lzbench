/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "stored_block_units.hpp"

#include "compression/deflate/streams/hw_deflate_state.hpp"
#include "simple_memory_ops.hpp"
#include "util/checksum.hpp"
#include "util/util.hpp"

namespace qpl::ml::compression {

constexpr uint32_t OWN_MAX_BIT_INDEX                  = 7U;
constexpr uint32_t OWN_DEFLATE_HEADER_MARKER_BIT_SIZE = 3U;
constexpr uint32_t OWN_FINAL_STORED_BLOCK             = 1U;
constexpr uint32_t OWN_STORED_BLOCK                   = 0U;

typedef struct {
    uint16_t length;
    uint16_t negative_length;
} stored_block_header_t;

/**
 * @brief Write stored block header
 * @note In case when size to be written is bigger than the available output buffer, function will return -1 and reset input as it was before.
 */
static auto write_stored_block(uint8_t* source_ptr, uint16_t source_size, uint8_t* output_begin_ptr,
                               uint32_t output_max_size, uint32_t start_bit_offset, bool is_final = false) noexcept
        -> int64_t {
    // Write deflate header
    const uint16_t header             = ((is_final) ? OWN_FINAL_STORED_BLOCK : OWN_STORED_BLOCK) << start_bit_offset;
    const uint16_t header_mask        = ~static_cast<uint16_t>(0U) - ((1 << start_bit_offset) - 1);
    uint8_t*       current_output_ptr = output_begin_ptr;
    int64_t        output_size        = output_max_size;

    if (start_bit_offset <= OWN_MAX_BIT_INDEX - OWN_DEFLATE_HEADER_MARKER_BIT_SIZE) {
        *current_output_ptr &= ~header_mask;
        *current_output_ptr |= header;
        current_output_ptr++;
        output_size--;
    } else {
        uint16_t* header_ptr = (uint16_t*)current_output_ptr;
        *header_ptr &= ~header_mask;
        *header_ptr |= header;
        current_output_ptr += sizeof(uint16_t);
        output_size -= sizeof(uint16_t);
    }

    // Write stored block header
    stored_block_header_t* stored_block_header_ptr = (stored_block_header_t*)current_output_ptr;
    stored_block_header_ptr->length                = source_size;
    stored_block_header_ptr->negative_length       = ~source_size;

    current_output_ptr += sizeof(stored_block_header_t);
    output_size -= sizeof(stored_block_header_t);

    // Size of the last copy
    output_size -= source_size;

    // If output buffer is not enough, return -1 and reset input as it was before
    if (output_size < 0) {
        current_output_ptr = output_begin_ptr;
        return -1;
    }

    // Write raw data
    core_sw::util::copy(source_ptr, source_ptr + source_size, current_output_ptr);

    current_output_ptr += source_size;

    return static_cast<uint32_t>(current_output_ptr - output_begin_ptr);
}

/**
 * @brief Write stored blocks
 * @note In case when size to be written is bigger than the available output buffer, function will return -1 and reset input as it was before.
 */
auto write_stored_blocks(uint8_t* source_ptr, uint32_t source_size, uint8_t* output_ptr, uint32_t output_max_size,
                         uint32_t start_bit_offset, bool is_final) noexcept -> int64_t {
    auto  chunks_count     = source_size / stored_block_max_length;
    auto* output_begin_ptr = output_ptr;
    auto  last_chunk_size  = source_size % stored_block_max_length;

    for (uint32_t chunk = 0U; chunk < chunks_count; chunk++) {
        auto is_last       = (!last_chunk_size && chunk == chunks_count - 1) ? is_final : false;
        auto written_bytes = write_stored_block(source_ptr, stored_block_max_length, output_ptr, output_max_size,
                                                start_bit_offset, (is_last) ? is_final : false);
        if (written_bytes < 0) {
            return -1;
        } else {
            source_ptr += stored_block_max_length;
            output_ptr += static_cast<uint32_t>(written_bytes);
            output_max_size -= static_cast<uint32_t>(written_bytes);
            start_bit_offset = 0U;
        }
    }

    if (last_chunk_size) {
        auto written_bytes = write_stored_block(source_ptr, last_chunk_size, output_ptr, output_max_size,
                                                start_bit_offset, is_final);
        if (written_bytes < 0) {
            return -1;
        } else {
            output_ptr += static_cast<uint32_t>(written_bytes);
            output_max_size -= static_cast<uint32_t>(written_bytes);
        }
    }

    return static_cast<uint32_t>(output_ptr - output_begin_ptr);
}

auto write_stored_block(deflate_state<execution_path_t::software>& stream, compression_state_t& state) noexcept
        -> qpl_ml_status {
    // If canned mode, writing stored block will cause error in decompression later
    // because it will parse stored block header as if it was the body.
    // Instead, return error directly
    if (stream.compression_mode() == canned_mode) { return status_list::more_output_needed; }

    auto isal_state = &stream.isal_stream_ptr_->internal_state;

    uint32_t copy_size         = 0;
    uint32_t avail_in          = 0;
    uint32_t block_next_offset = 0;
    uint8_t* next_in           = nullptr;

    state = compression_state_t::write_stored_block_header;

    do { //NOLINT(cppcoreguidelines-avoid-do-while)
        auto status = write_stored_block_header(stream, state);
        if (status) { break; }

        assert(isal_state->count <= isal_state->block_end - isal_state->block_next);

        copy_size         = isal_state->count;
        block_next_offset = stream.isal_stream_ptr_->total_in - isal_state->block_next;
        next_in           = stream.isal_stream_ptr_->next_in - block_next_offset;
        avail_in          = stream.isal_stream_ptr_->avail_in + block_next_offset;

        if (copy_size > stream.isal_stream_ptr_->avail_out || copy_size > avail_in) {
            isal_state->count = copy_size;
            copy_size =
                    (stream.isal_stream_ptr_->avail_out <= avail_in) ? stream.isal_stream_ptr_->avail_out : avail_in;

            stream.write_bytes(next_in, copy_size);
            isal_state->count -= copy_size;

            return status_list::more_output_needed;
        } else {
            stream.write_bytes(next_in, copy_size);

            isal_state->count = 0;

            state = compression_state_t::write_stored_block_header;
        }

        isal_state->block_next += copy_size;

        if (isal_state->block_next == isal_state->block_end) {
            if (stream.isal_stream_ptr_->avail_in) {
                stream.reset_match_history();

                state = compression_state_t::start_new_block;
            } else {

                state = isal_state->has_eob_hdr ? compression_state_t::finish_deflate_block
                                                : compression_state_t::finish_compression_process;
            }
        }
    } while (state == compression_state_t::write_stored_block_header);

    return status_list::ok;
}

auto write_stored_block_header(deflate_state<execution_path_t::software>& stream, compression_state_t& state) noexcept
        -> qpl_ml_status {
    auto isal_state = &stream.isal_stream_ptr_->internal_state;
    auto bit_buffer = &isal_state->bitbuf;

    uint64_t       stored_block_header = 0;
    uint32_t       copy_size           = 0;
    uint32_t       memcopy_len         = 0;
    uint32_t       avail_in            = 0;
    uint32_t       block_next_offset   = 0;
    const uint32_t block_in_size       = isal_state->block_end - isal_state->block_next;

    if (block_in_size > stored_block_max_length) {
        stored_block_header = 0xFFFF;
        copy_size           = stored_block_max_length;
    } else {
        stored_block_header = ~static_cast<uint64_t>(block_in_size);
        stored_block_header <<= 16;
        stored_block_header |= (block_in_size & 0xFFFF);
        copy_size = block_in_size;

        /* Handle BFINAL bit */
        block_next_offset = stream.isal_stream_ptr_->total_in - isal_state->block_next;
        avail_in          = stream.isal_stream_ptr_->avail_in + block_next_offset;

        if (stream.isal_stream_ptr_->end_of_stream && avail_in == block_in_size) {
            stream.isal_stream_ptr_->internal_state.has_eob_hdr = 1;
        }
    }

    if (bit_buffer->m_bit_count == 0 && stream.isal_stream_ptr_->avail_out >= stored_header_length + 1) {
        stored_block_header = stored_block_header << 8;
        stored_block_header |= stream.isal_stream_ptr_->internal_state.has_eob_hdr;

        memcopy_len                   = stored_header_length + 1;
        auto* stored_block_header_ptr = reinterpret_cast<uint8_t*>(&stored_block_header);
        stream.write_bytes(stored_block_header_ptr, memcopy_len);
    } else if (stream.isal_stream_ptr_->avail_out >= bit_buffer_slope_bytes) {
        stream.reset_bit_buffer();

        write_bits_flush(bit_buffer, isal_state->has_eob_hdr, 3);

        stream.dump_bit_buffer();

        memcopy_len                   = stored_header_length;
        auto* stored_block_header_ptr = reinterpret_cast<uint8_t*>(&stored_block_header);
        stream.write_bytes(stored_block_header_ptr, memcopy_len);
    } else {
        stream.isal_stream_ptr_->internal_state.has_eob_hdr = 0;

        return status_list::more_output_needed;
    }

    state             = compression_state_t::write_stored_block;
    isal_state->count = copy_size;

    return status_list::ok;
}

auto calculate_size_needed(uint32_t input_data_size, uint32_t bit_size) noexcept -> uint32_t {
    uint32_t size = util::bit_to_byte(bit_size);

    if (0U == input_data_size) {
        size += stored_block_header_length;
    } else {
        const uint32_t stored_blocks_count = (input_data_size + stored_block_max_length - 1U) / stored_block_max_length;
        size += input_data_size + stored_blocks_count * stored_block_header_length;
    }

    return size;
}

auto write_stored_block(deflate_state<execution_path_t::hardware>& state) noexcept -> compression_operation_result_t {
    constexpr uint32_t IAA_ACCUMULATOR_CAPACITY = 256U + 64U;

    compression_operation_result_t result;

    hw_iaa_aecs_compress* actual_aecs = hw_iaa_aecs_compress_get_aecs_ptr(
            state.meta_data_->aecs_, state.meta_data_->aecs_index, state.meta_data_->aecs_size);
    if (!actual_aecs) {
        result.status_code_ = status_list::internal_error;
        return result;
    }

    uint8_t* input_ptr   = nullptr;
    uint32_t input_size  = 0U;
    uint8_t* output_ptr  = nullptr;
    uint32_t output_size = 0U;

    hw_iaa_descriptor_get_input_buffer(state.compress_descriptor_, &input_ptr, &input_size);
    hw_iaa_descriptor_get_output_buffer(state.compress_descriptor_, &output_ptr, &output_size);

    const bool is_block_continued = (!state.is_first_chunk() && !state.start_new_block);
    uint32_t   bits_to_flush      = 0U;

    if (!is_block_continued) {
        bits_to_flush = state.meta_data_->stored_bits;
    } else {
        bits_to_flush = hw_iaa_aecs_compress_accumulator_get_actual_bits(actual_aecs);
        hw_iaa_aecs_compress_accumulator_insert_eob(actual_aecs, state.meta_data_->eob_code);
        bits_to_flush += state.meta_data_->eob_code.length;
    }

    auto stored_blocks_required_size = calculate_size_needed(input_size, bits_to_flush);

    if (stored_blocks_required_size > output_size) {
        result.status_code_ = status_list::more_output_needed;

        return result;
    }

    // Flush AECS buffer
    if (IAA_ACCUMULATOR_CAPACITY <= bits_to_flush) {
        result.status_code_ = status_list::internal_error;

        return result;
    }

    uint32_t bytes_written = 0U;

    if (bits_to_flush) {
        hw_iaa_aecs_compress_accumulator_flush(actual_aecs, &output_ptr, bits_to_flush);

        auto offset = bits_to_flush / byte_bit_size;
        bytes_written += offset;
        output_ptr += offset;
    } else {
        actual_aecs->num_output_accum_bits = 0U;
    }

    // Write stored blocks
    const int64_t stored_block_bytes = write_stored_blocks(input_ptr, input_size, output_ptr, output_size,
                                                           bits_to_flush & 7U, state.is_last_chunk());

    if (stored_block_bytes < 0) {
        result.status_code_ = status_list::more_output_needed;
        return result;
    } else {
        bytes_written += static_cast<uint32_t>(stored_block_bytes); // safe as we either return uint32_t or -1
    }

    // Calculate checksums
    uint32_t              crc = 0U, xor_checksum = 0U;
    hw_iaa_aecs_compress* actual_aecs_in = hw_iaa_aecs_compress_get_aecs_ptr(
            state.meta_data_->aecs_, state.meta_data_->aecs_index, state.meta_data_->aecs_size);
    if (!actual_aecs_in) {
        result.status_code_ = status_list::internal_error;
        return result;
    }

    hw_iaa_aecs_compress_get_checksums(actual_aecs_in, &crc, &xor_checksum);

    crc = (false) ? // @todo Add Support of 2 different crc polynomials
                  util::crc32_iscsi_inv(input_ptr, input_ptr + input_size, crc)
                  : util::crc32_gzip(input_ptr, input_ptr + input_size, crc);

    xor_checksum = util::xor_checksum(input_ptr, input_ptr + input_size, xor_checksum);

    hw_iaa_aecs_compress* actual_aecs_out = hw_iaa_aecs_compress_get_aecs_ptr(
            state.meta_data_->aecs_, state.meta_data_->aecs_index ^ 1U, state.meta_data_->aecs_size);
    if (!actual_aecs_out) {
        result.status_code_ = status_list::internal_error;
        return result;
    }

    hw_iaa_aecs_compress_set_checksums(actual_aecs_out, crc, xor_checksum);

    // Prepare result
    result.checksums_.crc32_ = crc;
    result.checksums_.xor_   = xor_checksum;
    result.completed_bytes_  = input_size;
    result.output_bytes_     = bytes_written;
    result.last_bit_offset   = 0U;

    result.status_code_ = status_list::ok;

    return result;
}

auto recover_and_write_stored_blocks(deflate_state<execution_path_t::software>& stream,
                                     compression_state_t&                       state) noexcept -> qpl_ml_status {
    // If canned mode, writing stored block will cause error in decompression later
    // because it will parse stored block header as if it was the body.
    // Instead, return error directly
    if (stream.compression_mode() == canned_mode) { return status_list::more_output_needed; }

    stream.isal_stream_ptr_->next_out -= stream.isal_stream_ptr_->total_out;
    stream.isal_stream_ptr_->avail_out += stream.isal_stream_ptr_->total_out;
    stream.isal_stream_ptr_->total_out = 0U;

    stream.isal_stream_ptr_->next_in -= stream.isal_stream_ptr_->total_in;
    stream.isal_stream_ptr_->avail_in += stream.isal_stream_ptr_->total_in;
    stream.isal_stream_ptr_->total_in = 0U;

    if (stream.isal_stream_ptr_->avail_out < get_stored_blocks_size(stream.isal_stream_ptr_->avail_in)) {
        return status_list::more_output_needed;
    }

    const int64_t stored_block_bytes = write_stored_blocks(
            stream.isal_stream_ptr_->next_in, stream.isal_stream_ptr_->avail_in, stream.isal_stream_ptr_->next_out,
            stream.isal_stream_ptr_->avail_out, 0U, stream.is_last_chunk());

    if (stored_block_bytes < 0) {
        return status_list::more_output_needed;
    } else {
        const int32_t result = static_cast<int32_t>(stored_block_bytes); // safe as we either return uint32_t or -1

        stream.isal_stream_ptr_->next_out += result;
        stream.isal_stream_ptr_->avail_out -= result;
        stream.isal_stream_ptr_->total_out += result;

        stream.isal_stream_ptr_->next_in += stream.isal_stream_ptr_->avail_in;
        stream.isal_stream_ptr_->total_in += stream.isal_stream_ptr_->avail_in;
        stream.isal_stream_ptr_->avail_in = 0U;

        state = compression_state_t::finish_compression_process;

        return status_list::ok;
    }
}

} // namespace qpl::ml::compression
