/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <sstream>

// Some functions to allow to_stirng to almost everything
namespace std{
  template<typename T>
  std::string to_string(const std::vector<T> &list){
    std::stringstream ss;
    bool first = true;
    ss<<"[";
    for(auto &item: list){
      if (!first)
        ss<<", ";
      else
        first = false;
      ss<<to_string(item);
    }
    ss<<"]";
    return ss.str();
  }

  inline std::string to_string(const std::string &str){
    return str;
  }

  inline bool startswith(const std::string_view &str, const std::string_view &maybe_start){
    if (str.length() < maybe_start.length())
      return false;
    return std::equal(std::begin(maybe_start), std::end(maybe_start), std::begin(str));
  }
  inline bool endswith(const std::string_view &str, const std::string_view &maybe_end){
    auto pos = str.length() - maybe_end.length();
    if (pos < 0)
      return false;
    return std::equal(std::begin(str) + pos, std::end(str), std::begin(maybe_end));
  }
}
namespace rtpmidid{
  std::vector<std::string> split(const std::string &str, char delim=' ');
}
