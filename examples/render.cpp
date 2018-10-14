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

namespace rd = rendirt;

std::ostream& operator<<(std::ostream& stream, glm::vec3 const& v) {
    return stream << "vec3{" << v.x << ", " << v.y << ", " << v.z << "}";
}

int main(int argc, char* argv[]) {
    // Loads a STL file and saves the rendered image to render.tiff
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
    } else {
        std::cerr << "No file specified, reading from stdin" << std::endl;
    }

    using frac_ms = std::chrono::duration<float, std::milli>;
    auto start = std::chrono::high_resolution_clock::now();

    // Load model from STL file. Let rendirt guess the format
    rd::Model model;
    rd::Model::Error err = model.loadSTL(*source);

    auto end = std::chrono::high_resolution_clock::now();

    if (err != rd::Model::Ok) {
        std::cerr << "Model load failed after "
                  << std::chrono::duration_cast<frac_ms>(end - start).count() << " ms\n"
                  << "Error: " << rd::Model::errorString(err)
                  << std::endl;
        return -1;
    }

    std::cerr << "Model loaded in "
              << std::chrono::duration_cast<frac_ms>(end - start).count() << " ms\n"
              << "Face count: " << model.size() << '\n'
              << "Bounding box: { " << model.boundingBox().from << ", " << model.boundingBox().to << " }\n"
              << "Center: " << model.center() << '\n'
              << "Memory usage: " << double(model.capacity()*sizeof(rd::Face))/1024.0 << " KB"
              << std::endl;

    // Create 800x600 px image and depth buffer
    std::vector<rd::Color> colorBuffer(800*600);
    rd::Image<rd::Color> img(colorBuffer.data(), 800, 600);
    img.clear(rd::Color(0, 0, 0, 255));

    std::vector<float> depthBuffer(800*600);
    rd::Image<float> depth(depthBuffer.data(), 800, 600);
    depth.clear(1.0f); // Depth buffer must be cleared to 1.0f

    // Precalculate useful values
    float aspect = float(img.width) / float(img.height);
    glm::vec3 diagonal = glm::abs(model.boundingBox().to - model.boundingBox().from);
    float maxDim = glm::max(diagonal.x, glm::max(diagonal.y, diagonal.z));

    // Stop the compiler from complaining when orthographic projection
    // is commented out
    (void)aspect; (void)maxDim;

    // Use the center of the bounding box as origin.
    // Place camera at (r=|diagonal|, theta=45deg, phi=45deg).
    // Look into the center of the bounding box.
    rd::Camera view(
        model.center() + glm::length(diagonal)*glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)),
        model.center(),
        { 0.0f, 1.0f, 0.0f }); // The up vector specifies which direction is 'up'

    // Build orthographic projection and ensure it fits the model
    /*rd::Projection proj(
        rd::Projection::Orthographic,
        -maxDim*aspect, maxDim*aspect,
        -maxDim, maxDim,
        0.0f, 2.0f*glm::length(diagonal));*/

    // Build perspective projection with 60deg fov
    rd::Projection proj(
        rd::Projection::Perspective,
        60.0f/180.0f*glm::pi<float>(), img.width, img.height,
        0.1f, 2.0f*glm::length(diagonal));

    start = std::chrono::high_resolution_clock::now();

    size_t count = rd::render(img, depth, model, proj*view,
        // rd::shaders::depth);
        // rd::shaders::position(model.boundingBox()));
        // rd::shaders::normal);
        rd::shaders::diffuseDirectional(glm::vec3(0.0f, -1.0f, -1.0f), rd::Color(40, 40, 40, 255), rd::Color(200, 200, 200, 255)));

    end = std::chrono::high_resolution_clock::now();

    std::cerr << "Rendering completed in "
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
    using RGB48 = glm::vec<3, std::uint16_t>;

    for (size_t i = 0, end = img.height*img.stride; i < end; i += img.stride - img.width)
        for (size_t rend = i + img.width; i < rend; ++i)
            img.buffer[i] = rd::Color(RGB48(img.buffer[i]) / std::uint16_t(256-img.buffer[i].a), img.buffer[i].a);

    if (!tiff::writeTIFF(output, img)) {
        std::cerr << "./render.tiff: write failed: " << strerror(errno) << std::endl;
        return -1;
    }

    output.close();
    std::cerr << "Image saved to ./render.tiff" << std::endl;

    return 0;
}
