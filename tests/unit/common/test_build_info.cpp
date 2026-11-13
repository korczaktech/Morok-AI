#include "morok/common/build_info.hpp"
#include <cstdlib>
#include <iostream>
int main() {
 const auto value = morok::common::version();
 if (value.empty()) {
  std::cerr << "Version string must not be empty.\n";
  return EXIT_FAILURE;
 }
 std::cout << "Morok version: " << value << '\n';
 return EXIT_SUCCESS;
}
