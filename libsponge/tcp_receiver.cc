#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (seg.header().syn) {
        _isn = seg.header().seqno;
    }
    if (_isn.has_value()) {
        size_t index = unwrap(seg.header().seqno + seg.header().syn, _isn.value(), _reassembler.idx_expected()) - 1;
        _reassembler.push_substring(seg.payload().copy(), index, seg.header().fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_isn.has_value()) {
        return nullopt;
    }
    return wrap(stream_out().bytes_written() + 1 + stream_out().input_ended(), _isn.value());
}

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
