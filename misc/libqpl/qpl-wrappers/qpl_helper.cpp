/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#define DEBUG
#include "qpl_helper.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef DEBUG
#include <iostream>
#endif

#include "qpl/qpl.h"

#ifdef DEBUG
#define QPL_CHECK_STATUS(status, step)                                                             \
    if ((status) != QPL_STS_OK) {                                                                  \
        std::cerr << "QPL error at step: " << (step) << " with status: " << (status) << std::endl; \
        return;                                                                                    \
    }
#define QPL_ERR_MSG(status, msg) \
    { std::cerr << "QPL error: " << msg << std::endl; }
#else
#define QPL_CHECK_STATUS(status, step) \
    if ((status) != QPL_STS_OK) { return; }
#define QPL_ERR_MSG(status, msg)
#endif

QPLCompressionContext* allocate_qpl_context(size_t jobs_number) {
    QPLCompressionContext* ctx = (QPLCompressionContext*)malloc(sizeof(QPLCompressionContext));
    if (!ctx) return NULL;

    ctx->jobs_number = jobs_number;

    qpl_path_t path     = qpl_path_auto;
    uint32_t   job_size = 0;
    qpl_status status   = qpl_get_job_size(path, &job_size);
    if (status != QPL_STS_OK) {
        QPL_ERR_MSG(status, "qpl_get_job_size failed with status: ");
        free(ctx);
        return NULL;
    }

    ctx->job = (qpl_job*)malloc(job_size);
    if (!ctx->job) {
        free(ctx);
        return NULL;
    }

    ctx->job_c = std::vector<qpl_job*>(ctx->jobs_number, nullptr);
    for (size_t i = 0; i < ctx->jobs_number; ++i) {
        ctx->job_c[i] = (qpl_job*)malloc(job_size);
        if (!ctx->job_c[i]) {
            for (size_t j = 0; j < i; ++j) {
                free(ctx->job_c[j]);
            }
            free(ctx->job);
            free(ctx);
            return NULL;
        }
    }
    return ctx;
}

bool initialize_qpl_context(QPLCompressionContext* ctx) {
    qpl_path_t path   = qpl_path_auto;
    qpl_status status = qpl_init_job(path, ctx->job);
    if (status != QPL_STS_OK) {
        QPL_ERR_MSG(status, "qpl_init_job failed with status: ");
        return false;
    }

    for (size_t i = 0; i < ctx->jobs_number; ++i) {
        status = qpl_init_job(path, ctx->job_c[i]);
        if (status != QPL_STS_OK) {
            QPL_ERR_MSG(status, "qpl_init_job failed with status: ");
            return false;
        }
    }
    return true;
}

void free_qpl_context(QPLCompressionContext* ctx, bool initialized) {
    if (!ctx) return;

    if (initialized) {
        qpl_fini_job(ctx->job);
        for (size_t i = 0; i < ctx->jobs_number; ++i) {
            qpl_fini_job(ctx->job_c[i]);
        }
    }

    for (size_t i = 0; i < ctx->jobs_number; ++i) {
        free(ctx->job_c[i]);
    }
    free(ctx->job);
}

/**
 * @brief Waits for all jobs to complete.
 * Timeout is set to 1 minute to prevent indefinite waiting.
 */
bool wait_for_all_jobs(QPLCompressionContext* ctx, std::vector<qpl_status>& status_check_c) {
    bool all_jobs_done;
    auto start_time = std::chrono::steady_clock::now();
    do {
        all_jobs_done = true;
        for (size_t i = 0; i < ctx->jobs_number; ++i) {
            status_check_c[i] = qpl_check_job(ctx->job_c[i]);
            if (status_check_c[i] == QPL_STS_BEING_PROCESSED || status_check_c[i] == QPL_STS_QUEUES_ARE_BUSY_ERR) {
                all_jobs_done = false;
            }
        }
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::minutes>(current_time - start_time).count();
        if (elapsed_time >= 1) { return false; }
    } while (!all_jobs_done);
    return true;
}

