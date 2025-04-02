/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <benchmark/benchmark.h>
#include <utility.hpp>

#include "qpl/qpl.h"

#include "cmd_decl.hpp" // cmd flags

// tool_hw_dispatcher
#include "test_hw_device.hpp"
#include "test_hw_dispatcher.hpp"

// tool_common
#include "system_info.hpp"

#if defined(__linux__)
#include <sys/utsname.h>
#endif
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>

namespace bench::details {
//
// Utilities implementations
//
registry_t& get_registry() {
    static registry_t reg;
    return reg;
}

constexpr const uint64_t poly = 0x04C11DB700000000U;

static bool init_hw() {
    uint32_t size = 0U;

    qpl_status status = qpl_get_job_size(qpl_path_hardware, &size);
    if (status != QPL_STS_OK) throw std::runtime_error("hw init failed in qpl_get_job_size");

    const std::unique_ptr<std::uint8_t[]> job_buffer(new std::uint8_t[size]);

    qpl_job* job = reinterpret_cast<qpl_job*>(job_buffer.get());
    status       = qpl_init_job(qpl_path_hardware, job);
    if (status != QPL_STS_OK) throw std::runtime_error("hw init failed in qpl_init_job");

    int data          = 0;
    job->next_in_ptr  = (std::uint8_t*)&data;
    job->available_in = 4;
    job->op           = qpl_op_crc64;
    job->crc64_poly   = poly;
    job->numa_id      = bench::cmd::FLAGS_node;

    status = qpl_submit_job(job);
    if (status != QPL_STS_OK) throw std::runtime_error("hw init failed in qpl_submit_job");

    status = qpl_wait_job(job);
    if (status != QPL_STS_OK) throw std::runtime_error("hw init failed in qpl_wait_job");

    status = qpl_fini_job(job);
    if (status != QPL_STS_OK) throw std::runtime_error("hw init failed in qpl_fini_job");

    return true;
}

std::uint32_t get_number_of_devices_matching_numa_policy(std::uint32_t user_specified_numa_id) noexcept {
    static auto& disp    = qpl::test::hw_dispatcher::get_instance();
    int          counter = 0;
    for (auto& device : disp) {
        if (qpl::test::is_device_matching_user_numa_policy(device.numa_id(), device.socket_id(),
                                                           user_specified_numa_id))
            counter++;
    }
    return counter;
}

} // namespace bench::details

//
// GBench command line extension
//
namespace bench::cmd {
// Default values for command line parameters
BM_DEFINE_string(block_size, "-1");
BM_DEFINE_int32(queue_size, 0);
BM_DEFINE_int32(threads, 0);
BM_DEFINE_int32(node, -1);
BM_DEFINE_string(dataset, "");
BM_DEFINE_string(in_mem, "llc");
BM_DEFINE_string(out_mem, "cс_ram");
BM_DEFINE_bool(full_time, false);
BM_DEFINE_bool(no_hw, false);

/**
 * Print the help message that includes information about the input parameters that can be used to configure the benchmark.
 * These parameters allow specifying the dataset path, block size, queue size, number of threads, NUMA node,
 * input and output memory types.
 *
 * Example:
 *   ./qpl_benchmark --dataset=/path/to/dataset --block_size=4096 --queue_size=16 --threads=4 --node=0
 *                   --in_mem=llc --out_mem=cache_ram
 */
static void print_help() {
    fprintf(stdout,
            "benchmark [--dataset=<path>]            - Path to folder containing dataset.\n"
            "          [--block_size=<size>]         - Input data is split by blocks of specified size and each block is processed separately.\n"
            "                                          If not specified, benchmarks would iterate over multiple block_sizes, incl. processing the full file.\n"
            "          [--queue_size=<size>]         - Number of tasks for a single device.\n"
            "          [--threads=<num>]             - Number of threads for asynchronous execution.\n"
            "          [--node=<num>]                - NUMA node for device selection.\n"
            "                                          If not specified, devices with NUMA nodes matching the NUMA node of the calling process are selected.\n"
            "          [--in_mem=<location>]         - Input memory type: cache, llc or ram. Set to llc by default. \n"
            "          [--out_mem=<location>]        - Output memory type: cache_ram or ram. Set to cache_ram by default. \n"
            "          [--full_time]                 - Include initialization and destruction into measured time. Off by default.\n"
            "          [--no_hw]                     - Skip accelerator initialization check and run only using qpl_software_path. Off by default.\n");
}

static void parse_cmd_line(int* argc, char** argv) {
    for (int i = 1; argc && i < *argc; ++i) {
        if (benchmark::ParseStringFlag(argv[i], "dataset", &FLAGS_dataset) ||
            benchmark::ParseStringFlag(argv[i], "block_size", &FLAGS_block_size) ||
            benchmark::ParseInt32Flag(argv[i], "queue_size", &FLAGS_queue_size) ||
            benchmark::ParseInt32Flag(argv[i], "threads", &FLAGS_threads) ||
            benchmark::ParseInt32Flag(argv[i], "node", &FLAGS_node) ||
            benchmark::ParseStringFlag(argv[i], "in_mem", &FLAGS_in_mem) ||
            benchmark::ParseStringFlag(argv[i], "out_mem", &FLAGS_out_mem) ||
            benchmark::ParseBoolFlag(argv[i], "full_time", &FLAGS_full_time) ||
            benchmark::ParseBoolFlag(argv[i], "no_hw", &FLAGS_no_hw)) {
            for (int j = i; j != *argc - 1; ++j)
                argv[j] = argv[j + 1];

            --(*argc);
            --i;
        } else if (benchmark::IsFlag(argv[i], "help"))
            print_help();
    }
}

std::int32_t get_block_size() {
    static std::int32_t block_size = -1;
    if (block_size < 0) {
        auto str = FLAGS_block_size;
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);

        std::int32_t mult = 1;
        if ((str.size() > 2 && str.find("KB", str.size() - 2) == str.size() - 2) ||
            (str.size() > 1 && str.find("K", str.size() - 1) == str.size() - 1))
            mult = 1024;
        else if ((str.size() > 2 && str.find("MB", str.size() - 2) == str.size() - 2) ||
                 (str.size() > 1 && str.find("M", str.size() - 1) == str.size() - 1))
            mult = 1024 * 1024;

        block_size = std::atoi(str.c_str());
        if (block_size == 0 && str != "0") throw std::runtime_error("invalid block size format");
        block_size *= mult;
    }
    return block_size;
}

