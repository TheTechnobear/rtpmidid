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
#include <stdlib.h>
#include <string>

#include "./rtpmidid.hpp"
#include "./aseq.hpp"
#include "./rtpserver.hpp"
#include "./rtpclient.hpp"
#include "./logger.hpp"
#include "./netutils.hpp"
#include "./config.hpp"
#include "./stringpp.hpp"

using namespace rtpmidid;

rtpmidid_t::rtpmidid_t(config_t *config) :
    name(config->name),
    seq(fmt::format("rtpmidi {}", name)) {
  setup_mdns();
  setup_alsa_seq();

  for (auto &port: config->ports){
    auto server = add_rtpmidid_import_server(config->name, port);
    servers.push_back(std::move(server));
  }

  for (auto &connect_to: config->connect_to){
    auto s = ::rtpmidid::split(connect_to, ':');
    if (s.size() == 1){
      add_rtpmidi_client(s[0], s[0], 5004);
    }
    else if (s.size() == 2){
      add_rtpmidi_client(s[0], s[0], std::stoi(s[1].c_str()));
    }
    else if (s.size() == 3){
      add_rtpmidi_client(s[0], s[1], std::stoi(s[2].c_str()));
    }
    else {
      ERROR("Invalid remote address. Format is ip, name:ip, or name:ip:port. {}", s.size());
      throw exception("Invalid remote address to connect to.");
    }
  }
}

void rtpmidid_t::announce_rtpmidid_server(const std::string &name, uint16_t port){
  mdns_rtpmidi.announce_rtpmidi(name, port);
}

void rtpmidid_t::unannounce_rtpmidid_server(const std::string &name, uint16_t port){
  mdns_rtpmidi.unannounce_rtpmidi(name, port);
}

std::shared_ptr<rtpserver> rtpmidid_t::add_rtpmidid_import_server(const std::string &name, uint16_t port){
  auto rtpserver = std::make_shared<::rtpmidid::rtpserver>(name, port);

  announce_rtpmidid_server(name, rtpserver->control_port);

  auto wrtpserver = std::weak_ptr(rtpserver);
  rtpserver->on_connected([this, wrtpserver, port](std::shared_ptr<::rtpmidid::rtppeer> peer){
    if (wrtpserver.expired()){
      return;
    }
    auto rtpserver = wrtpserver.lock();

    INFO("Remote client connects to local server at port {}. Name: {}", port, peer->remote_name);
    auto aseq_port = seq.create_port(peer->remote_name);

    peer->on_midi([this, aseq_port](parse_buffer_t &pb){
      this->recv_rtpmidi_event(aseq_port, pb);
    });
    seq.on_midi_event(aseq_port, [this, aseq_port](snd_seq_event_t *ev){
      auto peer_it = known_servers_connections.find(aseq_port);
      if (peer_it == std::end(known_servers_connections)){
        WARNING("Got MIDI event in an non existing anymore peer.");
        return;
      }
      auto conn = &peer_it->second;

      uint8_t data[128];
      parse_buffer_t stream(data, sizeof(data));

      alsamidi_to_midiprotocol(ev, stream);

      // Reset
      stream.end = stream.position;
      stream.position = stream.start;

      // And send
      conn->peer->send_midi(stream);
    });
    peer->on_disconnect([this, aseq_port]{
      seq.remove_port(aseq_port);
      known_servers_connections.erase(aseq_port);
    });

    server_conn_info server_conn={
      peer->remote_name,
      peer,
      rtpserver,
    };

    known_servers_connections[aseq_port] = server_conn;
  });

  return rtpserver;
}

std::shared_ptr<rtpserver> rtpmidid_t::add_rtpmidid_export_server(
      const std::string &name, uint8_t alsaport, aseq::port_t &from){
    auto server = std::make_shared<rtpserver>(name, 0);

    announce_rtpmidid_server(name, server->control_port);

    seq.on_midi_event(alsaport, [this, server](snd_seq_event_t *ev){
      uint8_t tmp[128];
      parse_buffer_t buffer(tmp, sizeof(tmp));
      alsamidi_to_midiprotocol(ev, buffer);
      buffer.end = buffer.position;
      buffer.position = buffer.start;

      server->send_midi_to_all_peers(buffer);
    });

    seq.on_unsubscribe(alsaport, [this, name, server](aseq::port_t from){
      // This should destroy the server.
      unannounce_rtpmidid_server(name, server->control_port);
      // TODO: disconnect from on_midi_event.
      alsa_to_server.erase(from);
    });

    server->on_midi_event_on_any_peer([this, alsaport](parse_buffer_t &buffer){
      this->recv_rtpmidi_event(alsaport, buffer);
    });

    alsa_to_server[from] = server;

    return server;
}

