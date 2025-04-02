/*******************************************************************************
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef QPL_SOURCES_MIDDLE_LAYER_DISPATCHER_QUEUE_SELECTOR_HPP_
#define QPL_SOURCES_MIDDLE_LAYER_DISPATCHER_QUEUE_SELECTOR_HPP_

#include <unordered_map>

#include "hw_devices.h"
#include "hw_iaa_flags.h"
#include "hw_queue.hpp"
#include "util/util.hpp"

namespace qpl::ml::dispatcher {

class queue_selector {
    static constexpr uint32_t max_working_queues = MAX_NUM_WQ;

    // Operation codes for Intel® In-Memory Analytics Accelerator (Intel® IAA)
    // Used to check OPCFG if operation is enabled/disabled
    static constexpr uint32_t opcodes_list[] = {QPL_OPCODE_DECOMPRESS, QPL_OPCODE_COMPRESS, QPL_OPCODE_CRC64,
                                                QPL_OPCODE_SCAN,       QPL_OPCODE_EXTRACT,  QPL_OPCODE_SELECT,
                                                QPL_OPCODE_EXPAND};

    using queues_container_t   = std::array<hw_queue, max_working_queues>;
    using op_config_register_t = std::array<uint32_t, TOTAL_OP_CFG_BIT_GROUPS>;
    using opcfg_container_t    = std::array<op_config_register_t, max_working_queues>;

public:
    queue_selector() = default;

    /**
     * @brief Initialize the queue_selector object.This constructor initializes the map of operation code to disabled wq.
     */
    queue_selector(const queues_container_t& working_queues, const uint8_t total_wq_size) {
        bool op_cfg_enabled = working_queues[0].get_op_configuration_support();

        if (!op_cfg_enabled) {
            for (uint32_t operation : opcodes_list) {
                wq_map_operation_enabled_to_bitmask_[operation] = util::bitmask128(total_wq_size);
            }
        } else {
            for (uint32_t operation : opcodes_list) {
                util::bitmask128 bit_index_is_valid_wq;

                for (uint32_t wq_idx = 0; wq_idx < total_wq_size; wq_idx++) {
                    if (OC_GET_OP_SUPPORTED(working_queues[wq_idx].get_op_config_register(), operation)) {
                        if (wq_idx < 64) {
                            bit_index_is_valid_wq.low |= static_cast<uint64_t>(1U) << wq_idx;
                        } else {
                            bit_index_is_valid_wq.high |= static_cast<uint64_t>(1U) << (wq_idx - 64);
                        }
                    }
                }

                wq_map_operation_enabled_to_bitmask_[operation] = bit_index_is_valid_wq;
            }
        }
    }

    /**
     * @brief Reduce the number of valid WQs based on the operation code. Disabled workqueues are marked as invalid.
     * @param [in]      operation Operation code
     * @param [in, out] bit_index_is_valid_wq Bitmask of size 128 bits (2 uint64_t) where each bit corresponds to a WQ.
     *                                        If bit is set, WQ is valid, otherwise it is disabled.
     */
    void reduce_by_operation(const uint32_t operation, util::bitmask128& bit_index_is_valid_wq) const noexcept {
        if (wq_map_operation_enabled_to_bitmask_.find(operation) != wq_map_operation_enabled_to_bitmask_.end()) {
            bit_index_is_valid_wq.low &= wq_map_operation_enabled_to_bitmask_.at(operation).low;
            bit_index_is_valid_wq.high &= wq_map_operation_enabled_to_bitmask_.at(operation).high;
        }
    }

private:
    /* Map of operation to enabled WQ indexes
     * Key: Operation code
     * Value: LE-64 Bitmask of size 128 bits of WQ indexes where operation is enabled
     */
    std::unordered_map<uint32_t, util::bitmask128> wq_map_operation_enabled_to_bitmask_;
};

} // namespace qpl::ml::dispatcher

#endif // QPL_SOURCES_MIDDLE_LAYER_DISPATCHER_QUEUE_SELECTOR_HPP_
