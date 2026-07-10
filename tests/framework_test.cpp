#include <gtest/gtest.h>

#include <string_view>

TEST(GoogleTestIntegration, UsesDxmtCxxStandard) {
  static_assert(__cplusplus >= 202002L);

  constexpr std::string_view project_name = "dxmt";
  EXPECT_EQ(project_name, "dxmt");
}