void rtpmidid_t::setup_alsa_seq(){
  // Export only one, but all data that is conencted to it.
  // add_export_port();
  auto alsaport = seq.create_port("Network");
  seq.on_subscribe(alsaport, [this, alsaport](aseq::port_t from, const std::string &name){
    DEBUG("Connected to network port. Create server for this alsa data.");

    add_rtpmidid_export_server(fmt::format("{}/{}", this->name, name), alsaport, from);
  });
}

void rtpmidid_t::setup_mdns(){
  mdns_rtpmidi.on_discovery([this](const std::string &name, const std::string &address, int32_t port){
    this->add_rtpmidi_client(name, address, port);
  });
  
  mdns_rtpmidi.on_removed([this](const std::string &name){
    // TODO : remove client / alsa sessions
    WARNING("NOT IMPLEMENTED : network browser removed client {} ", name);
  });
}


std::optional<uint8_t> rtpmidid_t::add_rtpmidi_client(
    const std::string &name, const std::string &address, uint16_t net_port){
  for (auto &known: known_clients){
    if (known.second.address == address && known.second.port == net_port){
      DEBUG(
          "Trying to add again rtpmidi {}:{} server. Quite probably mDNS re announce. "
          "Maybe somebody ask, or just periodically.", address, net_port
      );
      return std::nullopt;
    }
  }

  auto aseq_port = seq.create_port(name);
  auto peer_info = ::rtpmidid::client_info{
    name, address, net_port, 0, nullptr,
  };

  INFO("New alsa port: {}, connects to {}:{} ({})", aseq_port, address, net_port, name);
  known_clients[aseq_port] = std::move(peer_info);

  seq.on_subscribe(aseq_port, [this, aseq_port](aseq::port_t port, const std::string &name){
    DEBUG("Callback on subscribe at rtpmidid: {}", name);
    auto peer_info = &known_clients[aseq_port];
    if (!peer_info->peer){
      peer_info->peer = std::make_shared<rtpclient>(name, peer_info->address, peer_info->port);
      peer_info->peer->on_midi([this, aseq_port](parse_buffer_t &pb){
        this->recv_rtpmidi_event(aseq_port, pb);
      });
      peer_info->use_count++;
    }
    else {
      DEBUG("Already connected.");
    }
  });
  seq.on_unsubscribe(aseq_port, [this, aseq_port](aseq::port_t port){
    DEBUG("Callback on unsubscribe at rtpmidid");
    auto peer_info = &known_clients[aseq_port];
    peer_info->use_count--;
    if (peer_info->use_count <= 0){
      peer_info->peer = nullptr;
    }
  });
  seq.on_midi_event(aseq_port, [this, aseq_port](snd_seq_event_t *ev){
    this->recv_alsamidi_event(aseq_port, ev);
  });

  return aseq_port;
}


