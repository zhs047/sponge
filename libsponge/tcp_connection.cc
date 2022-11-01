#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_receive; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active) {
        return;
    }
    _time_since_last_receive = 0;
    if (seg.header().rst) {
        disconnect();
        return;
    }

    if (!_sender.syn_sent()) {  // closed listen
        if (seg.header().syn) {
            _receiver.segment_received(seg);
            connect();  // goto syn sent
        }
    } else if (_sender.acked_seqno() == 0 && !_receiver.ackno().has_value()) {  // syn sent
        if (seg.header().syn && seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);  // goto established
            _receiver.segment_received(seg);
            _sender.send_empty_segment();
            send_segments();
        } else if (seg.header().syn && !seg.header().ack) {  // goto syn received
            _receiver.segment_received(seg);
            _sender.send_empty_segment();
            send_segments();
        }
    } else if (_sender.acked_seqno() == 0 && _receiver.ackno().has_value() &&
               !_receiver.stream_out().input_ended()) {              // syn received
        _sender.ack_received(seg.header().ackno, seg.header().win);  // goto established
        _receiver.segment_received(seg);
    } else if (_sender.acked_seqno() > 0 && !_sender.stream_in().eof()) {  // established
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _receiver.segment_received(seg);
        if (seg.length_in_sequence_space() != 0) {
            _sender.send_empty_segment();
        }
        _sender.fill_window();
        send_segments();
    } else if (_sender.fin_sent() && bytes_in_flight() > 0 && !_receiver.stream_out().input_ended()) {  // fin wait 1
        if (seg.header().fin) {
            _sender.ack_received(seg.header().ackno, seg.header().win);  // goto time wait
            _receiver.segment_received(seg);
            _sender.send_empty_segment();
            send_segments();
        } else if (seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);  // goto fin wait 2
            _receiver.segment_received(seg);
            send_segments();
        }
    } else if (_sender.fin_sent() && bytes_in_flight() == 0 && !_receiver.stream_out().input_ended()) {  // fin wait 2
        _sender.ack_received(seg.header().ackno, seg.header().win);  // goto time wait
        _receiver.segment_received(seg);
        _sender.send_empty_segment();
        send_segments();
    } else if (_sender.fin_sent() && bytes_in_flight() == 0 && _receiver.stream_out().input_ended()) {  // time wait
        if (seg.header().fin) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
            _receiver.segment_received(seg);
            _sender.send_empty_segment();
            send_segments();
        }
    } else {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _receiver.segment_received(seg);
        _sender.fill_window();
        send_segments();
    }
}

void TCPConnection::send_segments() {
    while (!_sender.segments_out().empty()) {
        if (_receiver.ackno().has_value()) {
            _sender.segments_out().front().header().ack = true;
            _sender.segments_out().front().header().ackno = _receiver.ackno().value();
            _sender.segments_out().front().header().win = static_cast<uint16_t>(_receiver.window_size());
        }
        _segments_out.push(_sender.segments_out().front());
        _sender.segments_out().pop();
    }

    if (_receiver.stream_out().input_ended()) {
        if (!_sender.fin_sent()) {
            _linger_after_streams_finish = false;
        } else if (bytes_in_flight() == 0) {
            if (!_linger_after_streams_finish || _time_since_last_receive >= 10 * _cfg.rt_timeout) {
                _active = false;
            }
        }
    }
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t written_size = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segments();
    return written_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active) {
        return;
    }
    _time_since_last_receive += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        reset();
        return;
    }
    send_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_segments();
}

void TCPConnection::disconnect() {
    _active = false;
    _linger_after_streams_finish = false;
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
}

void TCPConnection::reset() {
    if (_sender.segments_out().empty()) {
        _sender.send_empty_segment();
    }
    _sender.segments_out().front().header().rst = true;
    send_segments();
    disconnect();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            reset();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
