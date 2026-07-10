#include <dxmt_benchmark.hpp>

int main(int argc, char **argv) {
  ::benchmark::MaybeReenterWithoutASLR(argc, argv);
  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}
