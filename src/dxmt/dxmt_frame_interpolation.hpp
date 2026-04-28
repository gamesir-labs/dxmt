#pragma once

#include "dxmt_texture.hpp"
#include "rc/util_rc_ptr.hpp"
#include <cstdint>

namespace dxmt {

struct FrameInterpolationInputs {
  Rc<Texture> previous_color;
  Rc<Texture> current_color;
  Rc<Texture> depth;
  Rc<Texture> motion_vector;
  Rc<Texture> hudless_color;
  Rc<Texture> ui_color_alpha;
  Rc<Texture> exposure;
};

struct FrameInterpolationConstants {
  uint64_t frame_index = 0;
  float motion_vector_scale_x = 1.0f;
  float motion_vector_scale_y = 1.0f;
  float jitter_offset_x = 0.0f;
  float jitter_offset_y = 0.0f;
  bool depth_reversed = false;
  bool reset = false;
};

struct FrameInterpolationRequest {
  FrameInterpolationInputs inputs;
  Rc<Texture> output_interpolated;
  FrameInterpolationConstants constants;
};

class FrameInterpolationBridge;

} // namespace dxmt
