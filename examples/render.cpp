/**
 * MIT License
 *
 * Copyright (c) 2018 Fabio Massaioli
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "rendirt.hpp"
#include "tiff.hpp"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <fstream>

using namespace rendirt;

std::ostream& operator<<(std::ostream& stream, glm::vec3 const& v) {
    return stream << "vec3{" << v.x << ", " << v.y << ", " << v.z << "}";
}

int main(int argc, char* argv[]) {
    // Loads a STL file and saves the rendered image to render.bmp
    // if no argument is given on the command line, reads model data from stdin

    std::istream* source = &std::cin;
    std::ifstream file;

    if (argc > 1) {
        source = &file;
        file.open(argv[1]);
        if (!file) {
            std::cerr << argv[1] << ": cannot open file for reading: " << strerror(errno) << std::endl;
            return -1;
        }
    }

    using frac_ms = std::chrono::duration<float, std::milli>;
    auto start = std::chrono::high_resolution_clock::now();

    // Load model from STL file. Let rendirt guess the format
    Model model;
    Model::Error err = model.loadSTL(*source);

    auto end = std::chrono::high_resolution_clock::now();

    if (err != Model::Ok) {
        std::cerr << "Model load failed after "
                  << std::chrono::duration_cast<frac_ms>(end - start).count() << " ms\n"
                  << "Error: " << Model::errorString(err)
                  << std::endl;
        return -1;
    }

    std::cout << "Model loaded in "
              << std::chrono::duration_cast<frac_ms>(end - start).count() << " ms\n"
              << "Face count: " << model.size() << '\n'
              << "Bounding box: { " << model.boundingBox().from << ", " << model.boundingBox().to << " }\n"
              << "Center: " << model.center() << '\n'
              << "Memory usage: " << double(model.capacity()*sizeof(Face))/1024.0 << " KB"
              << std::endl;

    // Create image 800x600 px image
    std::vector<Color> buffer(800*600);
    Image img(&buffer.front(), 800, 600);
    img.clear(Color(0, 0, 0, 255));

    // Precalculate useful values
    auto aspect = float(img.width) / float(img.height);
    auto diagonal = glm::abs(model.boundingBox().to - model.boundingBox().from);
    float maxDim = glm::max(diagonal.x, glm::max(diagonal.y, diagonal.z));

    (void)aspect; (void)maxDim;

    // With the center of the bounding box as origin,
    // place camera at (r=|diagonal|, theta=45deg, phi=45deg).
    // Look into the center of the bounding box.
    Camera view(
        model.center() + glm::length(diagonal)*glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)),
        model.center(),
        { 0.0f, 1.0f, 0.0f });

    // Build orthographic projection and ensure it fits the model
    /*Projection proj(
        Projection::Orthographic,
        -maxDim*aspect, maxDim*aspect,
        -maxDim, maxDim,
        0.0f, 2.0f*glm::length(diagonal));*/

    // Build perspective projection with 60deg fov
    Projection proj(
        Projection::Perspective,
        60.0f/180.0f*glm::pi<float>(), img.width, img.height,
        0.1f, 2.0f*glm::length(diagonal));

    start = std::chrono::high_resolution_clock::now();

    size_t count = render(img, model, view, proj,
        // shaders::depth);
        // shaders::position(model.boundingBox()));
        // shaders::normal);
        shaders::diffuseDirectional(glm::vec3(0.0f, -1.0f, -1.0f), Color(40, 40, 40, 255), Color(255, 255, 255, 255)));

    end = std::chrono::high_resolution_clock::now();

    std::cout << "Rendering completed in "
              << std::chrono::duration_cast<frac_ms>(end - start).count() << " ms\n"
              << "Resolution: " << img.width << "x" << img.height << " px\n"
              << "Rasterized faces: " << count
              << std::endl;

    // Write out rendered image
    std::ofstream output("./render.tiff", std::ofstream::binary);
    if (!output) {
        std::cerr << "./render.tiff: cannot open file for writing: " << strerror(errno) << std::endl;
        return -1;
    }

    // Convert image to premultiplied alpha
    using RGB16 = glm::vec<3, std::uint16_t>;

    for (size_t i = 0, end = img.height*img.stride; i < end; i += img.stride - img.width)
        for (size_t rend = i + img.width; i < rend; ++i)
            img.buffer[i] = Color(RGB16(img.buffer[i]) / std::uint16_t(256-img.buffer[i].a), img.buffer[i].a);

    if (!tiff::writeTIFF(output, img)) {
        std::cerr << "./render.tiff: write failed: " << strerror(errno) << std::endl;
        return -1;
    }

    return 0;
}
