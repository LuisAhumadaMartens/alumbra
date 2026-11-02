#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <span>
#include <system_error>
#include <vector>

#include "fuzz_target.h"

namespace {

bool CollectInputs(const std::filesystem::path& path,
                   std::vector<std::filesystem::path>& inputs) {
  std::error_code error;
  if (std::filesystem::is_regular_file(path, error)) {
    inputs.push_back(path);
    return true;
  }
  if (!std::filesystem::is_directory(path, error)) {
    std::cerr << "Not a readable file or directory: " << path << '\n';
    return false;
  }
  for (std::filesystem::recursive_directory_iterator entry(path, error), end;
       !error && entry != end; entry.increment(error)) {
    if (entry->is_regular_file(error)) {
      inputs.push_back(entry->path());
    }
  }
  if (error) {
    std::cerr << "Could not list " << path << ": " << error.message() << '\n';
    return false;
  }
  return true;
}

bool Replay(const std::filesystem::path& path) {
  std::cerr << "Running: " << path.string() << '\n' << std::flush;
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  std::ifstream stream(path, std::ios::binary);
  if (error || !stream) {
    std::cerr << "Could not open " << path << '\n';
    return false;
  }
  std::vector<std::uint8_t> input(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(input.data()),
              static_cast<std::streamsize>(input.size()));
  if (static_cast<std::size_t>(stream.gcount()) != input.size()) {
    std::cerr << "Could not read " << path << '\n';
    return false;
  }
  static constexpr std::uint8_t kEmpty = 0;
  LLVMFuzzerTestOneInput(input.empty() ? &kEmpty : input.data(), input.size());
  return true;
}

}

int main(int argc, char** argv) {
  const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
  if (arguments.size() < 2) {
    std::cerr << "Usage: replay <file-or-directory>...\n";
    return 2;
  }
  std::vector<std::filesystem::path> inputs;
  bool ok = true;
  for (const char* argument : arguments.subspan(1)) {
    ok = CollectInputs(argument, inputs) && ok;
  }
  std::ranges::sort(inputs);
  if (inputs.empty()) {
    std::cerr << "No inputs to replay\n";
    return 1;
  }
  for (const std::filesystem::path& input : inputs) {
    ok = Replay(input) && ok;
  }
  std::cout << "Replayed " << inputs.size() << " inputs\n";
  return ok ? 0 : 1;
}
