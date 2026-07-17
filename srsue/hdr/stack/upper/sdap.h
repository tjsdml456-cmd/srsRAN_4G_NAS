/**
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#ifndef SRSUE_SDAP_H
#define SRSUE_SDAP_H

#include "srsran/common/buffer_pool.h"
#include "srsran/common/common.h"
#include "srsran/common/common_nr.h"
#include "srsran/common/tti_point.h"
#include "srsran/interfaces/ue_gw_interfaces.h"
#include "srsran/interfaces/ue_pdcp_interfaces.h"
#include "srsran/interfaces/ue_sdap_interfaces.h"
#include <functional>

namespace srsue {

class sdap final : public sdap_interface_pdcp_nr, public sdap_interface_gw_nr, public sdap_interface_rrc
{
public:
  static constexpr uint8_t  DSCP_UNSET             = 0xFF;
  /// Ignore tiny UL packets (e.g. iperf control) for DSCP phase change / BSR+SR.
  static constexpr uint32_t MIN_DSCP_PHASE_BYTES = 64;

  explicit sdap(const char* logname);
  bool init(pdcp_interface_sdap_nr*                pdcp_,
            srsue::gw_interface_pdcp*              gw_,
            std::function<srsran::tti_point()> get_tti_ = {});
  void stop();

  // Interface for GW
  void write_pdu(uint32_t lcid, srsran::unique_byte_buffer_t pdu) final;

  // Interface for PDCP
  void write_sdu(uint32_t lcid, srsran::unique_byte_buffer_t pdu) final;

  // Interface for RRC
  bool set_bearer_cfg(uint32_t lcid, const sdap_interface_rrc::bearer_cfg_t& cfg) final;

private:
  pdcp_interface_sdap_nr*              m_pdcp = nullptr;
  gw_interface_pdcp*                   m_gw   = nullptr;
  std::function<srsran::tti_point()> get_tti;

  // state
  bool running = false;

  // configuration
  std::array<sdap_interface_rrc::bearer_cfg_t, srsran::MAX_NR_NOF_BEARERS> bearers = {};
  // DSCP value of the current traffic phase; matching packets use the RLC priority queue
  std::array<uint8_t, srsran::MAX_NR_NOF_BEARERS> priority_dscp = {};

  srslog::basic_logger& logger;
};

} // namespace srsue

#endif // SRSUE_SDAP_H

