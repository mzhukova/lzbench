/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 *  Intel® Query Processing Library (Intel® QPL)
 *  Job API (public C API)
 */

#ifndef QPL_UTIL_JOB_API_SERVICE_H_
#define QPL_UTIL_JOB_API_SERVICE_H_

#include "qpl/c_api/job.h"
#include "qpl/c_api/status.h"

#include "common/defs.hpp"
#include "compression_operations/compression_state_t.h"
#include "hw_definitions.h"
#include "legacy_hw_path/hardware_state.h"

namespace qpl::job {

// ------ JOB VALIDATION ------ //

template <qpl_operation operation>
inline qpl_status validate_operation(const qpl_job* const job_ptr) noexcept;

template <qpl_operation operation>
inline qpl_status bad_arguments_check(const qpl_job* const job_ptr) noexcept;

// ------ JOB GETTERS ------ //
static inline auto get_execution_path(const qpl_job* const job_ptr) noexcept -> ml::execution_path_t {
    switch (job_ptr->data_ptr.path) {
        case qpl_path_software: {
            return ml::execution_path_t::software;
        }
        case qpl_path_hardware: {
            return ml::execution_path_t::hardware;
        }
        default: {
            return ml::execution_path_t::auto_detect;
        }
    }
}

static inline auto get_state(const qpl_job* const job_ptr) noexcept {
    return job_ptr->data_ptr.hw_state_ptr;
}

static inline auto get_adler32(const qpl_job* const job_ptr) noexcept {
    auto* data_ptr = (own_compression_state_t*)job_ptr->data_ptr.compress_state_ptr;
    return data_ptr->adler32;
}

static inline auto get_async_job_status(const qpl_job* const job_ptr) noexcept {
    auto* hw_state_ptr = reinterpret_cast<qpl_hw_state*>(job_ptr->data_ptr.hw_state_ptr);
    return hw_state_ptr->async_job_status;
}

static inline bool is_indexing_enabled(const qpl_job* const job_ptr) noexcept {
    return job_ptr->mini_block_size;
}

static inline bool is_dictionary(const qpl_job* const job_ptr) noexcept {
    return job_ptr->flags & QPL_FLAG_FIRST && job_ptr->dictionary != nullptr;
}

static inline bool is_high_level_compression(const qpl_job* const job_ptr) noexcept {
    return (qpl_op_compress == job_ptr->op) && (qpl_high_level == job_ptr->level);
}

static inline bool is_canned_mode_compression(const qpl_job* const job_ptr) noexcept {
    return (qpl_op_compress == job_ptr->op) && (QPL_FLAG_CANNED_MODE & job_ptr->flags);
}

static inline bool is_canned_mode_decompression(const qpl_job* const job_ptr) noexcept {
    return (qpl_op_decompress == job_ptr->op) && (QPL_FLAG_CANNED_MODE & job_ptr->flags);
}

static inline bool is_huffman_only_decompression(const qpl_job* const job_ptr) noexcept {
    return (qpl_op_decompress == job_ptr->op) && (QPL_FLAG_NO_HDRS & job_ptr->flags);
}

static inline bool is_huffman_only_compression(const qpl_job* const job_ptr) noexcept {
    return (qpl_op_compress == job_ptr->op) && (QPL_FLAG_GEN_LITERALS & job_ptr->flags);
}

static inline bool is_random_decompression(const qpl_job* const job_ptr) noexcept {
    return (qpl_op_decompress == job_ptr->op) && (QPL_FLAG_RND_ACCESS & job_ptr->flags);
}

static inline bool is_decompression(const qpl_job* const job_ptr) noexcept {
    return qpl_op_decompress == job_ptr->op;
}

static inline bool is_compression(const qpl_job* const job_ptr) noexcept {
    return job_ptr->op == qpl_op_compress;
}

static inline bool is_extract(const qpl_job* const job_ptr) noexcept {
    return qpl_op_extract == job_ptr->op;
}

static inline bool is_scan(const qpl_job* const job_ptr) noexcept {
    return qpl_op_scan_eq <= job_ptr->op;
}

static inline bool is_select(const qpl_job* const job_ptr) noexcept {
    return qpl_op_select == job_ptr->op;
}

static inline bool is_expand(const qpl_job* const job_ptr) noexcept {
    return qpl_op_expand == job_ptr->op;
}

static inline bool is_crc64(const qpl_job* const job_ptr) noexcept {
    return qpl_op_crc64 == job_ptr->op;
}

static inline bool is_filter(const qpl_job* const job_ptr) noexcept {
    return (is_scan(job_ptr) || is_extract(job_ptr) || is_select(job_ptr) || is_expand(job_ptr));
}

/**
 * @note crc64 and filter operations are always done as a single job.
*/
static inline bool is_single_job(const qpl_job* const job_ptr) noexcept {
    const uint32_t stateless_flags = QPL_FLAG_FIRST | QPL_FLAG_LAST;
    return ((stateless_flags & job_ptr->flags) == stateless_flags || is_filter(job_ptr) || is_crc64(job_ptr));
}

static inline bool is_multi_job(const qpl_job* const job_ptr) noexcept {
    return !is_single_job(job_ptr);
}

static inline bool is_zlib_flag_set(const qpl_job* const job_ptr) noexcept {
    return QPL_FLAG_ZLIB_MODE & job_ptr->flags;
}

static inline bool is_verification_supported(const qpl_job* const qpl_job_ptr) noexcept {
    bool stream_should_be_verified = false;

    if (!(qpl_job_ptr->flags & QPL_FLAG_OMIT_VERIFY)) {
        if (!(qpl_job_ptr->flags & QPL_FLAG_GEN_LITERALS)) { stream_should_be_verified = true; }
    }

    return stream_should_be_verified;
}

/**
 * @brief Check for skipping high level compression on hardware/auto execution paths.
*/
static inline bool is_supported_on_hardware(const qpl_job* const qpl_ptr) {
    return ((qpl_path_hardware == qpl_ptr->data_ptr.path || qpl_path_auto == qpl_ptr->data_ptr.path) &&
            !is_high_level_compression(qpl_ptr));
}

/**
 * @brief Check if fallback to qpl_path_software is supported.
 *
 * @warning Disallow falling back to the host execution if this is not the first chunk in a multi-chunk job
 */
static inline bool is_sw_fallback_supported(const qpl_job* const qpl_job_ptr) {
    return (qpl_path_auto == qpl_job_ptr->data_ptr.path) &&
           ((qpl_job_ptr->flags & QPL_FLAG_FIRST) || job::is_single_job(qpl_job_ptr));
}

/**
 * @brief Check if fallback to qpl_path_software is supported in case if qpl_path_hardware returns an error.
 *
 * @warning Disallow falling back to the host execution if failure is not on the
 * first chunk or if QPL_STS_MORE_OUTPUT_NEEDED (output buffer is too small) error happened.
*/
static inline bool is_sw_fallback_supported(const qpl_job* const qpl_job_ptr, qpl_status status) {
    return (QPL_STS_MORE_OUTPUT_NEEDED != status) && is_sw_fallback_supported(qpl_job_ptr);
}

/**
 * @brief Check if Force Array Output Modification is supported
 */
static inline bool is_force_array_output_supported(const qpl_job* const job_ptr) noexcept {
    // Check if job_ptr and hw_state_ptr are not null
    if (job_ptr != nullptr && job_ptr->data_ptr.path != qpl_path_software && job_ptr->data_ptr.path != qpl_path_auto &&
        job_ptr->data_ptr.hw_state_ptr != nullptr) {
        // Check if force array output modification is supported
        return ((qpl_hw_state*)job_ptr->data_ptr.hw_state_ptr)
                ->accel_context.device_properties.force_array_output_mod_available;
    }
    return false;
}

/**
 * @brief Check if Gen 2 Min Capabilities are available
 */
static inline bool are_gen_2_min_capabilities_available(const qpl_job* const job_ptr) noexcept {
    // Check if job_ptr and hw_state_ptr are not null
    if (job_ptr != nullptr && job_ptr->data_ptr.hw_state_ptr != nullptr) {
        // Check if gen 2 min capabilities are available
        return ((qpl_hw_state*)job_ptr->data_ptr.hw_state_ptr)
                ->accel_context.device_properties.gen_2_min_capabilities_available;
    }
    return false;
}

/**
 * @brief Check if no descriptor has been completed. Some descriptors may be completed
 * in a multi-descriptor job when previous submission gets QPL_STS_QUEUES_ARE_BUSY_ERR
*/
static inline bool is_no_descriptor_completed(const qpl_job* const job_ptr) {
    if (qpl_path_software == job_ptr->data_ptr.path) {
        return true;
    } else {
        auto* hw_state = (qpl_hw_state*)job_ptr->data_ptr.hw_state_ptr;
        return hw_state->multi_desc_status == qpl_none_completed;
    }
}

static inline bool is_job_submitted(const qpl_job* const job_ptr) {
    auto* state_ptr = reinterpret_cast<qpl_hw_state*>(job::get_state(job_ptr));
    return (state_ptr->async_job_status != QPL_STS_JOB_NOT_SUBMITTED);
}

// ------ JOB SETTERS ------ //
template <qpl_operation operation_type>
static inline void reset(qpl_job* const qpl_job_ptr) noexcept {
    qpl_job_ptr->total_in        = 0U;
    qpl_job_ptr->total_out       = 0U;
    qpl_job_ptr->crc             = 0U;
    qpl_job_ptr->idx_num_written = 0U;
}

/**
 * @brief set new CRC-32 and XOR checksum values
*/
static inline void update_checksums(qpl_job* const qpl_job_ptr, uint32_t crc32, uint32_t xor_checksum) noexcept {
    qpl_job_ptr->crc          = crc32;
    qpl_job_ptr->xor_checksum = xor_checksum;
}

/**
 * @brief set new CRC-64 checksum value
*/
static inline void update_crc(qpl_job* const qpl_job_ptr, uint64_t crc64) noexcept {
    qpl_job_ptr->crc64 = crc64;
}

static inline void update_multidescriptor_status(qpl_job* const            qpl_job_ptr,
                                                 hw_multidescriptor_status multi_desc_status) noexcept {
    if (qpl_job_ptr->data_ptr.path != qpl_path_software) {

        // Disable gzip/zlib, multi-chunk for saving multi-descriptor status before they are enabled and tested
        if (is_single_job(qpl_job_ptr) && !(qpl_job_ptr->flags & (QPL_FLAG_GZIP_MODE | QPL_FLAG_ZLIB_MODE))) {
            auto* state              = (qpl_hw_state*)qpl_job_ptr->data_ptr.hw_state_ptr;
            state->multi_desc_status = multi_desc_status;
        }
    }
}

/**
 * @brief set new Adler-32 checksum value
*/
static inline void update_adler32(qpl_job* const qpl_job_ptr, uint32_t adler32_in) noexcept {
    auto* data_ptr    = (own_compression_state_t*)qpl_job_ptr->data_ptr.compress_state_ptr;
    data_ptr->adler32 = adler32_in;
}

static inline void update_aggregates(qpl_job* const qpl_job_ptr, const uint32_t sum_agg, const uint32_t min_first_agg,
                                     const uint32_t max_last_agg) noexcept {
    qpl_job_ptr->sum_value             = sum_agg;
    qpl_job_ptr->first_index_min_value = min_first_agg;
    qpl_job_ptr->last_index_max_value  = max_last_agg;
}

static inline void update_input_stream(qpl_job* const qpl_job_ptr, const uint32_t size) noexcept {
    qpl_job_ptr->next_in_ptr += size;
    qpl_job_ptr->available_in -= size;
    qpl_job_ptr->total_in += size;
}

static inline void update_index_table(qpl_job* const qpl_job_ptr, const uint32_t indices_written) noexcept {
    qpl_job_ptr->idx_num_written = indices_written;
    // qpl_job_ptr->idx_num_written += indices_written; // TODO: Align between SW and HW.
}

static inline void update_output_stream(qpl_job* const qpl_job_ptr, const uint32_t size,
                                        const uint32_t last_bit_offset) noexcept {
    qpl_job_ptr->next_out_ptr += size;
    qpl_job_ptr->available_out -= size;
    qpl_job_ptr->total_out += size;
    qpl_job_ptr->last_bit_offset = last_bit_offset;
}

static inline void update_is_sw_fallback(qpl_job* const qpl_job_ptr, bool is_sw_fallback) noexcept {

    auto* state_ptr           = reinterpret_cast<qpl_hw_state*>(job::get_state(qpl_job_ptr));
    state_ptr->is_sw_fallback = is_sw_fallback;
}

static inline void set_async_job_status(qpl_job* const qpl_job_ptr, qpl_status async_job_status) noexcept {
    auto* state_ptr             = reinterpret_cast<qpl_hw_state*>(job::get_state(qpl_job_ptr));
    state_ptr->async_job_status = async_job_status;
}

static inline void set_job_to_in_progress(qpl_job* const qpl_job_ptr) noexcept {
    auto* state_ptr             = reinterpret_cast<qpl_hw_state*>(job::get_state(qpl_job_ptr));
    state_ptr->async_job_status = QPL_STS_BEING_PROCESSED;
}

/**
 * @brief Check if the job should immediately fall back to software path.
 *  Essentially checks if job configuration is supported on sw path but not hw path.
*/
static inline bool is_unsupported_on_hw_supported_on_sw(qpl_job* job_ptr) noexcept {
    if (!job::is_sw_fallback_supported(job_ptr)) { return false; }
    if (job::is_huffman_only_decompression(job_ptr) && (job_ptr->flags & QPL_FLAG_HUFFMAN_BE)) {
        // Intel® In-Memory Analytics Accelerator (Intel® IAA) generation 1.0 limitation,
        // Huffman only decompression with BE16 format cannot work if ignore_end_bits is greater than 7
        // Fallback to running on SW path in this case where limitation does not exist
        return job_ptr->ignore_end_bits > 7U && !job::are_gen_2_min_capabilities_available(job_ptr);
    }

    return false;
}

template <class result_t>
void inline update(qpl_job* job_ptr, result_t& result) noexcept;
} // namespace qpl::job

#endif //QPL_UTIL_JOB_API_SERVICE_H_
