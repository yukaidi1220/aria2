/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "ServiceBindingSelector.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace aria2 {

namespace dns {

namespace {

const uint16_t SVC_PARAM_KEY_ECH = 5;

bool isSupportedAlpn(const std::string& alpn,
                     const std::vector<std::string>& supportedAlpns)
{
  return std::find(std::begin(supportedAlpns), std::end(supportedAlpns),
                   alpn) != std::end(supportedAlpns);
}

bool selectAlpn(std::string& alpn, bool& defaultAlpnUsed,
                const ServiceBindingRecord& record,
                const ServiceBindingSelectionConfig& config)
{
  for (const auto& supportedAlpn : config.supportedAlpns) {
    if (std::find(std::begin(record.alpn), std::end(record.alpn),
                  supportedAlpn) != std::end(record.alpn)) {
      alpn = supportedAlpn;
      defaultAlpnUsed = false;
      return true;
    }
  }

  if (!record.noDefaultAlpn && !config.defaultAlpn.empty() &&
      isSupportedAlpn(config.defaultAlpn, config.supportedAlpns)) {
    alpn = config.defaultAlpn;
    defaultAlpnUsed = true;
    return true;
  }

  return false;
}

bool mandatoryParamsSupported(const ServiceBindingRecord& record,
                              const ServiceBindingSelectionConfig& config)
{
  for (const auto& key : record.mandatoryKeys) {
    if (key == SVC_PARAM_KEY_ECH && !config.echConfigListEnabled) {
      return false;
    }
  }
  return true;
}

} // namespace

std::vector<std::string>
getServiceBindingAddressHints(const ServiceBindingRecord& record,
                              SvcbAddressFamily family)
{
  std::vector<std::string> hints;
  if (family == SVCB_ADDRESS_FAMILY_UNSPEC ||
      family == SVCB_ADDRESS_FAMILY_IPV6) {
    hints.insert(std::end(hints), std::begin(record.ipv6hint),
                 std::end(record.ipv6hint));
  }
  if (family == SVCB_ADDRESS_FAMILY_UNSPEC ||
      family == SVCB_ADDRESS_FAMILY_IPV4) {
    hints.insert(std::end(hints), std::begin(record.ipv4hint),
                 std::end(record.ipv4hint));
  }
  return hints;
}

std::vector<SelectedServiceBinding>
selectServiceBindings(const std::vector<ServiceBindingRecord>& records,
                      const ServiceBindingSelectionConfig& config)
{
  std::vector<SelectedServiceBinding> result;

  for (const auto& record : records) {
    if (record.priority == 0 || record.aliasModeUnavailable ||
        record.hasUnknownMandatoryKey ||
        !mandatoryParamsSupported(record, config)) {
      continue;
    }

    std::string alpn;
    bool defaultAlpnUsed = false;
    if (!selectAlpn(alpn, defaultAlpnUsed, record, config)) {
      continue;
    }

    auto port = record.hasPort ? record.port : config.defaultPort;
    if (port == 0) {
      continue;
    }

    SelectedServiceBinding selected;
    selected.priority = record.priority;
    selected.targetName = record.targetName.empty() ? record.ownerName
                                                    : record.targetName;
    selected.port = port;
    selected.alpn = std::move(alpn);
    selected.defaultAlpnUsed = defaultAlpnUsed;
    if (config.echConfigListEnabled && !record.echConfigList.empty()) {
      selected.echConfigList = record.echConfigList;
    }
    selected.addressHints =
        getServiceBindingAddressHints(record, config.addressFamily);
    result.push_back(std::move(selected));
  }

  std::stable_sort(std::begin(result), std::end(result),
                   [](const SelectedServiceBinding& lhs,
                      const SelectedServiceBinding& rhs) {
                     return lhs.priority < rhs.priority;
                   });

  return result;
}

std::vector<ServiceBindingEndpoint>
selectServiceBindingEndpoints(const std::vector<ServiceBindingRecord>& records,
                              const std::string& originHost,
                              uint16_t originPort,
                              const ServiceBindingSelectionConfig& config)
{
  auto selections = selectServiceBindings(records, config);
  std::vector<ServiceBindingEndpoint> result;
  result.reserve(selections.size());

  for (auto& selection : selections) {
    ServiceBindingEndpoint endpoint;
    endpoint.originHost = originHost;
    endpoint.originPort = originPort;
    endpoint.connectHost = std::move(selection.targetName);
    endpoint.connectPort = selection.port;
    endpoint.alpn = std::move(selection.alpn);
    endpoint.defaultAlpnUsed = selection.defaultAlpnUsed;
    endpoint.echConfigList = std::move(selection.echConfigList);
    endpoint.addressHints = std::move(selection.addressHints);
    result.push_back(std::move(endpoint));
  }

  return result;
}

} // namespace dns

} // namespace aria2