mem_loc_e get_in_mem() {
    static mem_loc_e mem = (mem_loc_e)-1;
    if ((std::int32_t)mem < 0) {
        auto str = FLAGS_in_mem;
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        if (str == "cache")
            mem = mem_loc_e::cache;
        else if (str == "llc")
            mem = mem_loc_e::llc;
        else if (str == "ram")
            mem = mem_loc_e::ram;
        else if (str == "pmem")
            mem = mem_loc_e::pmem;
        else
            throw std::runtime_error("invalid input memory location");
    }
    return mem;
}

mem_loc_e get_out_mem() {
    static mem_loc_e mem = (mem_loc_e)-1;
    if ((std::int32_t)mem < 0) {
        auto str = FLAGS_out_mem;
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        if (str == "ram")
            mem = mem_loc_e::ram;
        else if (str == "pmem")
            mem = mem_loc_e::pmem;
        else if (str == "cс_ram")
            mem = mem_loc_e::cc_ram;
        else if (str == "сс_pmem")
            mem = mem_loc_e::cc_pmem;
        else
            throw std::runtime_error("invalid output memory location");
    }
    return mem;
}
} // namespace bench::cmd

namespace bench {
std::vector<std::string> FILTER_op;
std::vector<std::string> FILTER_path;
std::vector<std::string> FILTER_execution_mode;
std::vector<std::string> FILTER_compression_mode;

/**
 * Parses the benchmark filter string and initialize the corresponding vectors
 * based on the parsed values.
 *
 * @param filter_string The benchmark filter string to parse.
 */
static void parse_benchmark_filter(const std::string& filter_string) {
    // If the filter is empty or negative, skip and register all benchmarks
    if (filter_string.empty() || filter_string[0] == '-') return;

    const std::regex            re("([a-zA-Z_]+)");
    std::smatch                 match;
    std::string::const_iterator search_start(filter_string.cbegin());
    while (std::regex_search(search_start, filter_string.cend(), match, re)) {
        const std::string value = match[1].str();
        if (value == "inflate" || value == "deflate" || value == "crc64")
            FILTER_op.push_back(value);
        else if (value == "gen_path") {
            /**
             * Skip the next value if it is "cpu" or "iaa" to not include it in the FILTER_path,
             * as it is only related to the generator path.
            */
            auto next_value = match.suffix().str();
            if (next_value.find(":cpu") == 0 || next_value.find(":iaa") == 0) {
                search_start = match.suffix().first + 4U /* e.g., length of ":cpu" */;
                continue;
            }
        } else if (value == "iaa" || value == "cpu")
            FILTER_path.push_back(value);
        else if (value == "fixed" || value == "dynamic" || value == "static" || value == "canned")
            FILTER_compression_mode.push_back(value);
        else if (value == "sync" || value == "async")
            FILTER_execution_mode.push_back(value);
        search_start = match.suffix().first;
    }
}
} // namespace bench

namespace bench {
std::string format(const char* format, ...) noexcept {
    std::string out;
    size_t      size = 0;

    va_list argptr1, argptr2;
    va_start(argptr1, format);
    va_copy(argptr2, argptr1);
    size = vsnprintf(NULL, 0, format, argptr1);
    va_end(argptr1);

    out.resize(size + 1);
    vsnprintf(out.data(), out.size(), format, argptr2);
    va_end(argptr2);
    out.resize(out.size() - 1);

    return out;
}
} // namespace bench

//
// Main
//
int main(int argc, char** argv) //NOLINT(bugprone-exception-escape)
{
    // Parse command line arguments
    bench::cmd::parse_cmd_line(&argc, argv);

    // Initialize the benchmark framework
    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;

    // Retrieve system information
    auto& sys_info = qpl::test::get_sys_info();
    std::cout << sys_info;

    // Initialize accelerator hardware if enabled
    if (!bench::cmd::FLAGS_no_hw) bench::details::init_hw();

    // Parse the benchmark filter
    bench::parse_benchmark_filter(benchmark::GetBenchmarkFilter());

    // Register benchmarks
    auto& registry = bench::details::get_registry();
    for (auto& reg : registry)
        reg();

    // Run benchmarks
    ::benchmark::RunSpecifiedBenchmarks();

    // Shutdown the benchmark framework
    ::benchmark::Shutdown();

    return 0;
}
