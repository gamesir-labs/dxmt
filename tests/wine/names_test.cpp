#include <dxmt_test.hpp>

#include "dxmt_names.hpp"

#include <sstream>

TEST(EnumNames, FormatsRepresentativeGraphicsEnums) {
  std::ostringstream output;
  output << D3D_FEATURE_LEVEL_12_1 << ' ' << D3D11_RESOURCE_DIMENSION_TEXTURE3D
         << ' ' << DXGI_FORMAT_NV12 << ' ' << WMTPixelFormatBC7_RGBAUnorm;
  EXPECT_EQ(output.str(),
            "D3D_FEATURE_LEVEL_12_1 D3D11_RESOURCE_DIMENSION_TEXTURE3D "
            "DXGI_FORMAT_NV12 WMTPixelFormatBC7_RGBAUnorm");
}

TEST(EnumNames, FormatsUnknownValueNumerically) {
  std::ostringstream output;
  output << static_cast<DXGI_FORMAT>(-7);
  EXPECT_EQ(output.str(), "<unknown enum: -7>");
}
