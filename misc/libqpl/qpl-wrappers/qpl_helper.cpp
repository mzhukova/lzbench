#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "qpl/qpl.h"
#include "qpl_helper.hpp"

QPLCompressionContext* allocate_qpl_context(size_t blocks_number) {
    QPLCompressionContext* ctx = (QPLCompressionContext*)malloc(sizeof(QPLCompressionContext));
    if (!ctx) return NULL;

    ctx->blocks_number = blocks_number;

    qpl_path_t path     = qpl_path_auto;
    uint32_t   job_size = 0;
    qpl_status status   = qpl_get_job_size(path, &job_size);
    if (status != QPL_STS_OK) {
        std::cerr << "qpl_get_job_size failed with status: " << status << std::endl;
        free(ctx);
        return NULL;
    }

    ctx->job = (qpl_job*)malloc(job_size);
    if (!ctx->job) {
        free(ctx);
        return NULL;
    }

    ctx->job_c = std::vector<qpl_job*>(ctx->blocks_number, nullptr);
    for (size_t i = 0; i < ctx->blocks_number; ++i) {
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
        std::cerr << "qpl_init_job failed with status: " << status << std::endl;
        return false;
    }

    for (size_t i = 0; i < ctx->blocks_number; ++i) {
        status = qpl_init_job(path, ctx->job_c[i]);
        if (status != QPL_STS_OK) {
            std::cerr << "qpl_init_job failed with status: " << status << std::endl;
            return false;
        }
    }

    return true;
}

void free_qpl_context(QPLCompressionContext* ctx, bool initialized) {
    if (!ctx) return;

    if (initialized) {
        qpl_fini_job(ctx->job);
        for (size_t i = 0; i < ctx->blocks_number; ++i) {
            qpl_fini_job(ctx->job_c[i]);
        }
    }

    for (size_t i = 0; i < ctx->blocks_number; ++i) {
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
        for (size_t i = 0; i < ctx->blocks_number; ++i) {
            status_check_c[i] = qpl_check_job(ctx->job_c[i]);
            if (status_check_c[i] == QPL_STS_BEING_PROCESSED ||
                status_check_c[i] == QPL_STS_QUEUES_ARE_BUSY_ERR) {
                all_jobs_done = false;
            }
        }
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time =
                std::chrono::duration_cast<std::chrono::minutes>(current_time - start_time).count();
        if (elapsed_time >= 1) { return false; }
    } while (!all_jobs_done);
    return true;
}

#ifdef DEBUG
#define QPL_CHECK_STATUS(status, step)                                                             \
    if ((status) != QPL_STS_OK) {                                                                  \
        std::cerr << "QPL error at step: " << (step) << " with status: " << (status) << std::endl; \
        return;                                                                                    \
    }
#else
#define QPL_CHECK_STATUS(status, step) \
    if ((status) != QPL_STS_OK) { return; }
#endif

void compress(QPLCompressionContext* ctx, char* input_data, size_t input_size,
              char* compressed_data, size_t* compressed_size,
              qpl_compression_levels compression_level, bool use_dynamic_huffman) {
    if (ctx->blocks_number == 1) {
        ctx->job->next_in_ptr   = reinterpret_cast<uint8_t*>(input_data);
        ctx->job->available_in  = input_size;
        ctx->job->next_out_ptr  = reinterpret_cast<uint8_t*>(compressed_data);
        ctx->job->available_out = *compressed_size;
        ctx->job->level         = compression_level;
        ctx->job->op            = qpl_op_compress;
        ctx->job->flags         = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
        if (use_dynamic_huffman) { ctx->job->flags |= QPL_FLAG_DYNAMIC_HUFFMAN; }

        qpl_status status = qpl_execute_job(ctx->job);
        QPL_CHECK_STATUS(status, "qpl_execute_job (compress)");

        *compressed_size = ctx->job->total_out;
    } else {
        auto job_flags = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
        if (use_dynamic_huffman) { job_flags |= QPL_FLAG_DYNAMIC_HUFFMAN; }

        std::uint32_t block_size      = input_size / ctx->blocks_number;
        std::uint32_t last_block_size = block_size + (input_size % ctx->blocks_number);

        std::uint32_t           header_offset = ((ctx->blocks_number + 2) * sizeof(std::uint32_t));
        std::vector<qpl_status> status_check_c(ctx->blocks_number);
        for (size_t i = 0; i < ctx->blocks_number; ++i) {
            ctx->job_c[i]->next_in_ptr = reinterpret_cast<uint8_t*>(&input_data[i * block_size]);
            ctx->job_c[i]->available_in =
                    (i == ctx->blocks_number - 1) ? last_block_size : block_size;
            ctx->job_c[i]->next_out_ptr = reinterpret_cast<uint8_t*>(
                    &compressed_data[header_offset +
                                     (i * (*compressed_size) / ctx->blocks_number)]);
            ctx->job_c[i]->available_out = *compressed_size / ctx->blocks_number;
            ctx->job_c[i]->level         = compression_level;
            ctx->job_c[i]->op            = qpl_op_compress;
            ctx->job_c[i]->flags         = job_flags;

            qpl_status status;
            do {
                status = qpl_submit_job(ctx->job_c[i]);
            } while (status == QPL_STS_QUEUES_ARE_BUSY_ERR);

            QPL_CHECK_STATUS(status, "qpl_submit_job (compress)");
        }

        if (!wait_for_all_jobs(ctx, status_check_c)) {
#ifdef DEBUG
            std::cerr << "wait_for_all_jobs timed out." << std::endl;
#endif
            return;
        }

        for (size_t i = 0; i < ctx->blocks_number; ++i) {
            QPL_CHECK_STATUS(status_check_c[i], "qpl_check_job (compress)");
        }

        std::uint32_t updated_size   = header_offset;
        uint8_t*      result_out_ptr = reinterpret_cast<uint8_t*>(&compressed_data[header_offset]);
        for (size_t i = 0; i < ctx->blocks_number; ++i) {
            std::memcpy(
                    result_out_ptr,
                    &compressed_data[header_offset + (i * (*compressed_size) / ctx->blocks_number)],
                    ctx->job_c[i]->total_out);
            result_out_ptr += ctx->job_c[i]->total_out;
        }

        std::memcpy(&compressed_data[0], &block_size, sizeof(std::uint32_t));
        std::memcpy(&compressed_data[sizeof(std::uint32_t)], &last_block_size,
                    sizeof(std::uint32_t));
        for (size_t i = 0; i < ctx->blocks_number; ++i) {
            std::uint32_t block_comp_size = ctx->job_c[i]->total_out;

            std::memcpy(&compressed_data[(i + 2) * sizeof(std::uint32_t)], &block_comp_size,
                        sizeof(std::uint32_t));
            updated_size += block_comp_size;
        }

        *compressed_size = updated_size;
    }
}

