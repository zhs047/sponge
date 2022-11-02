#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "network_interface.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetFrame frame;
    frame.header().src = _ethernet_address;

    if (_ip2ether.count(next_hop_ip) != 0 && _ip2ether.at(next_hop_ip).second + NetworkInterface::EXPIRATION > _now) {
        frame.payload() = dgram.serialize();
        frame.header().type = EthernetHeader::TYPE_IPv4;
        frame.header().dst = _ip2ether.at(next_hop_ip).first;
        _frames_out.push(frame);
    } else {
        if (_pending_dgram.count(next_hop_ip) == 0 ||
            get<2>(_pending_dgram.at(next_hop_ip)) + NetworkInterface::ARP_WAIT <= _now) {
            ARPMessage amsg;
            amsg.opcode = ARPMessage::OPCODE_REQUEST;
            amsg.sender_ip_address = _ip_address.ipv4_numeric();
            amsg.sender_ethernet_address = _ethernet_address;
            amsg.target_ip_address = next_hop_ip;
            frame.header().type = EthernetHeader::TYPE_ARP;
            frame.header().dst = ETHERNET_BROADCAST;
            frame.payload() = BufferList{amsg.serialize()};
            _frames_out.push(frame);
        }
        _pending_dgram.emplace(next_hop_ip, make_tuple(dgram, next_hop, _now));
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return nullopt;
    }

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        return dgram;
    }

    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage amsg;
        if (amsg.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;
        }
        _time2ip.emplace(_now, amsg.sender_ip_address);
        _ip2ether.emplace(amsg.sender_ip_address, make_pair(amsg.sender_ethernet_address, _now));
        if (_pending_dgram.count(amsg.sender_ip_address) != 0) {
            const auto &[dgram, addr, time] = _pending_dgram.at(amsg.sender_ip_address);
            send_datagram(dgram, addr);
            _pending_dgram.erase(amsg.sender_ip_address);
        }
        if (amsg.opcode == ARPMessage::OPCODE_REPLY) {
            _time2ip.emplace(_now, amsg.target_ip_address);
            _ip2ether.emplace(amsg.target_ip_address, make_pair(amsg.target_ethernet_address, _now));
            if (_pending_dgram.count(amsg.target_ip_address) != 0) {
                const auto &[dgram, addr, time] = _pending_dgram.at(amsg.target_ip_address);
                send_datagram(dgram, addr);
                _pending_dgram.erase(amsg.target_ip_address);
            }
        } else {
            if (amsg.target_ip_address == _ip_address.ipv4_numeric()) {
                ARPMessage reply;
                reply.opcode = ARPMessage::OPCODE_REPLY;
                reply.sender_ip_address = _ip_address.ipv4_numeric();
                reply.sender_ethernet_address = _ethernet_address;
                reply.target_ip_address = amsg.sender_ip_address;
                reply.target_ethernet_address = amsg.sender_ethernet_address;
                EthernetFrame reply_frame;
                reply_frame.header().type = EthernetHeader::TYPE_ARP;
                reply_frame.header().src = _ethernet_address;
                reply_frame.header().dst = amsg.sender_ethernet_address;
                reply_frame.payload() = BufferList{reply.serialize()};
                _frames_out.push(reply_frame);
            }
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _now += ms_since_last_tick;
    while (!_time2ip.empty()) {
        const auto [t, ip] = _time2ip.front();
        if (t + NetworkInterface::EXPIRATION <= _now) {
            if (_ip2ether.count(ip) != 0 && _ip2ether.at(ip).second == t) {
                _ip2ether.erase(ip);
            }
            _time2ip.pop();
        } else {
            break;
        }
    }
}
