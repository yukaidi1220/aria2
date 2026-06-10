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
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "StartupOptionLog.h"

#include <cstring>

#include "Option.h"
#include "OptionHandler.h"
#include "OptionParser.h"
#include "fmt.h"

namespace aria2 {

const char* optionSourceName(size_t depth, bool confPrecedence)
{
  if (depth == 0) {
    return confPrecedence ? "conf" : "command";
  }
  if (depth == 1) {
    return confPrecedence ? "command" : "conf";
  }
  return "default";
}

std::string getStartupOptionSource(const Option* option, PrefPtr pref,
                                   const OptionParser* parser)
{
  auto confPrecedence = option->get(PREF_CONF_PRECEDENCE) == "conf";
  for (size_t depth = 0; option; ++depth, option = option->getParent().get()) {
    if (!option->definedLocal(pref)) {
      continue;
    }
    auto source = optionSourceName(depth, confPrecedence);
    auto handler = parser->find(pref);
    if (strcmp(source, "default") == 0 && handler &&
        option->get(pref) != handler->getDefaultValue()) {
      return "runtime";
    }
    return source;
  }
  return "unset";
}

std::string formatStartupOptionLog(const Option* option, PrefPtr pref,
                                   const OptionParser* parser)
{
  return fmt("Option: %s=%s (source=%s)", pref->k, option->get(pref).c_str(),
             getStartupOptionSource(option, pref, parser).c_str());
}

} // namespace aria2
