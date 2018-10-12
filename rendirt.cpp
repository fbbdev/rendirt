#define GLM_ENABLE_EXPERIMENTAL

#ifdef NDEBUG
    #define GLM_FORCE_INLINE
#endif

#include "rendirt.hpp"

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/normal.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <numeric>

using namespace rendirt;

// Model methods
void Model::updateBoundingBox() {
    if (empty())
        boundingBox_ = { { 0, 0, 0 }, { 0, 0, 0 } };
    else
        boundingBox_ = std::accumulate(begin(), end(),
            AABB{
                glm::min(front().vertex[0], glm::min(front().vertex[1], front().vertex[2])),
                glm::max(front().vertex[0], glm::max(front().vertex[1], front().vertex[2]))
            }, [](AABB const& box, Face const& face) -> AABB {
                return {
                    glm::min(box.from, glm::min(face.vertex[0], glm::min(face.vertex[1], face.vertex[2]))),
                    glm::max(box.to,   glm::max(face.vertex[0], glm::max(face.vertex[1], face.vertex[2])))
                };
            });
}

namespace {
    // STL format parsing helpers
    size_t skipWhitespace(std::istream& stream) {
        size_t count = 0;

        while (std::isspace(stream.peek())) {
            stream.ignore();
            ++count;
        }

        return count;
    }
} /* namespace */

Model::Error Model::loadTextSTL(std::istream& stream, Model& model, bool useNormals) {
    std::string tok;

    // Read (and ignore) model name
    stream >> std::skipws >> tok;
    if (!stream) // If error or eof, we have a problem
        return FileTruncated;

    model.clear();

    stream >> tok;
    if (stream.fail())
        return FileTruncated;

    Face face = {};

    for (bool first = true; tok == "facet";) {
        // Read normal
        stream >> tok;
        if (!stream)
            return FileTruncated;
        else if (tok != "normal")
            return UnexpectedToken;

        stream >> face.normal.x
               >> face.normal.y
               >> face.normal.z;
        if (stream.eof())
            return FileTruncated;
        else if (!stream)
            return InvalidToken;

        // Read vertices
        stream >> tok;
        if (!stream)
            return FileTruncated;
        else if (tok != "outer")
            return UnexpectedToken;

        stream >> tok;
        if (!stream)
            return FileTruncated;
        else if (tok != "loop")
            return UnexpectedToken;

        for (unsigned int i = 0; i < 3; ++i) {
            stream >> tok;
            if (!stream)
                return FileTruncated;
            else if (tok != "vertex")
                return UnexpectedToken;

            stream >> face.vertex[i].x
                   >> face.vertex[i].y
                   >> face.vertex[i].z;
            if (stream.eof())
                return FileTruncated;
            else if (!stream)
                return InvalidToken;
        }

        stream >> tok;
        if (!stream)
            return FileTruncated;
        else if (tok != "endloop")
            return UnexpectedToken;

        stream >> tok;
        if (!stream)
            return FileTruncated;
        else if (tok != "endfacet")
            return UnexpectedToken;

        // Recompute normal (some programs are known to write garbage)
        if (!useNormals)
            face.normal = glm::triangleNormal(face.vertex[0], face.vertex[1], face.vertex[2]);

        model.push_back(face);

        // Update bounding box
        if (first) {
            first = false;
            model.boundingBox_ = {
                glm::min(face.vertex[0], glm::min(face.vertex[1], face.vertex[2])),
                glm::max(face.vertex[0], glm::max(face.vertex[1], face.vertex[2]))
            };
        }

        model.boundingBox_ = {
            glm::min(model.boundingBox_.from, glm::min(face.vertex[0], glm::min(face.vertex[1], face.vertex[2]))),
            glm::max(model.boundingBox_.to,   glm::max(face.vertex[0], glm::max(face.vertex[1], face.vertex[2])))
        };

        // Read next face or end of model
        stream >> tok;
        if (stream.fail())
            return FileTruncated;
    }

    if (tok != "endsolid")
        return tok.empty() ? FileTruncated : UnexpectedToken;

    model.shrink_to_fit();
    return Ok;
}

Model::Error Model::loadBinarySTL(std::istream& stream, Model& model, bool useNormals, size_t skipped) {
    // Skip header
    stream.ignore(80 - skipped);

    // Read size (32 bit unsigned integer)
    uint32_t size = 0;
    stream.read(reinterpret_cast<char*>(&size), sizeof(uint32_t));
    if (stream.gcount() < std::streamsize(sizeof(uint32_t)))
        return FileTruncated;

    // Reassign to free memory
    model = Model();
    model.reserve(size);
    model.resize(size);

    auto begin = model.begin(), end = model.end();
    uint16_t attrs = 0;

    for (auto face = begin; face != end; ++face) {
        stream.read(reinterpret_cast<char*>(&*face), sizeof(Face));
        if (stream.gcount() < std::streamsize(sizeof(Face)))
            return FileTruncated;

        // Ignore attrs: they should be zero, some programs use them
        // as color values
        stream.read(reinterpret_cast<char*>(&attrs), sizeof(uint16_t));
        if (stream.gcount() < std::streamsize(sizeof(uint16_t)))
            return FileTruncated;

        // Recompute normal (some programs are known to write garbage)
        if (!useNormals)
            face->normal = glm::triangleNormal(face->vertex[0], face->vertex[1], face->vertex[2]);

        // Update bounding box
        if (face == begin)
            model.boundingBox_ = {
                glm::min(face->vertex[0], glm::min(face->vertex[1], face->vertex[2])),
                glm::max(face->vertex[0], glm::max(face->vertex[1], face->vertex[2]))
            };

        model.boundingBox_ = {
            glm::min(model.boundingBox_.from, glm::min(face->vertex[0], glm::min(face->vertex[1], face->vertex[2]))),
            glm::max(model.boundingBox_.to,   glm::max(face->vertex[0], glm::max(face->vertex[1], face->vertex[2])))
        };
    }

    return Ok;
}

