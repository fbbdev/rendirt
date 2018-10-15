#include <SDL.h>

#include "rendirt.hpp"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <fstream>

namespace rd = rendirt;

std::ostream& operator<<(std::ostream& stream, glm::vec3 const& v) {
    return stream << "vec3{" << v.x << ", " << v.y << ", " << v.z << "}";
}

int main(int argc, char* argv[]) {
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

    if (source == &file)
        file.close();

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

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    int width = 320, height = 320;
    unsigned int lastFrame = 0, timeout = 0;
    bool throttle = true;

    // Performance counters
    double trisPerFrame = 0.0;
    size_t frames = 0;
    unsigned int lastReport = 0, reportTimeout = 2000;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());
        return -1;
    }

    if (SDL_CreateWindowAndRenderer(width, height, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window and renderer: %s", SDL_GetError());
        return -1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create texture: %s", SDL_GetError());
        return -1;
    }

    float aspect = float(width) / float(height);
    glm::vec3 diagonal = glm::abs(model.boundingBox().to - model.boundingBox().from);
    float maxDim = glm::max(diagonal.x, glm::max(diagonal.y, diagonal.z));

    // Use the center of the bounding box as origin.
    // Starting position at (r=|diagonal|, theta=45deg, phi=45deg).
    // Look into the center of the bounding box.
    glm::vec3 eye = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 step = glm::vec3(-0.02f, -0.006f, 0.0f);

    rd::Camera view(
        model.center() + glm::length(diagonal)*glm::normalize(eye),
        model.center(),
        { 0.0f, 1.0f, 0.0f });

    // Build orthographic projection and ensure it fits the model
    rd::Projection ortho(
        rd::Projection::Orthographic,
        -maxDim*aspect, maxDim*aspect,
        -maxDim, maxDim,
        0.0f, 2.0f*glm::length(diagonal));

    // Build perspective projection with 60deg fov
    rd::Projection perspective(
        rd::Projection::Perspective,
        60.0f/180.0f*glm::pi<float>(), width, height,
        0.1f, 2.0f*glm::length(diagonal));

    rd::Projection* proj = &perspective;

    static char const* const shaderNames[] = { "depth", "position", "normal", "diffuseDirectional" };
    rd::Shader shaders[] = {
        rd::shaders::depth,
        rd::shaders::position(model.boundingBox()),
        rd::shaders::normal,
        rd::shaders::diffuseDirectional(glm::vec3(0.0f, -1.0f, -1.0f), rd::Color(40, 40, 40, 255), rd::Color(200, 200, 200, 255))
    };
    rd::Shader* currentShader = shaders;

    static char const* const cullingModeNames[] = { "CullNone", "CullCW", "CullCCW" };
    rd::CullingMode cullingModes[] = { rd::CullNone, rd::CullCW, rd::CullCCW };
    rd::CullingMode* currentCullingMode = &cullingModes[1];

    std::vector<float> depthBuffer(width*height);
    rd::Image<float> depth(depthBuffer.data(), width, height);
    depth.clear(1.0f);

    std::cerr << "Starting renderer.\n"
              << "Current shader: " << shaderNames[currentShader-shaders] << ". Press SPACE to change.\n"
              << "Current projection: " << ((proj == &ortho) ? "orthographic" : "perspective") << ". Press 'p' to change.\n"
              << "Current culling mode: " << cullingModeNames[currentCullingMode-cullingModes] << ". Press 'c' to change.\n"
              << "Frame throttling " << (throttle ? "enabled" : "disabled") << ". "
              << "Press 't' to switch " << (throttle ? "off" : "on") << '.'
              << std::endl;

    while (1) {
        if (SDL_WaitEventTimeout(NULL, timeout)) {
            if (SDL_QuitRequested())
                break;

            SDL_Event ev;
            while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) {
                if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    int nw = 0, nh = 0;
                    SDL_GetWindowSize(window, &nw, &nh);
                    if (width == nw && height == nh)
                        continue;

                    SDL_Texture* newtex = SDL_CreateTexture(
                        renderer, SDL_PIXELFORMAT_ABGR8888,
                        SDL_TEXTUREACCESS_STREAMING, nw, nh);
                    if (newtex) {
                        SDL_DestroyTexture(texture);
                        texture = newtex;
                        width = nw;
                        height = nh;
                        aspect = float(width) / float(height);

                        // Aspect changed, rebuild depth buffer and projection matrices
                        depthBuffer.resize(width*height);
                        depth = rd::Image<float>(depthBuffer.data(), width, height);

                        ortho = rd::Projection(
                            rd::Projection::Orthographic,
                            -maxDim*aspect, maxDim*aspect,
                            -maxDim, maxDim,
                            0.0f, 2.0f*glm::length(diagonal));

                        perspective = rd::Projection(
                            rd::Projection::Perspective,
                            60.0f/180.0f*glm::pi<float>(), width, height,
                            0.1f, 2.0f*glm::length(diagonal));
                    }
                } else if (ev.type == SDL_WINDOWEVENT &&
                           ev.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    lastFrame = 0;
                } else if (ev.type == SDL_KEYDOWN) {
                    switch (ev.key.keysym.sym) {
                        case SDLK_SPACE:
                            // When SPACE is pressed, cycle shaders
                            ++currentShader;
                            if (currentShader >= shaders + (sizeof(shaders)/sizeof(rd::Shader)))
                                currentShader = shaders;
                            std::cerr << "Current shader: " << shaderNames[currentShader-shaders] << std::endl;
                            break;

                        case SDLK_p:
                            // When 'p' is pressed, swap projection
                            proj = (proj == &perspective) ? &ortho : &perspective;
                            std::cerr << "Current projection: " << ((proj == &ortho) ? "orthographic" : "perspective") << std::endl;
                            break;

                        case SDLK_c:
                            // When 'c' is pressed, cycle culling modes
                            ++currentCullingMode;
                            if (currentCullingMode >= cullingModes + (sizeof(cullingModes)/sizeof(rd::CullingMode)))
                                currentCullingMode = cullingModes;
                            std::cerr << "Current culling mode: " << cullingModeNames[currentCullingMode-cullingModes] << std::endl;
                            break;

                        case SDLK_t:
                            // When 't' is pressed, switch frame throttling on or off
                            throttle = !throttle;
                            std::cerr << "Frame throttling " << (throttle ? "enabled" : "disabled") << ". "
                                      << "Press 't' to switch " << (throttle ? "off" : "on") << '.'
                                      << std::endl;
                            break;
                    }
                }
            }
        }

        // Frame throttling. Target 25fps
        unsigned int ticks = SDL_GetTicks();
        if (throttle && ticks - lastFrame < 35) {
            timeout = 35 - (ticks - lastFrame);
            continue;
        }

        lastFrame = ticks;

        void* pixels = NULL;
        int pitch = 0;
        if (!SDL_LockTexture(texture, NULL, &pixels, &pitch)) {
            if (pitch % sizeof(rd::Color)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Texture pitch is not a multiple of pixel size: %d", pitch);
                return -1;
            }

            // Animate camera
            eye += step;

            if ((eye.x <= -1.0f && step.x < 0.0f) || (eye.x >= 1.0f && step.x > 0.0f))
                std::swap(step.x, step.z);

            if ((eye.y <= -1.0f && step.y < 0.0f) || (eye.y >= 1.0f && step.y > 0.0f))
                step.y = -step.y;

            if ((eye.z <= -1.0f && step.z < 0.0f) || (eye.z >= 1.0f && step.z > 0.0f)) {
                step.z = -step.z;
                std::swap(step.x, step.z);
            }

            view = rd::Camera(
                model.center() + glm::length(diagonal)*glm::normalize(eye),
                model.center(),
                { 0.0f, 1.0f, 0.0f });

            rd::Image<rd::Color> img(reinterpret_cast<rd::Color*>(pixels), width, height, pitch/sizeof(rd::Color));
            img.clear(rd::Color(0, 0, 0, 255));
            depth.clear(1.0f);

            trisPerFrame += (rd::render(img, depth, model, *proj * view, *currentShader, *currentCullingMode)
                - trisPerFrame) / (frames+1) ;

            SDL_UnlockTexture(texture);
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't lock texture: %s", SDL_GetError());
            return -1;
        }

        ++frames;
        ticks = SDL_GetTicks();

        timeout = throttle ? (35 - glm::min(ticks - lastFrame, 35u)) : 0;

        if (ticks - lastReport > reportTimeout) {
            std::cerr << "FPS: " << double(frames)/((ticks - lastReport)*0.001) << "; "
                      << "Avg triangles per frame: " << trisPerFrame*0.001 << "k"
                      << std::endl;

            trisPerFrame = 0.0;
            frames = 0;
            lastReport = ticks;
        }

        SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0x00);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}
