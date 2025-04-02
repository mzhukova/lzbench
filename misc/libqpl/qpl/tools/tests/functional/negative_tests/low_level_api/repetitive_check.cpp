/*******************************************************************************
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "qpl/qpl.h"

#include "gtest/gtest.h"

// tool_common
#include "dispatcher_checks.hpp"
#include "util.hpp"

// test_common
#include <iostream>
#include <numeric>

#include "run_operation.hpp"
#include "tn_common.hpp"

namespace qpl::test {

constexpr const uint32_t source_size   = 1000;
constexpr const uint64_t poly          = 0x04C11DB700000000;
constexpr const uint64_t reference_crc = 6467333940108591104;

/**
 * @brief Negative test for repetitive calls to qpl_check_job function in the low-level API
 * based on simple CRC64 operation.
 *
 * Test verifies the behavior of the qpl_check_job function when called multiple times
 * after job submission. It ensures that the function returns the correct status every time
 * and preserves the results of the job (i.e., no overwriting of the results).
 *
 * Additionally, the test verifies that calling qpl_check_job before job submission
 * returns QPL_STS_JOB_NOT_SUBMITTED.
 */
QPL_LOW_LEVEL_API_NEGATIVE_TEST(check, repetitive_calls) {
    // Only test Hardware Path and Asynchronous execution
    QPL_SKIP_TEST_FOR(qpl_path_software);
    QPL_SKIP_TEST_FOR(qpl_path_auto);
    if (!util::TestEnvironment::GetInstance().IsAsynchronousApiTesting()) GTEST_SKIP();

    const qpl_path_t execution_path = util::TestEnvironment::GetInstance().GetExecutionPath();

    // Job initialization for valid CRC64 operation
    std::unique_ptr<uint8_t[]> job_buffer;
    uint32_t                   size = 0;

    ASSERT_EQ(QPL_STS_OK, qpl_get_job_size(execution_path, &size));

    job_buffer   = std::make_unique<uint8_t[]>(size);
    qpl_job* job = reinterpret_cast<qpl_job*>(job_buffer.get());

    ASSERT_EQ(QPL_STS_OK, qpl_init_job(execution_path, job));

    std::vector<uint8_t> source(source_size, 4);
    std::iota(std::begin(source), std::end(source), 0);

    job->op           = qpl_op_crc64;
    job->next_in_ptr  = source.data();
    job->available_in = source_size;
    job->crc64_poly   = poly;

    // A call to qpl_check_job before submission should return an error
    ASSERT_EQ(QPL_STS_JOB_NOT_SUBMITTED, qpl_check_job(job));

    // Proper submission of the job should return QPL_STS_OK
    ASSERT_EQ(QPL_STS_OK, qpl_submit_job(job));

    // clang-format off
    while (QPL_STS_BEING_PROCESSED == qpl_check_job(job));
    // clang-format on

    ASSERT_EQ(QPL_STS_OK, qpl_check_job(job));
    ASSERT_EQ(job->crc64, reference_crc);

    // A call to qpl_check_job multiple times should return the same status
    // and preserve previous results
    for (auto i = 0U; i < 42U; i++) {
        ASSERT_EQ(QPL_STS_OK, qpl_check_job(job));
        ASSERT_EQ(job->crc64, reference_crc);
    }
}

QPL_LOW_LEVEL_API_NEGATIVE_TEST(wait, repetitive_calls) {
    // Only test Hardware Path and Asynchronous execution
    QPL_SKIP_TEST_FOR(qpl_path_software);
    QPL_SKIP_TEST_FOR(qpl_path_auto);
    if (!util::TestEnvironment::GetInstance().IsAsynchronousApiTesting()) GTEST_SKIP();

    const qpl_path_t execution_path = util::TestEnvironment::GetInstance().GetExecutionPath();

    // Job initialization for valid CRC64 operation
    std::unique_ptr<uint8_t[]> job_buffer;
    uint32_t                   size = 0;

    ASSERT_EQ(QPL_STS_OK, qpl_get_job_size(execution_path, &size));

    job_buffer   = std::make_unique<uint8_t[]>(size);
    qpl_job* job = reinterpret_cast<qpl_job*>(job_buffer.get());

    ASSERT_EQ(QPL_STS_OK, qpl_init_job(execution_path, job));

    std::vector<uint8_t> source(source_size, 4);
    std::iota(std::begin(source), std::end(source), 0);

    job->op           = qpl_op_crc64;
    job->next_in_ptr  = source.data();
    job->available_in = source_size;
    job->crc64_poly   = poly;

    // A call to qpl_check_job before submission should return an error
    ASSERT_EQ(QPL_STS_JOB_NOT_SUBMITTED, qpl_wait_job(job));

    // Proper submission of the job should return QPL_STS_OK
    ASSERT_EQ(QPL_STS_OK, qpl_submit_job(job));
    ASSERT_EQ(QPL_STS_OK, qpl_wait_job(job));
    ASSERT_EQ(job->crc64, reference_crc);

    // A call to qpl_check_job multiple times should return the same status
    // and preserve previous results
    for (auto i = 0U; i < 42U; i++) {
        ASSERT_EQ(QPL_STS_OK, qpl_wait_job(job));
        ASSERT_EQ(job->crc64, reference_crc);
    }
}

} // namespace qpl::test
