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
    if (index >= _idx_expected + _capacity - _output.buffer_size()) {
        return;
    }
    auto hint = _buf.end();
    for (size_t i = 0; i < data.size(); ++i) {
        if (index + i < _idx_expected) {
            continue;
        } else if (index + i == _idx_expected && remaining_capacity() != 0) {
            _output.write_char(data[i]);
            ++_idx_expected;
        } else if (remaining_capacity() != 0) {  // not full
            hint = _buf.emplace_hint(hint, index + i, data[i]);
        } else if (!_buf.empty() && index + i < _buf.rbegin()->first) {  // full, discard byte from bottom if possible
            if (_buf.rbegin()->first - _input_end_at == 0) {
                _input_end_at = -1;  // if eof byte discarded reset eof
            }
            _buf.erase(--_buf.end());
            _buf.emplace_hint(_buf.end(), index + i, data[i]);
        }

        while (!_buf.empty() && _buf.begin()->first == _idx_expected) {
            _output.write_char(_buf.begin()->second);
            _buf.erase(_buf.begin());
            ++_idx_expected;
        }
    }

    if (eof) {
        _input_end_at = index + (data.size() == 0 ? 0 : data.size() - 1);
    }
    if (_input_end_at != -1 && _buf.empty()) {
        _output.end_input();
    }
}

size_t StreamReassembler::remaining_capacity() { return _output.remaining_capacity() - _buf.size(); }

size_t StreamReassembler::unassembled_bytes() const { return _buf.size(); }

bool StreamReassembler::empty() const { return _buf.empty(); }

size_t StreamReassembler::idx_expected() const { return _idx_expected; }
