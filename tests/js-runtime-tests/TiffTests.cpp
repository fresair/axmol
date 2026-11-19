// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Native Image regression for the original v3 particle TIFFs and general TIFFs.
// SPDX-License-Identifier: MIT
#include "axmol/platform/Image.h"

#include <tiffio.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

namespace
{
using Bytes = std::vector<uint8_t>;
using Pixel = std::array<uint8_t, 4>;
constexpr std::array<Pixel, 6> colors = {{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
                                         {99, 121, 221, 128}, {200, 88, 77, 0}, {31, 47, 63, 255}}};
constexpr std::array<std::array<int, 6>, 8> oriented = {{{0, 1, 2, 3, 4, 5}, {2, 1, 0, 5, 4, 3},
    {5, 4, 3, 2, 1, 0}, {3, 4, 5, 0, 1, 2}, {0, 3, 1, 4, 2, 5}, {3, 0, 4, 1, 5, 2},
    {5, 2, 4, 1, 3, 0}, {2, 5, 1, 4, 0, 3}}};

void require(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error(message);
}

Bytes readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "Cannot read " + path.string());
    return Bytes(std::istreambuf_iterator<char>(input), {});
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        auto base = std::filesystem::temp_directory_path() / "axmol-tiff-XXXXXX";
        std::string value = base.string();
        require(mkdtemp(value.data()) != nullptr, "mkdtemp failed");
        path = value;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    std::filesystem::path path;
};

Pixel associated(Pixel value)
{
    for (int c = 0; c < 3; ++c)
        value[c] = static_cast<uint8_t>((static_cast<unsigned>(value[c]) * value[3] + 127) / 255);
    return value;
}

Bytes expectedColors(int orientation = 1)
{
    Bytes result;
    for (int index : oriented[orientation - 1])
    {
        auto value = associated(colors[index]);
        result.insert(result.end(), value.begin(), value.end());
    }
    return result;
}

void decode(const Bytes& input, const Bytes& expected, int width, int height, const std::string& label)
{
    auto image = std::make_unique<ax::Image>();
    require(image->initWithImageData(input.data(), static_cast<ssize_t>(input.size())), label + ": native decode failed");
    require(image->getFileType() == ax::Image::Format::TIFF, label + ": wrong Image format");
    require(image->getPixelFormat() == ax::rhi::PixelFormat::RGBA8, label + ": wrong pixel format");
    require(image->getWidth() == width && image->getHeight() == height, label + ": wrong dimensions/orientation");
    require(image->hasPremultipliedAlpha(), label + ": associated alpha not reported");
    require(image->getDataSize() == static_cast<ssize_t>(expected.size()), label + ": wrong byte count");
    require(std::equal(expected.begin(), expected.end(), image->getData()), label + ": RGBA pixel mismatch");
}

