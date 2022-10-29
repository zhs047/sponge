#include "tcp_config.hh"
#include "tcp_sender.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

RetransmissionTimer::RetransmissionTimer(const uint64_t time_limit) : _time_limit(time_limit) {}

void RetransmissionTimer::reset() { _elapsed_time = 0; }

uint64_t RetransmissionTimer::get_time_limit() { return _time_limit; }

void RetransmissionTimer::set_time_limit(const uint64_t time_limit) { _time_limit = time_limit; }

bool RetransmissionTimer::timeout(const uint64_t ms_since_last_tick) {
    if (_elapsed_time + ms_since_last_tick >= _time_limit) {
        _elapsed_time = 0;
        return true;
    }
    _elapsed_time += ms_since_last_tick;
    return false;
}
//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _timer(retx_timeout)
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    uint16_t window_size = _window_size == 0 ? 1 : _window_size;
    while (window_size > _bytes_in_flight) {
        TCPSegment segment;
        // send syn
        if (!_syn_sent) {
            segment.header().syn = true;
            _syn_sent = true;
        }
        // send seqno
        segment.header().seqno = wrap(_next_seqno, _isn);
        // send payload
        uint16_t window_remained = window_size - _bytes_in_flight - segment.header().syn;
        string payload_str =
            _stream.read(window_remained > TCPConfig::MAX_PAYLOAD_SIZE ? TCPConfig::MAX_PAYLOAD_SIZE : window_remained);
        uint16_t payload_size = payload_str.size();
        segment.payload() = Buffer(move(payload_str));
        // send fin
        if (!_fin_sent && _stream.eof() && (window_remained - payload_size) > 0) {
            segment.header().fin = true;
            _fin_sent = true;
        }

        if (_segments_pending.empty()) {
            _nof_consec_retransmisson = 0;
            _timer.set_time_limit(_initial_retransmission_timeout);
            _timer.reset();
        }
        if (segment.length_in_sequence_space() == 0) {
            break;  // nothing to send
        }

        _segments_out.push(segment);
        _segments_pending.push(segment);
        _next_seqno += segment.length_in_sequence_space();
        _bytes_in_flight += segment.length_in_sequence_space();

        if (_fin_sent) {
            break;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    if (abs_ackno > _next_seqno) {
        return;  // invalid ackno
    }
    if (abs_ackno >= _acked_seqno) {
        _window_size = window_size;
    }
    if (abs_ackno > _acked_seqno) {
        _acked_seqno = abs_ackno;
        _nof_consec_retransmisson = 0;
        _timer.set_time_limit(_initial_retransmission_timeout);
        _timer.reset();
    }

    while (!_segments_pending.empty()) {
        if (abs_ackno < unwrap(_segments_pending.front().header().seqno, _isn, _next_seqno) +
                            _segments_pending.front().length_in_sequence_space()) {
            break;
        }
        _bytes_in_flight -= _segments_pending.front().length_in_sequence_space();
        _segments_pending.pop();
    }

    if (_segments_pending.empty()) {
        _nof_consec_retransmisson = 0;
        _timer.set_time_limit(_initial_retransmission_timeout);
        _timer.reset();
    }
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer.timeout(ms_since_last_tick)) {
        return;
    }
    // timeout happens
    if (_segments_pending.empty()) {
        _nof_consec_retransmisson = 0;
        _timer.set_time_limit(_initial_retransmission_timeout);
        _timer.reset();
        return;
    }
    if (_window_size != 0) {
        ++_nof_consec_retransmisson;
        _timer.set_time_limit(_timer.get_time_limit() * 2);
    }
    _segments_out.push(_segments_pending.front());
    _timer.reset();
}

unsigned int TCPSender::consecutive_retransmissions() const { return _nof_consec_retransmisson; }

void TCPSender::send_empty_segment() {
    TCPSegment empty_segment;
    empty_segment.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(empty_segment);
}
