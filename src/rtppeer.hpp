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
#include <string>
#include <functional>
#include <arpa/inet.h>

// A random int32. Should be configurable, so different systems, have different SSRC.
inline const uint32_t SSRC = 0x111f6c31;

namespace rtpmidid {
  struct parse_buffer_t;

  class rtppeer {
  public:
    // Commands, the id is the same chars as the name
    enum commands_e {
      IN = 0x494e,
      OK = 0x4f4b,
      NO = 0x4e4f,
      BY = 0x4259,
      CK = 0x434b,
      RS = 0x5253
    };
    enum status_e {
      NOT_CONNECTED = 0,
      CONTROL_CONNECTED = 1,
      MIDI_CONNECTED = 2,
      CONNECTED = 3
    };
    enum port_e {
      MIDI_PORT,
      CONTROL_PORT,
    };

    status_e status;
    uint32_t initiator_id;
    uint32_t remote_ssrc;
    std::string local_name;
    std::string remote_name;
    uint16_t seq_nr_ack;
    uint16_t seq_nr;
    uint16_t remote_seq_nr;
    uint64_t timestamp_start; // Time in ms
    uint64_t latency;
    std::function<void(parse_buffer_t &)> event_midi;
    std::function<void(void)> event_close;
    std::vector<std::function<void(const std::string &)>> event_connect;
    std::function<void(port_e, const parse_buffer_t &)> sendto;

    rtppeer(std::string _name);
    ~rtppeer();

    void on_midi(std::function<void(parse_buffer_t &)> f){
      event_midi = f;
    }
    void on_close(std::function<void(void)> f){
      event_close = f;
    }
    void on_connect(std::function<void(const std::string &)> f){
      event_connect.push_back(f);
    }
    void on_send(std::function<void(port_e, const parse_buffer_t &)> f){
      sendto = f;
    }
    bool is_connected(){
      return status == CONNECTED;
    }
    void reset();
    void data_ready(parse_buffer_t &, port_e port);

    void parse_command(parse_buffer_t &, port_e port);
    void parse_feedback(parse_buffer_t &);
    void parse_command_ok(parse_buffer_t &, port_e port);
    void parse_command_in(parse_buffer_t &, port_e port);
    void parse_command_ck(parse_buffer_t &, port_e port);
    void parse_command_by(parse_buffer_t &, port_e port);
    void parse_midi(parse_buffer_t &);

    void send_midi(parse_buffer_t *buffer);
    void send_goodbye(port_e to_port);
    void connect_to(port_e rtp_port);
    void send_ck0();
    uint64_t get_timestamp();
  };
}