void decompress(QPLCompressionContext* ctx, char* compressed_data, size_t compressed_size,
                char* decompressed_data, size_t* decompressed_size, bool use_dynamic_huffman) {
    if (ctx->blocks_number == 1) {
        ctx->job->next_in_ptr   = reinterpret_cast<uint8_t*>(compressed_data);
        ctx->job->available_in  = compressed_size;
        ctx->job->next_out_ptr  = reinterpret_cast<uint8_t*>(decompressed_data);
        ctx->job->available_out = *decompressed_size;
        ctx->job->op            = qpl_op_decompress;
        ctx->job->flags         = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
        if (use_dynamic_huffman) { ctx->job->flags |= QPL_FLAG_DYNAMIC_HUFFMAN; }

        qpl_status status = qpl_execute_job(ctx->job);
        QPL_CHECK_STATUS(status, "qpl_execute_job (decompress)");

        *decompressed_size = ctx->job->total_out;
    } else {
        std::uint32_t header_offset = ((ctx->blocks_number + 2) * sizeof(std::uint32_t));

        auto job_flags = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
        if (use_dynamic_huffman) { job_flags |= QPL_FLAG_DYNAMIC_HUFFMAN; }

        std::uint32_t block_uncompressed_size;
        std::uint32_t last_block_uncompressed_size;
        std::memcpy(&block_uncompressed_size, &compressed_data[0], sizeof(std::uint32_t));
        std::memcpy(&last_block_uncompressed_size, &compressed_data[sizeof(std::uint32_t)],
                    sizeof(std::uint32_t));

        std::uint32_t block_compressed_size;
        std::uint8_t* in_byte_ptr =
                reinterpret_cast<std::uint8_t*>(&compressed_data[header_offset]);
        std::uint8_t* out_byte_ptr = reinterpret_cast<std::uint8_t*>(&decompressed_data[0]);
        for (size_t i = 0; i < ctx->blocks_number; i += 1) {
            std::memcpy(&block_compressed_size, &compressed_data[sizeof(std::uint32_t) * (i + 2)],
                        sizeof(std::uint32_t));
            ctx->job_c[i]->next_in_ptr   = in_byte_ptr;
            ctx->job_c[i]->available_in  = block_compressed_size;
            ctx->job_c[i]->next_out_ptr  = out_byte_ptr;
            ctx->job_c[i]->available_out = (i == (ctx->blocks_number - 1))
                                                   ? last_block_uncompressed_size
                                                   : block_uncompressed_size;
            ctx->job_c[i]->op            = qpl_op_decompress;
            ctx->job_c[i]->flags         = job_flags;

            qpl_status status;
            do {
                status = qpl_submit_job(ctx->job_c[i]);
            } while (status == QPL_STS_QUEUES_ARE_BUSY_ERR);

            QPL_CHECK_STATUS(status, "qpl_submit_job (decompress)");

            in_byte_ptr += block_compressed_size;
            out_byte_ptr += block_uncompressed_size;
        }

        std::vector<qpl_status> status_check_c(ctx->blocks_number);

        if (!wait_for_all_jobs(ctx, status_check_c)) {
#ifdef DEBUG
            std::cerr << "wait_for_all_jobs timed out." << std::endl;
#endif
            return;
        }

        for (size_t i = 0; i < ctx->blocks_number; ++i) {
            QPL_CHECK_STATUS(status_check_c[i], "qpl_check_job (decompress)");
        }

        *decompressed_size = 0;
        for (const auto& job : ctx->job_c) {
            *decompressed_size += job->total_out;
        }
    }
}
