#pragma once

#include <cstdint>

namespace dxmt::apitrace {

#ifdef DXMT_APITRACE_ENABLED

bool enabled();
void ensure_session_open();
void seal_checkpoint();
void shutdown();
void set_current_d3d_sequence(uint64_t d3d_sequence);
void on_command_buffer_begin(uint64_t command_buffer_id, uint64_t frame_id);
void on_command_buffer_commit(uint64_t command_buffer_id);
void on_present_drawable(
    uint64_t command_buffer_id,
    uint64_t drawable_id,
    uint64_t frame_index,
    uint32_t sync_interval,
    uint32_t flags);
void maybe_seal_metal_checkpoint_after_frame(uint64_t frame_index);
void maybe_seal_d3d_checkpoint_after_frame(uint64_t frame_index);

#else

inline bool enabled() { return false; }
inline void ensure_session_open() {}
inline void seal_checkpoint() {}
inline void shutdown() {}
inline void set_current_d3d_sequence(uint64_t) {}
inline void on_command_buffer_begin(uint64_t, uint64_t) {}
inline void on_command_buffer_commit(uint64_t) {}
inline void on_present_drawable(uint64_t, uint64_t, uint64_t, uint32_t, uint32_t) {}
inline void maybe_seal_metal_checkpoint_after_frame(uint64_t) {}
inline void maybe_seal_d3d_checkpoint_after_frame(uint64_t) {}

#endif

} // namespace dxmt::apitrace