Model::Error Model::loadSTL(std::istream& stream, bool useNormals, Mode mode) {
    size_t skipped = 0;

    if (mode == Guess) {
        char signature[5];

        skipped += skipWhitespace(stream);
        if (skipped > 75)
            return GuessFailed;

        stream.read(signature, 5);
        if (stream.gcount() < 5)
            return FileTruncated;

        skipped += 5;
        mode = std::equal(signature, signature + 5, "solid") ? Text : Binary;
    }

    return (mode == Text) ? loadTextSTL(stream, *this, useNormals)
                          : loadBinarySTL(stream, *this, useNormals, skipped);
}

char const* Model::errorString(Error err) {
    static char const* strings[ErrorCount] = {
        "no error",
        "invalid token",
        "unexpected token",
        "file truncated",
        "guess failed"
    };

    return strings[(unsigned int) err];
}

// Image methods
void Image::clear(Color color) {
    for (Color *p = buffer, *end = buffer + height*stride; p != end; p += stride)
        std::fill(p, p + width, color);
}

// Renderer
size_t rendirt::render(Image const& image, Model const& model,
                       glm::mat4 const& view, glm::mat4 const& projection,
                       Shader const& shader, CullingMode cullingMode)
{
    size_t faceCount = 0;

    std::vector<float> depth(image.width*image.height, 1.0f);

    using vec2s = glm::vec<2, size_t>;
    vec2s imgSize(image.width, image.height);
    glm::vec2 imgSizef(imgSize);

    glm::mat4 viewProj = projection * view;

    for (auto const& face: model) {
        glm::vec4 clipf[3] = {
            viewProj * glm::vec4(face.vertex[0], 1.0f),
            viewProj * glm::vec4(face.vertex[1], 1.0f),
            viewProj * glm::vec4(face.vertex[2], 1.0f)
        };

        clipf[0] /= clipf[0].w; clipf[1] /= clipf[1].w; clipf[2] /= clipf[2].w;

        // Face culling by winding detection
        float doubleArea = ((clipf[0].y - clipf[1].y)*clipf[2].x + (clipf[1].x - clipf[0].x)*clipf[2].y + (clipf[0].x*clipf[1].y - clipf[0].y*clipf[1].x));
        if ((cullingMode == CullCW && doubleArea <= 0.0f) || (cullingMode == CullCCW && doubleArea > 0.0f))
            continue;

        AABB brect = {
            glm::min(clipf[0], glm::min(clipf[1], clipf[2])),
            glm::max(clipf[0], glm::max(clipf[1], clipf[2]))
        };

        brect.from = glm::max(brect.from, glm::vec3(-1.0f, -1.0f, -1.0f));
        brect.to = glm::min(brect.to, glm::vec3(1.0f, 1.0f, 1.0f));
        auto dims = glm::abs(brect.to - brect.from);

        // Discard faces outside clipping planes
        if (dims.x <= 0.0f || dims.y <= 0.0f || brect.from.z >= 1.0f || brect.to.z <= -1.0f)
            continue;

        ++faceCount;

        vec2s from =
            glm::clamp(vec2s(glm::floor((glm::vec2(brect.from.x, -brect.to.y)*0.5f + 0.5f)*imgSizef)), vec2s(0, 0), imgSize);
        vec2s to =
            glm::clamp(vec2s(glm::ceil((glm::vec2(brect.to.x, -brect.from.y)*0.5f + 0.5f)*imgSizef)), vec2s(0, 0), imgSize);

        // Matrix for computing barycentric coordinates normalized so their sum is 1
        // XXX: column-major
        glm::mat3 barycentric = glm::mat3{
            { clipf[1].y - clipf[2].y,                       clipf[2].y - clipf[0].y,                       clipf[0].y - clipf[1].y },
            { clipf[2].x - clipf[1].x,                       clipf[0].x - clipf[2].x,                       clipf[1].x - clipf[0].x },
            { clipf[1].x*clipf[2].y - clipf[1].y*clipf[2].x, clipf[2].x*clipf[0].y - clipf[2].y*clipf[0].x, clipf[0].x*clipf[1].y - clipf[0].y*clipf[1].x },
        } / doubleArea;

        glm::vec3 posParams[3] = {
            face.vertex[0],
            face.vertex[1] - face.vertex[0],
            face.vertex[2] - face.vertex[0]
        };
        glm::vec3 zParams(clipf[0].z, clipf[1].z - clipf[0].z, clipf[2].z - clipf[0].z);

        for (size_t y = from.y; y < to.y; ++y) {
            for (size_t x = from.x; x < to.x; ++x) {
                glm::vec2 sample = (glm::vec2(x + 0.5f, y + 0.5f)/imgSizef - 0.5f) * glm::vec2(2.0f, -2.0f);

                // Compute barycentric coordinates
                glm::vec3 lambda = barycentric * glm::vec3(sample, 1.0f);

                // Interpolate position and depth
                glm::vec3 pos = posParams[0] + lambda.y*posParams[1] + lambda.z*posParams[2];
                float z = zParams.x + lambda.y*zParams.y + lambda.z*zParams.z;

                // Test if inside triangle, then depth test
                if (lambda.x >= 0.0f && lambda.y >= 0.0f && lambda.z >= 0.0f &&
                    z > -1.0f && z < depth[y*image.width + x])
                {
                    depth[y*image.width + x] = z;
                    image.buffer[y*image.stride + x] = shader(glm::vec3(sample, z), pos, face.normal);
                }
            }
        }
    }

    return faceCount;
}
