#include "morok/common/utf8_text.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr std::uint64_t kMaxFileBytes = 16ULL << 20U;
constexpr std::uint64_t kMaxTotalBytes = 1ULL << 30U;
}
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: morok-convert <input-directory> <output-directory>\n"
                  << "Copies .txt corpus documents after strict UTF-8 validation.\n";
        return 2;
    }
    try {
        const fs::path input = fs::canonical(argv[1]);
        if (!fs::is_directory(input)) throw std::invalid_argument("input is not a directory");
        const fs::path output = fs::absolute(argv[2]).lexically_normal();
        if (fs::exists(output)) throw std::invalid_argument("output directory must not already exist");
        const auto relative = output.lexically_relative(input);
        if (!relative.empty() && *relative.begin() != "..")
            throw std::invalid_argument("output directory cannot be inside input directory");
        fs::create_directories(output);

        std::vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(
                 input, fs::directory_options::skip_permission_denied)) {
            if (fs::is_regular_file(entry.symlink_status()) &&
                entry.path().extension() == ".txt")
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) throw std::runtime_error("no .txt documents found");

        std::uint64_t total = 0;
        std::size_t converted = 0;
        for (const auto& source : files) {
            const auto size = fs::file_size(source);
            if (size > kMaxFileBytes || total + size > kMaxTotalBytes)
                throw std::runtime_error("corpus exceeds conversion byte limits");
            std::ifstream in(source, std::ios::binary);
            if (!in) throw std::runtime_error("cannot open " + source.string());
            std::string data((std::istreambuf_iterator<char>(in)), {});
            if (in.bad()) throw std::runtime_error("failed reading " + source.string());
            if (!morok::common::Utf8Text::from_utf8(data))
                throw std::runtime_error("invalid UTF-8 in " + source.string());

            const auto relative_path = fs::relative(source, input);
            const auto destination = output / relative_path;
            fs::create_directories(destination.parent_path());
            std::ofstream out(destination, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("cannot create " + destination.string());
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            out.flush();
            if (!out) throw std::runtime_error("failed writing " + destination.string());
            total += size;
            ++converted;
        }
        std::cout << "{\"converted_documents\":" << converted
                  << ",\"bytes\":" << total << ",\"encoding\":\"UTF-8\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "morok-convert: " << error.what() << '\n';
        return 1;
    }
}
