#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <functional>
#include <istream>
#include <string>
#include <vector>

namespace rendirt {

struct Face {
    glm::vec3 normal;
    glm::vec3 vertex[3];
};

struct AABB {
    glm::vec3 from;
    glm::vec3 to;
};

class Model : public std::vector<Face> {
public:
    enum Error : uint8_t {
        Ok = 0,
        InvalidToken,
        UnexpectedToken,
        FileTruncated,
        GuessFailed,

        ErrorCount
    };

    enum Mode : uint8_t {
        Guess = 0,
        Text,
        Binary
    };

    using std::vector<Face>::vector;

    AABB const& boundingBox() const {
        return boundingBox_;
    }

    glm::vec3 center() const {
        return (boundingBox_.from + boundingBox_.to) / 2.0f;
    }

    void updateBoundingBox();

    Error loadSTL(std::istream& stream, Mode mode = Guess) {
        return loadSTL(stream, false, mode);
    }

    Error loadSTL(std::istream& stream, bool useNormals, Mode mode = Guess);

    static char const* errorString(Error err);
private:
    AABB boundingBox_;

    static Error loadTextSTL(std::istream& stream, Model& model, bool useNormals);
    static Error loadBinarySTL(std::istream& stream, Model& model, bool useNormals, size_t skipped);
};

struct Projection : glm::mat4 {
    using glm::mat4::mat;

    static constexpr struct FrustumTag {} Frustum = {};
    static constexpr struct PerspectiveTag {} Perspective = {};
    static constexpr struct OrthographicTag {} Orthographic = {};

    explicit Projection(FrustumTag, float left, float right, float bottom, float top, float near, float far)
        : glm::mat4(glm::frustum(left, right, bottom, top, near, far))
        {}

    explicit Projection(PerspectiveTag, float fov, float width, float height, float near, float far)
        : glm::mat4(glm::perspectiveFov(fov, width, height, near, far))
        {}

    explicit Projection(OrthographicTag, float left, float right, float bottom, float top, float zNear, float zFar)
        : glm::mat4(glm::ortho(left, right, bottom, top, zNear, zFar))
        {}
};

struct Camera : glm::mat4 {
    using glm::mat4::mat;

    explicit Camera(glm::vec3 const& eye, glm::vec3 const& center, glm::vec3 const& up)
        : glm::mat4(glm::lookAt(eye, center, up))
        {}
};

using Color = glm::vec<4, uint8_t>;

using Shader = std::function<Color(glm::vec3 /* frag */, glm::vec3 /* pos */, glm::vec3 /* normal */)>;

struct Image {
    constexpr Image(Color* buf, size_t w, size_t h)
        : buffer(buf), width(w), height(h), stride(w)
        {}

    constexpr Image(Color* buf, size_t w, size_t h, size_t s)
        : buffer(buf), width(w), height(h), stride(s)
        {}

    void clear(Color color);

    Color* buffer;
    size_t width;
    size_t height;
    size_t stride;
};

enum CullingMode : uint8_t {
    CullNone = 0,
    CullCW,
    CullCCW,
    CullBack = CullCW,
    CullFront = CullCCW
};

// Returns number of faces actually rendered
size_t render(Image const& image, Model const& model,
              glm::mat4 const& view, glm::mat4 const& projection,
              Shader const& shader, CullingMode cullingMode = CullCW);

namespace shaders {
    // Depth is scaled from range [-1,1] to range [0,1]
    inline Color depth(glm::vec3 frag, glm::vec3, glm::vec3) {
        uint8_t depth = (frag.z*0.5f + 0.5f)*255.0f;
        return Color(depth, depth, depth, 255);
    }

    // Expects each component of bbox.to to be strictly greater than or equal
    // to the corresponding component of bbox.from.
    // Color components range from (0, 0, 0) at bbox.from,
    // to (255, 255, 255) at bbox.to.
    inline Shader position(AABB bbox) {
        bbox.to -= bbox.from;
        return [bbox](glm::vec3, glm::vec3 pos, glm::vec3) {
            return Color(((pos - bbox.from)/bbox.to)*255.0f, 255);
        };
    }

    // Normal components are scaled from range [-1,1] to range [0,1]
    inline Color normal(glm::vec3, glm::vec3, glm::vec3 normal) {
        return Color((normal*0.5f + 0.5f)*255.0f, 255);
    }

    // Takes: direction of the light, ambient color, diffuse color
    // Expects normal vectors to be normalized
    inline Shader diffuseDirectional(glm::vec3 dir, Color ambient, Color diffuse) {
        dir = glm::normalize(dir);
        return [dir,ambient,diffuse](glm::vec3, glm::vec3, glm::vec3 normal) {
            glm::vec4 color = glm::vec4(ambient) + glm::max(-glm::dot(normal, dir), 0.0f)*glm::vec4(diffuse);
            return Color(glm::clamp(color, 0.0f, 255.0f));
        };
    }
} /* namespace shaders */

} /* namespace rendirt */
