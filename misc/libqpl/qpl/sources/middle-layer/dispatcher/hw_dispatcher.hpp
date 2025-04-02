/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef QPL_SOURCES_MIDDLE_LAYER_DISPATCHER_HW_DISPATCHER_HPP_
#define QPL_SOURCES_MIDDLE_LAYER_DISPATCHER_HW_DISPATCHER_HPP_

#include <array>
#include <atomic>
#include <cstdint>

#include "qpl/c_api/defs.h"

#include "hw_device.hpp"
#include "hw_devices.h"
#include "hw_status.h"

#if defined(__linux__)
#ifdef DYNAMIC_LOADING_LIBACCEL_CONFIG
#include "hw_configuration_driver.h"
#else //DYNAMIC_LOADING_LIBACCEL_CONFIG=OFF
#include "hw_definitions.h"
#include "hw_devices.h"
#include "libaccel_config.h"
#endif //DYNAMIC_LOADING_LIBACCEL_CONFIG
#endif //__linux__

namespace qpl::ml::dispatcher {

class hw_dispatcher final {

    static constexpr uint32_t max_devices = MAX_NUM_DEV;

#if defined(__linux__)

    using device_container_t = std::array<hw_device, max_devices>;

    class hw_context final {
    public:
        void set_driver_context_ptr(accfg_ctx* driver_context_ptr) noexcept;

        [[nodiscard]] auto get_driver_context_ptr() noexcept -> accfg_ctx*;

    private:
        accfg_ctx* driver_context_ptr_ = nullptr; /**< QPL driver context */
    };

#endif //__linux__

public:
    static auto get_instance() noexcept -> hw_dispatcher&;

    [[nodiscard]] auto is_hw_support() const noexcept -> bool;

    [[nodiscard]] auto get_hw_init_status() const noexcept -> hw_accelerator_status;

    void fill_hw_context(hw_accelerator_context* hw_context_ptr) noexcept;

#if defined(__linux__)

    [[nodiscard]] auto begin() const noexcept -> device_container_t::const_iterator;

    [[nodiscard]] auto end() const noexcept -> device_container_t::const_iterator;

    [[nodiscard]] auto device_count() const noexcept -> size_t;

    [[nodiscard]] auto device(size_t idx) const noexcept -> const hw_device&;

#endif //__linux__

    virtual ~hw_dispatcher() noexcept;

protected:
    hw_dispatcher() noexcept;

    auto initialize_hw() noexcept -> hw_accelerator_status;

private:
#if defined(__linux__)
    hw_context         hw_context_;
    device_container_t devices_ {};
    size_t             device_count_ = 0;
#ifdef DYNAMIC_LOADING_LIBACCEL_CONFIG
    hw_driver_t hw_driver_ {};
#endif //DYNAMIC_LOADING_LIBACCEL_CONFIG
#endif //__linux__

    bool                  hw_support_;
    hw_accelerator_status hw_init_status_;
};

} // namespace qpl::ml::dispatcher
#endif //QPL_SOURCES_MIDDLE_LAYER_DISPATCHER_HW_DISPATCHER_HPP_