void compress(QPLCompressionContext* ctx, char* input_data, size_t input_size, char* compressed_data,
              size_t* compressed_size, qpl_compression_levels compression_level, bool use_dynamic_huffman) {

    qpl_status    status;
    int           block_size            = ctx->block_size;
    std::uint32_t blocks                = input_size / block_size;
    size_t        total_compressed_size = 0;
    std::uint32_t last_block_size       = input_size % block_size;
    std::uint32_t header_offset         = (blocks + 4) * sizeof(std::uint32_t);

    // Write header
    std::memcpy(&compressed_data[0], &block_size, sizeof(std::uint32_t));
    std::memcpy(&compressed_data[sizeof(std::uint32_t)], &last_block_size, sizeof(std::uint32_t));
    std::memcpy(&compressed_data[2 * sizeof(std::uint32_t)], &blocks, sizeof(std::uint32_t));

    std::uint8_t* out_header = reinterpret_cast<uint8_t*>(compressed_data);
    std::uint8_t* out_data   = &out_header[header_offset];
    const auto    jobs       = ctx->jobs_number;
    auto          job_flags  = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
    if (use_dynamic_huffman) { job_flags |= QPL_FLAG_DYNAMIC_HUFFMAN; }

    if (input_size <= block_size) {
        ctx->job->next_in_ptr   = reinterpret_cast<uint8_t*>(input_data);
        ctx->job->available_in  = input_size;
        ctx->job->next_out_ptr  = out_data;
        ctx->job->available_out = *compressed_size;
        ctx->job->level         = compression_level;
        ctx->job->op            = qpl_op_compress;
        ctx->job->flags         = job_flags;

        status = qpl_execute_job(ctx->job);
        QPL_CHECK_STATUS(status, "qpl_execute_job (compress)");

        *compressed_size = ctx->job->total_out + header_offset;
    } else {

        int      i;
        int      next_to_copy_block_idx = 0;
        int      next_to_copy_block     = 0;
        int      out_block_size         = (*compressed_size - header_offset) / (blocks + 1);
        uint8_t* next_to_queue_src_addr = reinterpret_cast<uint8_t*>(input_data);
        uint8_t* next_to_copy_src_addr  = reinterpret_cast<uint8_t*>(input_data);
        uint8_t* next_to_copy_dst_addr  = out_data;
        uint8_t* last_to_copy_src_addr  = reinterpret_cast<uint8_t*>(input_data) + (blocks * block_size);

        //std::cout << "out block size " << out_block_size << "\n";
        // Submit job until jobs or queues are full
        for (i = 0; i < (blocks + 1) && i < jobs; i++) {
            std::uint32_t available_in =
                    (next_to_queue_src_addr >= last_to_copy_src_addr) ? last_block_size : block_size;
            ctx->job_c[i]->next_in_ptr   = next_to_queue_src_addr;
            ctx->job_c[i]->available_in  = available_in;
            ctx->job_c[i]->next_out_ptr  = &out_data[i * out_block_size];
            ctx->job_c[i]->available_out = out_block_size;
            ctx->job_c[i]->level         = compression_level;
            ctx->job_c[i]->op            = qpl_op_compress;
            ctx->job_c[i]->flags         = job_flags;
            next_to_queue_src_addr += available_in;

            do {
                status = qpl_submit_job(ctx->job_c[i]);
            } while (status == QPL_STS_QUEUES_ARE_BUSY_ERR);

            QPL_CHECK_STATUS(status, "qpl_submit_job (compress)");
        }

        // Check if next block to copy job is done for remaining blocks and submit next
        for (; i < (blocks + 1); i++) {
            int idx = next_to_copy_block_idx;

            while (1) {
                status = qpl_check_job(ctx->job_c[idx]);
                if (QPL_STS_OK == status) {
                    unsigned long stats = 0;
                    for (int j = 1; j < (jobs / 2) && j < blocks; ++j)
                        stats += qpl_check_job(ctx->job_c[(idx + j) % jobs]);

                    std::uint32_t block_comp_size = ctx->job_c[idx]->total_out;
                    std::memcpy(next_to_copy_dst_addr, &out_data[next_to_copy_block * out_block_size], block_comp_size);
                    std::memcpy(&out_header[(3 + next_to_copy_block) * sizeof(std::uint32_t)], &block_comp_size,
                                sizeof(std::uint32_t));

                    total_compressed_size += block_comp_size;
                    next_to_copy_dst_addr += block_comp_size;
                    next_to_copy_src_addr += block_size;

                    // schedule next block on free job unless done
                    std::uint32_t available_in =
                            (next_to_queue_src_addr >= last_to_copy_src_addr) ? last_block_size : block_size;
                    ctx->job_c[idx]->next_in_ptr   = next_to_queue_src_addr;
                    ctx->job_c[idx]->available_in  = available_in;
                    ctx->job_c[idx]->next_out_ptr  = &out_data[i * out_block_size];
                    ctx->job_c[idx]->available_out = out_block_size;
                    ctx->job_c[idx]->level         = compression_level;
                    ctx->job_c[idx]->op            = qpl_op_compress;
                    ctx->job_c[idx]->flags         = job_flags;

                    do {
                        status = qpl_submit_job(ctx->job_c[idx]);
                    } while (status == QPL_STS_QUEUES_ARE_BUSY_ERR);

                    QPL_CHECK_STATUS(status, "qpl_submit_job (compress)");

                    next_to_queue_src_addr += available_in;
                    next_to_copy_block++;
                    next_to_copy_block_idx = ++next_to_copy_block_idx % jobs;

                    break;
                } // status ok
                else if (QPL_STS_BEING_PROCESSED == status || QPL_STS_QUEUES_ARE_BUSY_ERR == status) {
                    continue;
                } else {
                    QPL_ERR_MSG(status, "job status error found: ");
                    return;
                }
            } // while next job not done
        }     // each block queued

        // Check if next to copy job is done for remaining queue
        while (next_to_copy_src_addr <= last_to_copy_src_addr) {
            int idx = next_to_copy_block_idx;

            while (1) {
                status = qpl_check_job(ctx->job_c[idx]);
                if (QPL_STS_OK == status) {
                    std::uint32_t block_comp_size = ctx->job_c[idx]->total_out;
                    std::memcpy(next_to_copy_dst_addr, &out_data[next_to_copy_block * out_block_size], block_comp_size);
                    std::memcpy(&out_header[(3 + next_to_copy_block) * sizeof(std::uint32_t)], &block_comp_size,
                                sizeof(std::uint32_t));

                    total_compressed_size += block_comp_size;
                    next_to_copy_dst_addr += block_comp_size;
                    next_to_copy_src_addr += block_size;
                    next_to_copy_block++;
                    next_to_copy_block_idx = ++next_to_copy_block_idx % jobs;

                    break;
                } // status ok
                else if (QPL_STS_BEING_PROCESSED == status || QPL_STS_QUEUES_ARE_BUSY_ERR == status) {
                    continue;
                } else {
                    QPL_ERR_MSG(status, "job status error found: ");
                    return;
                }
            } // while job not done
        }     // while jobs not done

        *compressed_size = total_compressed_size + header_offset;
    }
}

