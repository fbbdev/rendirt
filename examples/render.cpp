#include "rendirt.hpp"

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
        shaders::diffuseDirectional(glm::vec3(0.0f, -1.0f, -1.0f), Color(40, 40, 40, 255), Color(255, 255, 255, 255)));

    end = std::chrono::high_resolution_clock::now();

    std::cout << "Rendering completed in "
              << std::chrono::duration_cast<frac_ms>(end - start).count() << " ms\n"
              << "Resolution: " << img.width << "x" << img.height << " px\n"
              << "Rasterized faces: " << count
              << std::endl;

    // Write out rendered image. Format is R8G8B8A8 (byte order)
    std::ofstream output("./example.raw", std::ofstream::binary);
    if (!output) {
        std::cerr << "./example.raw: cannot open file for writing: " << strerror(errno) << std::endl;
        return -1;
    }

    output.write(reinterpret_cast<char const*>(buffer.data()), buffer.size()*sizeof(Color));
    output.close();

    return 0;
}
