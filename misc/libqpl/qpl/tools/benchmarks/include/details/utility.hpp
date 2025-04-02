/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "cmd_decl.hpp"

#ifdef __linux__
#include <time.h>
#endif
#include <map>
#include <types.hpp>
#include <vector>
#include <x86intrin.h>

//
// Defines
//
#define ERROR(M, ...) fprintf(stderr, "[ERROR] " M "\n", ##__VA_ARGS__)
#define ASSERT(C, M, ...)        \
    if (!(C)) {                  \
        ERROR(M, ##__VA_ARGS__); \
        exit(EXIT_FAILURE);      \
    }
#define ASSERT_NM(C) \
    if (!(C)) { exit(EXIT_FAILURE); }

#define BENCHMARK_SET_DELAYED_INT(UNIQUE_REG, UNIQUE_SING)                \
    void UNIQUE_REG();                                                    \
    class UNIQUE_SING {                                                   \
    public:                                                               \
        UNIQUE_SING() {                                                   \
            auto& reg = bench::details::get_registry();                   \
            reg.push_back(UNIQUE_REG);                                    \
        }                                                                 \
    };                                                                    \
    static UNIQUE_SING BENCHMARK_PRIVATE_NAME(_local_register_instance_); \
    void               UNIQUE_REG()

namespace bench::details {
//
// Registration utils
//
using registry_call_t = void (*)();
using registry_t      = std::vector<registry_call_t>;
registry_t& get_registry();

//
// Device utils
//
std::uint32_t get_number_of_devices_matching_numa_policy(std::uint32_t user_specified_numa_id) noexcept;

constexpr std::uint64_t submitRetryWaitNs = 0U;

inline void retry_sleep() {
    if constexpr (submitRetryWaitNs == 0U)
        return;
    else {
#ifdef __linux__
        timespec spec {0, submitRetryWaitNs};
        nanosleep(&spec, nullptr);
#else
#endif
    }
}

template <typename RangeT>
inline void mem_control(RangeT begin, RangeT end, mem_loc_e op) noexcept {
    for (auto line = begin; line < end; line += 64U) {
        __builtin_ia32_clflush(&(*line));

        volatile char* m = (char*)&(*line);
        if (op == mem_loc_e::cache)
            *m = *m;
        else if (op == mem_loc_e::llc) {
            *m = *m;
            // CLDEMOTE
            asm volatile(".byte 0x0f, 0x1c, 0x07\t\n" ::"D"(m));
        }
    }
    __builtin_ia32_mfence();
}
} // namespace bench::details