void decompress(QPLCompressionContext* ctx, char* compressed_data, size_t compressed_size, char* decompressed_data,
                size_t* decompressed_size, bool use_dynamic_huffman) {

    int           block_size, i;
    qpl_status    status;
    std::uint32_t blocks;
    size_t        total_compressed_size;
    std::uint32_t last_block_size;
    std::uint8_t* out_byte_ptr = reinterpret_cast<std::uint8_t*>(decompressed_data);
    const auto    jobs         = ctx->jobs_number;
    auto          job_flags    = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
    if (use_dynamic_huffman) { job_flags |= QPL_FLAG_DYNAMIC_HUFFMAN; }

    // Read header
    std::memcpy(&block_size, &compressed_data[0], sizeof(std::uint32_t));
    std::memcpy(&last_block_size, &compressed_data[sizeof(std::uint32_t)], sizeof(std::uint32_t));
    std::memcpy(&blocks, &compressed_data[2 * sizeof(std::uint32_t)], sizeof(std::uint32_t));

    std::uint32_t header_offset = (blocks + 4) * sizeof(std::uint32_t);
    std::uint8_t* in_header     = reinterpret_cast<std::uint8_t*>(compressed_data);
    std::uint8_t* in_data       = &in_header[header_offset];

    if ((blocks * block_size + last_block_size) > *decompressed_size) {
        QPL_ERR_MSG(status, "bad header values read");
        return;
    }

    if (blocks == 0) {
        ctx->job->next_in_ptr   = in_data;
        ctx->job->available_in  = compressed_size;
        ctx->job->next_out_ptr  = out_byte_ptr;
        ctx->job->available_out = *decompressed_size;
        ctx->job->op            = qpl_op_decompress;
        ctx->job->flags         = job_flags;

        status = qpl_execute_job(ctx->job);
        QPL_CHECK_STATUS(status, "qpl_execute_job (decompress)");

        *decompressed_size = ctx->job->total_out;
    } else {

        std::uint32_t block_compressed_size;

        for (i = 0; i < (blocks + 1) && i < jobs; i++) {

            std::memcpy(&block_compressed_size, &in_header[sizeof(std::uint32_t) * (i + 3)], sizeof(std::uint32_t));
            std::uint32_t available_out = (i == blocks) ? last_block_size : block_size;

            if (block_compressed_size > available_out) {
                QPL_ERR_MSG(status, "bad header value for block compressed size");
                return;
            }

            ctx->job_c[i]->next_in_ptr   = in_data;
            ctx->job_c[i]->available_in  = block_compressed_size;
            ctx->job_c[i]->next_out_ptr  = out_byte_ptr;
            ctx->job_c[i]->available_out = available_out;
            ctx->job_c[i]->op            = qpl_op_decompress;
            ctx->job_c[i]->flags         = job_flags;

            do {
                status = qpl_submit_job(ctx->job_c[i]);
            } while (status == QPL_STS_QUEUES_ARE_BUSY_ERR);

            QPL_CHECK_STATUS(status, "qpl_submit_job (decompress)");

            in_data += block_compressed_size;
            out_byte_ptr += available_out;
        }

        for (; i < (blocks + 1); i++) {
            int idx = i % jobs;

            std::uint32_t available_out = (i == blocks) ? last_block_size : block_size;

            while (1) {
                status = qpl_check_job(ctx->job_c[idx]);

                if (QPL_STS_OK == status) {
                    std::memcpy(&block_compressed_size, &in_header[sizeof(std::uint32_t) * (i + 3)],
                                sizeof(std::uint32_t));
                    std::uint32_t available_out = (i == blocks) ? last_block_size : block_size;

                    if (block_compressed_size > available_out) {
                        QPL_ERR_MSG(status, "bad header value for block compressed size");
                        return;
                    }

                    ctx->job_c[idx]->next_in_ptr   = in_data;
                    ctx->job_c[idx]->available_in  = block_compressed_size;
                    ctx->job_c[idx]->next_out_ptr  = out_byte_ptr;
                    ctx->job_c[idx]->available_out = available_out;
                    ctx->job_c[idx]->op            = qpl_op_decompress;
                    ctx->job_c[idx]->flags         = job_flags;

                    do {
                        status = qpl_submit_job(ctx->job_c[idx]);
                    } while (status == QPL_STS_QUEUES_ARE_BUSY_ERR);

                    QPL_CHECK_STATUS(status, "qpl_submit_job (decompress)");

                    in_data += block_compressed_size;
                    out_byte_ptr += available_out;

                    break;
                } // status ok
                else if (QPL_STS_BEING_PROCESSED == status || QPL_STS_QUEUES_ARE_BUSY_ERR == status) {
                    continue;
                } else {
                    QPL_ERR_MSG(status, "job status error found: ");
                    return;
                }
            }
        }

        std::vector<qpl_status> status_check_c(jobs);

        if (!wait_for_all_jobs(ctx, status_check_c)) {
            QPL_ERR_MSG(status, "wait_for_all_jobs timed out.");
            return;
        }

        *decompressed_size = (blocks * block_size) + last_block_size;
    }
}
