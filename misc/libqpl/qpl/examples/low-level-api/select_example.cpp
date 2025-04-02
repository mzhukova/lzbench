/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

//* [QPL_LOW_LEVEL_SELECT_EXAMPLE] */

#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include "qpl/qpl.h"

#include "examples_utils.hpp" // for argument parsing function

/**
 * @brief This example requires a command line argument to set the execution path. Valid values are `software_path`
 * and `hardware_path`.
 * In QPL, @ref qpl_path_software (`Software Path`) means that computations will be done with CPU.
 * Accelerator can be used instead of CPU. In this case, @ref qpl_path_hardware (`Hardware Path`) must be specified.
 * If there is no difference where calculations should be done, @ref qpl_path_auto (`Auto Path`) can be used to allow
 * the library to chose the path to execute. The Auto Path usage is not demonstrated by this example.
 *
 * @warning ---! Important !---
 * `Hardware Path` doesn't support all features declared for `Software Path`
 *
 */
constexpr const uint32_t source_size                = 1000;
constexpr const uint32_t boundary                   = 48;
constexpr const uint32_t scan_input_vector_width    = 8;
constexpr const uint32_t select_output_vector_width = 1;
constexpr const uint32_t byte_bit_length            = 8;

auto main(int argc, char** argv) -> int {
    std::cout << "Intel(R) Query Processing Library version is " << qpl_get_library_version() << ".\n";

    // Default to Software Path
    qpl_path_t execution_path = qpl_path_software;

    // Get path from input argument
    const int parse_ret = parse_execution_path(argc, argv, &execution_path);
    if (parse_ret != 0) { return 1; }

    // Source and output containers
    std::vector<uint8_t> source(source_size, 0);
    std::vector<uint8_t> mask_after_scan(source_size / 8, 4);
    std::vector<uint8_t> destination(source_size, 4);

    std::unique_ptr<uint8_t[]> job_buffer;
    uint32_t                   size = 0;

    // Filling source containers
    std::iota(std::begin(source), std::end(source), 0);

    // Job initialization
    qpl_status status = qpl_get_job_size(execution_path, &size);
    if (status != QPL_STS_OK) {
        std::cout << "An error " << status << " acquired during job size getting.\n";
        return 1;
    }

    job_buffer   = std::make_unique<uint8_t[]>(size);
    qpl_job* job = reinterpret_cast<qpl_job*>(job_buffer.get());

    status = qpl_init_job(execution_path, job);
    if (status != QPL_STS_OK) {
        std::cout << "An error " << status << " acquired during job initializing.\n";
        return 1;
    }

    // Performing a scan operation
    job->next_in_ptr        = source.data();
    job->available_in       = source_size;
    job->next_out_ptr       = mask_after_scan.data();
    job->available_out      = static_cast<uint32_t>(mask_after_scan.size());
    job->op                 = qpl_op_scan_eq;
    job->src1_bit_width     = scan_input_vector_width;
    job->num_input_elements = source_size;
    job->out_bit_width      = qpl_ow_nom;
    job->param_low          = boundary;

    status = qpl_execute_job(job);
    if (status != QPL_STS_OK) {
        std::cout << "An error " << status << " acquired during performing scan.\n";
        return 1;
    }

    const auto scan_byte_size = job->total_out;
    const auto mask_length    = scan_byte_size;

    // Performing a select operation
    job->next_in_ptr        = source.data();
    job->available_in       = source_size;
    job->next_out_ptr       = destination.data();
    job->available_out      = static_cast<uint32_t>(destination.size());
    job->op                 = qpl_op_select;
    job->src1_bit_width     = scan_input_vector_width;
    job->num_input_elements = source_size;
    job->out_bit_width      = qpl_ow_nom;
    job->next_src2_ptr      = mask_after_scan.data();
    job->available_src2     = mask_length;
    job->src2_bit_width     = select_output_vector_width;

    status = qpl_execute_job(job);
    if (status != QPL_STS_OK) {
        std::cout << "An error " << status << " acquired during performing select.\n";
        return 1;
    }

    const auto select_byte_size = job->total_out;

    // Freeing resources
    status = qpl_fini_job(job);
    if (status != QPL_STS_OK) {
        std::cout << "An error " << status << " acquired during job finalization.\n";
        return 1;
    }

    // Compare with reference
    for (uint32_t i = 0; i < select_byte_size; i++) {
        if (destination[i] != boundary) {
            std::cout << "Select was done incorrectly.\n";
            return 1;
        }
    }

    std::cout << "Select was performed successfully.\n";

    return 0;
}

//* [QPL_LOW_LEVEL_SELECT_EXAMPLE] */
