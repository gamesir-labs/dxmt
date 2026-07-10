#include <dxmt_test.hpp>

int main(int argc, char** argv) {
  GTEST_FLAG_SET(brief, true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