void rtpmidid_t::recv_rtpmidi_event(int port, parse_buffer_t &midi_data){
  uint8_t current_command =  0;
  snd_seq_event_t ev;

  while (midi_data.position < midi_data.end){
    // MIDI may reuse the last command if appropiate. For example several consecutive Note On
    int maybe_next_command = midi_data.read_uint8();
    if (maybe_next_command & 0x80){
      current_command = maybe_next_command;
    } else {
      midi_data.position--;
    }
    auto type = current_command & 0xF0;

    switch(type){
      case 0xB0: // CC
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_controller(&ev, current_command & 0x0F, midi_data.read_uint8(), midi_data.read_uint8());
      break;
      case 0x90:
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_noteon(&ev, current_command & 0x0F, midi_data.read_uint8(), midi_data.read_uint8());
      break;
      case 0x80:
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_noteoff(&ev, current_command & 0x0F, midi_data.read_uint8(), midi_data.read_uint8());
      break;
      case 0xA0:
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_keypress(&ev, current_command & 0x0F, midi_data.read_uint8(), midi_data.read_uint8());
      break;
      case 0xC0:
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_pgmchange(&ev, current_command & 0x0F, midi_data.read_uint8());
        break;
      case 0xD0:
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_chanpress(&ev, current_command & 0x0F, midi_data.read_uint8());
      break;
      case 0xE0:
      {
        snd_seq_ev_clear(&ev);
        auto lsb = midi_data.read_uint8();
        auto msb = midi_data.read_uint8();
        auto pitch_bend = ((msb << 7) + lsb) - 8192;
        // DEBUG("Pitch bend received {}", pitch_bend);
        snd_seq_ev_set_pitchbend(&ev, current_command & 0x0F, pitch_bend);
      }
      break;
      case 0xFE: //active sense
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_fixed(&ev);
        ev.type = SND_SEQ_EVENT_SENSING;
      break;
      // XXXTODO: sysex
      default:
        WARNING("MIDI command type {:02X} not implemented yet", type);
        return;
        break;
    }
    snd_seq_ev_set_source(&ev, port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output_direct(seq.seq, &ev);
  }

}


void rtpmidid_t::recv_alsamidi_event(int aseq_port, snd_seq_event *ev){
  // DEBUG("Callback on midi event at rtpmidid, port {}", aseq_port);
  auto peer_info = &known_clients[aseq_port];
  if (!peer_info->peer){
    ERROR("There is no peer but I received an event! This situation should NEVER happen. File a bug. Port {}", aseq_port);
    return;
  }
  uint8_t data[128];
  parse_buffer_t stream(data, sizeof(data));

  alsamidi_to_midiprotocol(ev, stream);

  // Reset
  stream.end = stream.position;
  stream.position = stream.start;

  // And send
  peer_info->peer->send_midi(stream);
}

void rtpmidid_t::alsamidi_to_midiprotocol(snd_seq_event_t *ev, parse_buffer_t &stream){
  switch(ev->type){
    //case SND_SEQ_EVENT_NOTE:
    case SND_SEQ_EVENT_NOTEON:
      stream.write_uint8(0x90 | (ev->data.note.channel & 0x0F));
      stream.write_uint8(ev->data.note.note);
      stream.write_uint8(ev->data.note.velocity);
    break;
    case SND_SEQ_EVENT_NOTEOFF:
      stream.write_uint8(0x80 | (ev->data.note.channel & 0x0F));
      stream.write_uint8(ev->data.note.note);
      stream.write_uint8(ev->data.note.velocity);
    break;
    case SND_SEQ_EVENT_KEYPRESS:
      stream.write_uint8(0xA0 | (ev->data.note.channel & 0x0F));
      stream.write_uint8(ev->data.note.note);
      stream.write_uint8(ev->data.note.velocity);
    break;
    case SND_SEQ_EVENT_CONTROLLER:
      stream.write_uint8(0xB0 | (ev->data.control.channel & 0x0F));
      stream.write_uint8(ev->data.control.param);
      stream.write_uint8(ev->data.control.value);
    break;
    case SND_SEQ_EVENT_PGMCHANGE:
      stream.write_uint8(0xC0 | (ev->data.control.channel));
      stream.write_uint8(ev->data.control.value & 0x0FF);
    break;
    case SND_SEQ_EVENT_CHANPRESS:
      stream.write_uint8(0xD0 | (ev->data.control.channel));
      stream.write_uint8(ev->data.control.value & 0x0FF);
    break;
    case SND_SEQ_EVENT_PITCHBEND:
      // DEBUG("Send pitch bend {}", ev->data.control.value);
      stream.write_uint8(0xE0 | (ev->data.control.channel & 0x0F));
      stream.write_uint8((ev->data.control.value + 8192) & 0x07F);
      stream.write_uint8((ev->data.control.value + 8192) >> 7 & 0x07F);
    break;
    case SND_SEQ_EVENT_SENSING:
      stream.write_uint8(0xFE);
    break;
    case SND_SEQ_EVENT_SYSEX: {
      unsigned len = ev->data.ext.len, sz = stream.size();
      if (len <= sz) {
	unsigned char *data = (unsigned char*)ev->data.ext.ptr;
	for (unsigned i = 0; i < len; i++) {
	  stream.write_uint8(data[i]);
	}
      } else {
	WARNING("Sysex buffer overflow! Not sending. ({} bytes needed)", len);
      }
    }
    break;
    default:
      WARNING("Event type not yet implemented! Not sending. {}", ev->type);
      return;
      break;
  }
}

void rtpmidid_t::remove_client(uint8_t port){
  DEBUG("Removing peer from known peers list.");
  known_clients.erase(port);
}
