#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (index + i < _idx_expected) {
            continue;
        } else if (index + i == _idx_expected && (buf_writeout() != 0 || remaining_capacity() != 0)) {
            _output.write_char(data[i]);
            ++_idx_expected;
        } else {
            buf_push(data[i], index + i);
        }
        buf_writeout();
    }

    if (eof) {
        _input_end_at = index + (data.size() == 0 ? 0 : data.size() - 1);
    }
    if (_input_end_at != -1 && _buf.empty()) {
        _output.end_input();
    }
}

void StreamReassembler::buf_push(const char c, const size_t index) {
    if (_buf.count(index) != 0) {
        return;
    }
    if (buf_writeout() != 0 || remaining_capacity() != 0) {  // not full
        _buf[index] = c;
    } else if (!_buf.empty()) {  // full, discard byte from bottom if possible
        size_t idx_end = _buf.rbegin()->first;
        if (index < idx_end) {
            if (static_cast<int>(idx_end) == _input_end_at) {
                _input_end_at = -1;  // if eof byte discarded reset eof
            }
            _buf.erase(idx_end);
            _buf[index] = c;
        }
    }
}

size_t StreamReassembler::buf_writeout() {
    size_t written_size = 0;
    while (!_buf.empty() && _output.remaining_capacity() != 0) {
        size_t idx_begin = _buf.begin()->first;
        if (idx_begin > _idx_expected) {
            break;
        }
        char c = _buf.at(idx_begin);
        _buf.erase(idx_begin);
        _output.write_char(c);
        ++_idx_expected;
        ++written_size;
    }
    return written_size;
}

size_t StreamReassembler::remaining_capacity() { return _output.remaining_capacity() - _buf.size(); }

size_t StreamReassembler::unassembled_bytes() const { return _buf.size(); }

bool StreamReassembler::empty() const { return _buf.empty(); }
