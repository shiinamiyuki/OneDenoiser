// MIT License

// Copyright (c) 2021 椎名深雪

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <string>
#include <memory>
#include <cxxopts.hpp>
#include <execution>
#include <algorithm>
#include <filesystem>
#include <OpenImageIO/imageio.h>
#ifdef ENABLE_OIDN
#    include <OpenImageDenoise/oidn.hpp>
#endif
#pragma warning(disable : 4244)
namespace fs = std::filesystem;
template <class Float>
inline Float linear_to_srgb(Float L) {
    return (L < 0.0031308) ? (L * 12.92) : (1.055 * std::pow(L, 1.0 / 2.4) - 0.055);
}
template <class Float>
inline Float srgb_to_linear(Float S) {
    return (S < 0.04045) ? (S / 12.92) : (std::pow(S + 0.055, 2.4) / 1.055);
}
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<float> pixels;
};
Image read_img(const std::string &filename) {
    using namespace OIIO;
    auto in = ImageInput::open(filename);
    if (!in)
        throw std::runtime_error("image not found");
    const ImageSpec &spec = in->spec();
    int xres = spec.width;
    int yres = spec.height;
    int channels = spec.nchannels;
    std::vector<float> pixels(xres * yres * channels);
    in->read_image(TypeDesc::FLOAT, &pixels[0]);
    if (spec.format != TypeDesc::FLOAT || spec.format != TypeDesc::DOUBLE || spec.format != TypeDesc::HALF) {
        std::transform(std::execution::par_unseq, pixels.begin(), pixels.end(), pixels.begin(),
                       [](float x) { return srgb_to_linear(x); });
    }
    in->close();
    Image img;
    img.width = xres;
    img.height = yres;
    img.channels = channels;
    img.pixels = std::move(pixels);
    return img;
}
void write_img(const Image &img, const std::string &filename) {
    const int xres = img.width, yres = img.height;
    const int channels = img.channels; // RGB
    std::vector<float> pixels = img.pixels;
    using namespace OIIO;
    std::unique_ptr<ImageOutput> out = ImageOutput::create(filename);
    if (!out)
        return;
    ImageSpec spec(xres, yres, channels, TypeDesc::UINT8);
    auto path = fs::path(filename);
    auto ext = path.extension();
    if (ext != ".exr") {
        std::transform(std::execution::par_unseq, pixels.begin(), pixels.end(), pixels.begin(),
                       [](float x) { return linear_to_srgb(x); });
    }
    out->open(filename, spec);
    out->write_image(TypeDesc::FLOAT, pixels.data());
    out->close();
}
#ifdef ENABLE_OIDN
Image run_oidn(const std::string &input_path, const std::string &albedo_path, const std::string &normal_path) {
    oidn::DeviceRef device = oidn::newDevice();
    oidn::FilterRef filter = device.newFilter("rt");
    auto input = read_img(input_path);
    filter.setImage("color", input.pixels.data(), oidn::Format::Float3, input.width, input.height);
    if (!albedo_path.empty()) {
        auto albedo = read_img(albedo_path);
        filter.setImage("albedo", albedo.pixels.data(), oidn::Format::Float3, input.width, input.height);
    }
    if (!normal_path.empty()) {
        auto normal = read_img(normal_path);
        filter.setImage("normal", normal.pixels.data(), oidn::Format::Float3, input.width, input.height);
    }
    Image output = input;
    filter.setImage("output", output.pixels.data(), oidn::Format::Float3, input.width, input.height);
    filter.set("hdr", true);
    filter.commit();
    filter.execute();
    return output;
}

#endif
int main(int argc, char **argv) {
    cxxopts::Options options(argv[0], " - OneDenoiser: easy-to-use wrapper for open source denoisers");
    // clang-format off
    options
      .allow_unrecognised_options()
      .add_options()
      ("use", "Which denoiser to use?", cxxopts::value<std::string>())
      ("i,input", "Noisy image", cxxopts::value<std::string>())
      ("a,albedo", "Albedo",cxxopts::value<std::string>())
      ("n,normal", "Normal",cxxopts::value<std::string>())
      ("o,output", "Denoised image", cxxopts::value<std::string>());
    // clang-format on
    auto result = options.parse(argc, argv);
    std::string algorithm;
    if (result.count("use")) {
        algorithm = result["use"].as<std::string>();

    } else {
        std::cout << options.help() << std::endl;
        exit(1);
    }
    std::string input_path;
    std::string albedo_path;
    std::string normal_path;
    std::string output_path;
    if (!result.count("input")) {
        std::cerr << "input not specified" << std::endl;
        exit(1);
    } else {
        input_path = result["input"].as<std::string>();
    }
    if (!result.count("output")) {
        std::cerr << "output not specified" << std::endl;
        exit(1);
    } else {
        output_path = result["output"].as<std::string>();
    }
    if (result.count("albedo")) {
        albedo_path = result["albedo"].as<std::string>();
    }
    if (result.count("normal")) {
        normal_path = result["normal"].as<std::string>();
    }
    Image output;
    if (algorithm == "oidn") {
#ifndef ENABLE_OIDN
        std::cerr << "OpenImageDenoise is not enabled" << std::endl;
        exit(1);
#else
        output = run_oidn(input_path, albedo_path, normal_path);
#endif
    } else {
        std::cerr << "unknown denosier " << algorithm << std::endl;
        exit(1);
    }
    write_img(output, output_path);
}