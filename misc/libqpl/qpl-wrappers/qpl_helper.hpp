/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "qpl/qpl.h"

struct QPLCompressionContext {
    qpl_job*              job = NULL;
    std::vector<qpl_job*> job_c;
    int                   jobs_number;
    size_t                block_size;
};

/**
 * @brief Allocates and initializes a QPLCompressionContext structure.
 *
 * @param blocks_number The number of blocks to be used in the compression context.
 * @return A pointer to the allocated QPLCompressionContext structure, or NULL if the allocation fails.
 *
 * @note If any allocation fails, the function frees all previously allocated memory and returns NULL.
 */
QPLCompressionContext* allocate_qpl_context(size_t blocks_number);

/**
 * @brief Initializes the QPL compression context.
 *
 * This function initializes the QPL compression context by setting up the job structures
 * for the main context and each block within the context. It uses the `qpl_init_job` function
 * to perform the initialization.
 *
 * @param ctx A pointer to the QPLCompressionContext structure that holds the context information.
 * @return Returns true if the initialization is successful for all jobs, otherwise returns false.
 */
bool initialize_qpl_context(QPLCompressionContext* ctx);

/**
 * @brief Frees the QPL compression context and its associated resources.
 *
 * This function releases the memory allocated for the QPL compression context,
 * including the jobs and job contexts. If the context was initialized, it also
 * finalizes the jobs before freeing them.
 *
 * @param ctx Pointer to the QPLCompressionContext to be freed.
 * @param initialized Boolean flag indicating whether the context was initialized.
 *                    If true, the jobs will be finalized before being freed.
 */
void free_qpl_context(QPLCompressionContext* ctx, bool initialized);

/**
 * @brief Compresses the input data using the QPL.
 *
 * This function compresses the input data and stores the compressed data in the provided buffer.
 * It supports both single-block and multi-block compression.
 *
 * @param ctx Pointer to the QPL compression context, which contains the job and job_c structures.
 * @param input_data Pointer to the input data to be compressed.
 * @param input_size Size of the input data in bytes.
 * @param compressed_data Pointer to the buffer where the compressed data will be stored.
 * @param compressed_size Pointer to the size of the compressed data buffer. On return, it will contain the size of the compressed data.
 * @param compression_level Compression level to be used.
 * @param use_dynamic_huffman Boolean flag indicating whether to use dynamic compression mode.
 *
 * @note If the number of blocks (ctx->blocks_number) is 1, the function performs single-block compression.
 *       Otherwise, it performs multi-block compression, dividing the input data into multiple blocks.
 *
 * @warning If any QPL job fails (either qpl_execute_job, qpl_submit_job, or qpl_check_job), the function prints an error message
 *          to std::cerr and returns without updating the compressed_size.
 */
void compress(QPLCompressionContext* ctx, char* input_data, size_t input_size,
              char* compressed_data, size_t* compressed_size,
              qpl_compression_levels compression_level, bool use_dynamic_huffman);

/**
 * @brief Decompresses data using QPL.
 *
 * This function decompresses the given compressed data and stores the result in the provided decompressed data buffer.
 * It supports both single-block and multi-block decompression.
 *
 * @param ctx Pointer to the QPL compression context.
 * @param compressed_data Pointer to the compressed data buffer.
 * @param compressed_size Size of the compressed data buffer.
 * @param decompressed_data Pointer to the buffer where decompressed data will be stored.
 * @param decompressed_size Pointer to the size of the decompressed data buffer. This value will be updated with the actual decompressed size.
 * @param use_dynamic_huffman Boolean flag indicating whether to use dynamic Huffman coding.
 *
 * @note If the decompression fails, an error message will be printed to std::cerr.
 */
void decompress(QPLCompressionContext* ctx, char* compressed_data, size_t compressed_size,
                char* decompressed_data, size_t* decompressed_size, bool use_dynamic_huffman);
