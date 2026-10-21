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
 * file(s) with this exception, you may extend this exception statement to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#ifndef D_HTTP_TLS_HANDSHAKE_PARAMS_H
#define D_HTTP_TLS_HANDSHAKE_PARAMS_H

#include "common.h"

#ifdef ENABLE_SSL

#include "SocketCore.h"

#include <string>
#include <vector>

namespace aria2 {

class Option;
class Request;

std::vector<std::string> createHttpAlpnProtocols(const Option* option);

TLSHandshakeParams createHttpTLSHandshakeParams(const Request* request,
                                                const Option* option);

// How a failed TLS handshake should be retried.
enum TlsAddressFailover {
  // Rethrow the TLS error and abort this request.
  TLS_ADDRESS_FAILOVER_ABORT,
  // Retry with an address of the other address family (IPv4 failed,
  // try IPv6, and vice versa).
  TLS_ADDRESS_FAILOVER_OTHER_FAMILY,
  // Retry with any other resolved address of the same host.
  TLS_ADDRESS_FAILOVER_ANY_ADDRESS,
};

// Decides how to retry after the TLS handshake on |failedFamily| failed.
//
// When |verifyPeer| is false (certificate verification disabled), any other
// resolved address of the same host may be retried. When |verifyPeer| is
// true, a handshake or certificate failure may be caused by interference
// that only affects one address family (for example a middlebox that
// resets or hijacks TLS on IPv4 while IPv6 still works), so the retry
// switches to the other address family only, and only once: if the other
// family has also failed recently, the request aborts instead of looping
// through addresses. Every retry still verifies the peer certificate, so
// this never accepts an invalid certificate.
TlsAddressFailover decideTlsAddressFailover(int failedFamily, bool verifyPeer,
                                            bool hasOppositeFamilyAddress,
                                            bool oppositeFamilyPenalized,
                                            bool hasAnyOtherAddress);

// Decides whether a failed HTTPS TLS handshake may be retried once without
// the SNI extension, still verifying the peer certificate against the
// request hostname. |backendCanOmitSNI| tells whether the TLS backend of
// the failed session can omit the extension while still verifying the peer.
//
// The retry is skipped when the request is not HTTPS, when
// --tls-sni-none-fallback is disabled, when this request was already retried
// without SNI (otherwise the retry would loop), when the backend cannot omit
// SNI, when SNI is already omitted, and when ECH is configured (ECH owns the
// outer name and cannot be combined with an override).
//
// Like createHttpTLSHandshakeParams(), this throws DlAbortEx for a
// misconfigured combination (for example ECH with an SNI override).
bool shouldRetryTLSHandshakeWithoutSNI(const Request* request,
                                       const Option* option,
                                       bool backendCanOmitSNI);

// Decides whether a failed HTTPS TLS handshake may be retried with another
// resolved address, given the handshake parameters of the failed attempt.
// An explicit SNI override pins the SNI semantics to the chosen hostname, so
// address rotation is skipped (an override identical to the request hostname
// still blocks it); omitting SNI does not block it.
bool tlsAddressFailoverAllowed(const TLSHandshakeParams& params);

} // namespace aria2

#endif // ENABLE_SSL

#endif // D_HTTP_TLS_HANDSHAKE_PARAMS_H
