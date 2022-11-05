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
    for (size_t i = 0; i < data.size(); ++i) {
        size_t cur_idx = index + i;
        if (cur_idx < _idx_expected) {
            continue;
        } else if (cur_idx == _idx_expected && remaining_capacity() != 0) {
            _output.write_char(data[i]);
            ++_idx_expected;
        } else if (remaining_capacity() != 0) {  // not full
            _buf.emplace(cur_idx, data[i]);
            _buf_bottom = _buf_bottom > cur_idx ? _buf_bottom : cur_idx;
        } else if (!_buf.empty() && cur_idx < _buf_bottom) {  // full, discard byte from bottom if possible
            if (_buf_bottom - _input_end_at == 0) {
                _input_end_at = -1;  // if eof byte discarded reset eof
            }
            _buf.erase(_buf_bottom);
            _buf.emplace(cur_idx, data[i]);
            // find new _buf_bottom
            _buf_bottom = 0;
            for (const auto &itm : _buf) {
                _buf_bottom = _buf_bottom > itm.first ? _buf_bottom : itm.first;
            }
        }

        auto it = _buf.end();
        while ((it = _buf.find(_idx_expected)) != _buf.end()) {
            _output.write_char(it->second);
            _buf.erase(it);
            if (_idx_expected == _buf_bottom) {
                _buf_bottom = 0;
            }
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