Bytes makeRGBA(const std::filesystem::path& path, uint16_t compression, int orientation = 1,
               const char* mode = "wl", bool tiled = false, uint16_t alphaType = EXTRASAMPLE_UNASSALPHA)
{
    auto tiff = std::unique_ptr<TIFF, decltype(&TIFFClose)>(TIFFOpen(path.c_str(), mode), TIFFClose);
    require(tiff != nullptr, "TIFF fixture open failed");
    require(TIFFSetField(tiff.get(), TIFFTAG_IMAGEWIDTH, 3) && TIFFSetField(tiff.get(), TIFFTAG_IMAGELENGTH, 2) &&
            TIFFSetField(tiff.get(), TIFFTAG_BITSPERSAMPLE, 8) && TIFFSetField(tiff.get(), TIFFTAG_SAMPLESPERPIXEL, 4) &&
            TIFFSetField(tiff.get(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB) &&
            TIFFSetField(tiff.get(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) &&
            TIFFSetField(tiff.get(), TIFFTAG_EXTRASAMPLES, 1, &alphaType) &&
            TIFFSetField(tiff.get(), TIFFTAG_COMPRESSION, compression) &&
            TIFFSetField(tiff.get(), TIFFTAG_ORIENTATION, orientation), "TIFF fixture tags failed");
    auto samples = colors;
    if (alphaType == EXTRASAMPLE_ASSOCALPHA)
        for (auto& value : samples)
            value = associated(value);
    if (compression == COMPRESSION_LZW || compression == COMPRESSION_ADOBE_DEFLATE)
        require(TIFFSetField(tiff.get(), TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL), "TIFF predictor tag failed");
    if (tiled)
    {
        require(TIFFSetField(tiff.get(), TIFFTAG_TILEWIDTH, 16) && TIFFSetField(tiff.get(), TIFFTAG_TILELENGTH, 16),
                "TIFF tile tags failed");
        std::array<Pixel, 256> tile{};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
                tile[y * 16 + x] = samples[y * 3 + x];
        require(TIFFWriteEncodedTile(tiff.get(), 0, tile.data(), sizeof(tile)) == sizeof(tile), "TIFF tile write failed");
    }
    else
    {
        require(TIFFSetField(tiff.get(), TIFFTAG_ROWSPERSTRIP, 1), "TIFF row tags failed");
        for (uint32_t row = 0; row < 2; ++row)
            require(TIFFWriteScanline(tiff.get(), samples.data() + row * 3, row) == 1, "TIFF strip write failed");
    }
    tiff.reset();
    return readFile(path);
}

void reject(const Bytes& input, const std::string& label)
{
    ax::Image image;
    require(!image.initWithImageData(input.data(), static_cast<ssize_t>(input.size())), label + ": damaged TIFF accepted");
    require(image.getDataSize() == 0 && image.getWidth() == 0 && image.getHeight() == 0,
            label + ": failed decode committed partial pixels");
}

void patchClassicTag(Bytes& input, uint16_t tag, uint16_t kind, uint32_t value)
{
    require(input[0] == 'I' && input[1] == 'I', "Expected little-endian classic fixture");
    auto get32 = [&](size_t p) { return uint32_t(input[p]) | (uint32_t(input[p + 1]) << 8) |
                                      (uint32_t(input[p + 2]) << 16) | (uint32_t(input[p + 3]) << 24); };
    const size_t directory = get32(4);
    const int count = input[directory] | (input[directory + 1] << 8);
    for (int i = 0; i < count; ++i)
    {
        const size_t entry = directory + 2 + static_cast<size_t>(i) * 12;
        if ((input[entry] | (input[entry + 1] << 8)) != tag)
            continue;
        input[entry + 2] = static_cast<uint8_t>(kind);
        input[entry + 3] = static_cast<uint8_t>(kind >> 8);
        for (int byte = 0; byte < 4; ++byte)
        {
            input[entry + 4 + byte] = byte == 0 ? 1 : 0;
            input[entry + 8 + byte] = static_cast<uint8_t>(value >> (byte * 8));
        }
        return;
    }
    throw std::runtime_error("Fixture tag not found");
}

void grayscaleAndPalette(const std::filesystem::path& directory)
{
    const std::array<uint8_t, 6> values{0, 51, 255, 123, 44, 217};
    for (auto photo : {PHOTOMETRIC_MINISBLACK, PHOTOMETRIC_MINISWHITE, PHOTOMETRIC_PALETTE})
    {
        auto path = directory / "gray.tiff";
        auto tiff = std::unique_ptr<TIFF, decltype(&TIFFClose)>(TIFFOpen(path.c_str(), "wb"), TIFFClose);
        require(tiff != nullptr, "Gray TIFF open failed");
        TIFFSetField(tiff.get(), TIFFTAG_IMAGEWIDTH, 3);
        TIFFSetField(tiff.get(), TIFFTAG_IMAGELENGTH, 2);
        TIFFSetField(tiff.get(), TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(tiff.get(), TIFFTAG_SAMPLESPERPIXEL, 1);
        TIFFSetField(tiff.get(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tiff.get(), TIFFTAG_PHOTOMETRIC, photo);
        TIFFSetField(tiff.get(), TIFFTAG_COMPRESSION, COMPRESSION_PACKBITS);
        TIFFSetField(tiff.get(), TIFFTAG_ROWSPERSTRIP, 1);
        std::array<uint16_t, 256> red{}, green{}, blue{};
        if (photo == PHOTOMETRIC_PALETTE)
        {
            for (int i = 0; i < 256; ++i)
            {
                red[i] = static_cast<uint16_t>(i * 257);
                green[i] = static_cast<uint16_t>((255 - i) * 257);
                blue[i] = static_cast<uint16_t>((i / 2) * 257);
            }
            TIFFSetField(tiff.get(), TIFFTAG_COLORMAP, red.data(), green.data(), blue.data());
        }
        auto samples = values;
        for (uint32_t row = 0; row < 2; ++row)
            require(TIFFWriteScanline(tiff.get(), samples.data() + row * 3, row) == 1, "Gray TIFF write failed");
        tiff.reset();
        Bytes expected;
        for (auto value : values)
        {
            uint8_t level = photo == PHOTOMETRIC_MINISWHITE ? 255 - value : value;
            Pixel pixel = photo == PHOTOMETRIC_PALETTE ? Pixel{value, static_cast<uint8_t>(255 - value),
                static_cast<uint8_t>(value / 2), 255} : Pixel{level, level, level, 255};
            expected.insert(expected.end(), pixel.begin(), pixel.end());
        }
        decode(readFile(path), expected, 3, 2, "grayscale/palette");
    }
}

void jpeg(const std::filesystem::path& directory)
{
    require(TIFFIsCODECConfigured(COMPRESSION_JPEG), "Axmol's configured JPEG TIFF codec is missing");
    auto path = directory / "jpeg.tiff";
    auto tiff = std::unique_ptr<TIFF, decltype(&TIFFClose)>(TIFFOpen(path.c_str(), "wl"), TIFFClose);
    require(tiff != nullptr, "JPEG TIFF open failed");
    TIFFSetField(tiff.get(), TIFFTAG_IMAGEWIDTH, 8);
    TIFFSetField(tiff.get(), TIFFTAG_IMAGELENGTH, 8);
    TIFFSetField(tiff.get(), TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tiff.get(), TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tiff.get(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff.get(), TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tiff.get(), TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
    TIFFSetField(tiff.get(), TIFFTAG_JPEGQUALITY, 100);
    TIFFSetField(tiff.get(), TIFFTAG_ROWSPERSTRIP, 8);
    std::array<uint8_t, 24> row;
    row.fill(37);
    for (uint32_t y = 0; y < 8; ++y)
        require(TIFFWriteScanline(tiff.get(), row.data(), y) == 1, "JPEG TIFF write failed");
    tiff.reset();
    Bytes expected;
    for (int i = 0; i < 64; ++i)
        expected.insert(expected.end(), {37, 37, 37, 255});
    decode(readFile(path), expected, 8, 8, "JPEG compression");
}
}  // namespace

int main(int argc, char** argv)
{
    static_assert(static_cast<int>(ax::Image::Format::JPG) == 0);
    static_assert(static_cast<int>(ax::Image::Format::RAW_DATA) == 11);
    static_assert(static_cast<int>(ax::Image::Format::UNKNOWN) == 12);
    static_assert(static_cast<int>(ax::Image::Format::TIFF) == 13);
    try
    {
        require(argc == 2, "Usage: js-tiff-tests extracted-original-fixtures-directory");
        std::filesystem::path fixtures = argv[1];
        std::ifstream list(fixtures / "originals.tsv");
        require(list.good(), "Original particle TIFF fixture list is missing");
        int originalCount = 0;
        for (std::string line; std::getline(list, line);)
        {
            std::istringstream row(line);
            std::string resource, width, height, stem;
            std::getline(row, resource, '\t'); std::getline(row, width, '\t');
            std::getline(row, height, '\t'); std::getline(row, stem, '\t');
            auto expected = readFile(fixtures / (stem + ".rgba"));
            decode(readFile(fixtures / (stem + ".input")), expected, std::stoi(width), std::stoi(height), resource + " gzip");
            decode(readFile(fixtures / (stem + ".tiff")), expected, std::stoi(width), std::stoi(height), resource + " raw");
            ++originalCount;
            std::cout << "PASS original " << resource << " " << width << "x" << height << " gzip+raw+RGBA\n";
        }
        require(originalCount == 13, "Not all 13 original embedded particle TIFFs were exercised");
        TemporaryDirectory temporary;
        auto path = temporary.path / "general.tiff";
        for (auto compression : {COMPRESSION_NONE, COMPRESSION_PACKBITS, COMPRESSION_LZW, COMPRESSION_ADOBE_DEFLATE})
        {
            require(TIFFIsCODECConfigured(compression), "A configured lossless TIFF codec is missing");
            for (const char* mode : {"wl", "wb", "w8l", "w8b"})
                decode(makeRGBA(path, compression, 1, mode), expectedColors(), 3, 2, "lossless/endian/BigTIFF");
        }
        std::cout << "PASS 4 lossless codecs x 2 byte orders x classic/BigTIFF\n";
        for (int orientation = 1; orientation <= 8; ++orientation)
            decode(makeRGBA(path, COMPRESSION_NONE, orientation), expectedColors(orientation), orientation <= 4 ? 3 : 2,
                   orientation <= 4 ? 2 : 3, "orientation " + std::to_string(orientation));
        std::cout << "PASS all 8 orientations, including dimension transposition\n";
        decode(makeRGBA(path, COMPRESSION_ADOBE_DEFLATE, 1, "wb", true), expectedColors(), 3, 2, "tiled Deflate");
        decode(makeRGBA(path, COMPRESSION_LZW, 1, "wl", false, EXTRASAMPLE_ASSOCALPHA), expectedColors(), 3, 2,
               "already associated alpha");
        std::cout << "PASS tiled TIFF, horizontal predictor, associated/unassociated alpha\n";
        grayscaleAndPalette(temporary.path);
        std::cout << "PASS black/white grayscale and palette conversion\n";
        jpeg(temporary.path);
        std::cout << "PASS JPEG TIFF using Axmol's existing libjpeg\n";
        const auto valid = makeRGBA(path, COMPRESSION_NONE);
        reject(Bytes{'I', 'I', 42, 0}, "truncated header");
        reject(Bytes{'M', 'M', 0, 42, 255, 255, 255, 255}, "IFD outside buffer");
        reject(Bytes(valid.begin(), valid.begin() + valid.size() / 2), "truncated IFD/strip");
        auto invalid = valid;
        patchClassicTag(invalid, TIFFTAG_IMAGEWIDTH, TIFF_LONG, 0xffffffffu);
        reject(invalid, "dimension/size overflow");
        invalid = valid;
        patchClassicTag(invalid, TIFFTAG_COMPRESSION, TIFF_SHORT, 50000);
        reject(invalid, "unknown compression");
        invalid = valid;
        patchClassicTag(invalid, TIFFTAG_STRIPOFFSETS, TIFF_LONG, 0xffffff00u);
        reject(invalid, "strip outside input");
        invalid = makeRGBA(path, COMPRESSION_ADOBE_DEFLATE);
        invalid[8] ^= 255;
        reject(invalid, "damaged compressed strip");
        ax::Image negative;
        require(!negative.initWithImageData(valid.data(), -1), "negative buffer length accepted");
        require(!negative.initWithImageData(nullptr, 1), "null buffer accepted");
        std::cout << "PASS 9 invalid/truncated/overflow/unsupported input cases\n";
        std::array<std::exception_ptr, 4> errors{};
        std::vector<std::thread> threads;
        for (size_t i = 0; i < errors.size(); ++i)
            threads.emplace_back([&, i] {
                try { for (int iteration = 0; iteration < 25; ++iteration)
                    decode(valid, expectedColors(), 3, 2, "parallel stream"); }
                catch (...) { errors[i] = std::current_exception(); }
            });
        for (auto& thread : threads) thread.join();
        for (auto error : errors) if (error) std::rethrow_exception(error);
        std::cout << "PASS 100 concurrent independent memory decodes\n";
        std::cout << "TIFF native Image tests: 13 original resources + 7 general groups PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
