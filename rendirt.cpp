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

#define GLM_ENABLE_EXPERIMENTAL

#ifdef NDEBUG
    #define GLM_FORCE_INLINE
#endif

#include "rendirt.hpp"

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/normal.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
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
    size_t skipWhitespace(std::istream& stream, size_t limit = -1) {
        size_t count = 0;

        while (std::isspace(stream.peek()) && count < limit) {
            stream.ignore();
            ++count;
        }

        return count;
    }
} /* namespace */

Model::Error Model::loadTextSTL(std::istream& stream, Model& model, bool useNormals, bool verified) {
    std::string tok;

    model.clear();

    stream >> std::skipws;

    if (!verified) {
        // Read and verify 'solid' signature
        stream >> tok;
        if (!stream) // If error or eof, we have a problem
            return FileTruncated;
        else if (tok != "solid")
            return UnexpectedToken;
    }

    // Read (and ignore) model name
    stream >> tok;
    if (!stream)
        return FileTruncated;

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
    // Reassign to free excess memory
    model = Model();

    // Skip header
    stream.ignore(80 - skipped);

    // Read size (32 bit unsigned integer)
    uint32_t size = 0;
    stream.read(reinterpret_cast<char*>(&size), sizeof(uint32_t));
    if (stream.gcount() < std::streamsize(sizeof(uint32_t)))
        return FileTruncated;

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
        char signature[6];

        skipped += skipWhitespace(stream, 80);
        if (skipped == 80)
            return GuessFailed;

        size_t available = glm::min(size_t(6), 80 - skipped);
        stream.read(signature, available);
        if (stream.gcount() < std::streamsize(available))
            return FileTruncated;

        skipped += available;

        if (available == 6 && std::equal(signature, signature + 5, "solid") && std::isspace(signature[5]))
            mode = Text;
        else if (available < 6 && std::equal(signature, signature + available, "solid"))
            return GuessFailed;
        else
            mode = Binary;
    }

    return (mode == Text) ? loadTextSTL(stream, *this, useNormals, skipped > 0)
                          : loadBinarySTL(stream, *this, useNormals, skipped);
}

char const* Model::errorString(Error err) {
    static char const* strings[LastError + 1] = {
        "no error",
        "invalid token",
        "unexpected token",
        "file truncated",
        "guess failed"
    };

    return strings[(unsigned int) err];
}

// Renderer
size_t rendirt::render(Image<Color> const& color, Image<float> const& depth,
                       Model const& model, glm::mat4 const& modelViewProj,
                       Shader const& shader, CullingMode cullingMode)
{
    assert(color.width == depth.width && color.height == depth.height);

    size_t faceCount = 0;

    using vec2s = glm::vec<2, size_t>;
    const vec2s imgSize(color.width, color.height);
    const glm::vec2 imgSizef(imgSize);

    glm::vec2 sampleStep = glm::vec2(2.0f, -2.0f)/imgSizef;

    for (auto const& face: model) {
        glm::vec4 clipf[3] = {
            modelViewProj * glm::vec4(face.vertex[0], 1.0f),
            modelViewProj * glm::vec4(face.vertex[1], 1.0f),
            modelViewProj * glm::vec4(face.vertex[2], 1.0f)
        };

        clipf[0] /= clipf[0].w; clipf[1] /= clipf[1].w; clipf[2] /= clipf[2].w;

        // Face culling by winding detection
        const float doubleArea = ((clipf[0].y - clipf[1].y)*clipf[2].x + (clipf[1].x - clipf[0].x)*clipf[2].y + (clipf[0].x*clipf[1].y - clipf[0].y*clipf[1].x));
        if ((cullingMode == CullCW && doubleArea <= 0.0f) || (cullingMode == CullCCW && doubleArea > 0.0f))
            continue;

        AABB brect = {
            glm::min(clipf[0], glm::min(clipf[1], clipf[2])),
            glm::max(clipf[0], glm::max(clipf[1], clipf[2]))
        };

        brect.from = glm::max(brect.from, glm::vec3(-1.0f, -1.0f, -1.0f));
        brect.to = glm::min(brect.to, glm::vec3(1.0f, 1.0f, 1.0f));
        const auto dims = glm::abs(brect.to - brect.from);

        // Discard faces outside clipping planes
        if (dims.x <= 0.0f || dims.y <= 0.0f || brect.from.z >= 1.0f || brect.to.z <= -1.0f)
            continue;

        ++faceCount;

        const vec2s from =
            glm::clamp(vec2s(glm::floor((glm::vec2(brect.from.x, -brect.to.y)*0.5f + 0.5f)*imgSizef)), vec2s(0, 0), imgSize);
        const vec2s to =
            glm::clamp(vec2s(glm::ceil((glm::vec2(brect.to.x, -brect.from.y)*0.5f + 0.5f)*imgSizef)), vec2s(0, 0), imgSize);

        // Matrix for computing barycentric coordinates normalized so their sum is 1
        // XXX: column-major
        const glm::mat3 barycentric = glm::mat3{
            { clipf[1].y - clipf[2].y,                       clipf[2].y - clipf[0].y,                       clipf[0].y - clipf[1].y },
            { clipf[2].x - clipf[1].x,                       clipf[0].x - clipf[2].x,                       clipf[1].x - clipf[0].x },
            { clipf[1].x*clipf[2].y - clipf[1].y*clipf[2].x, clipf[2].x*clipf[0].y - clipf[2].y*clipf[0].x, clipf[0].x*clipf[1].y - clipf[0].y*clipf[1].x },
        } / doubleArea;

        const glm::vec3 posParams[3] = {
            face.vertex[0],
            face.vertex[1] - face.vertex[0],
            face.vertex[2] - face.vertex[0]
        };
        const glm::vec3 zParams(clipf[0].z, clipf[1].z - clipf[0].z, clipf[2].z - clipf[0].z);

        const glm::vec2 sampleStart = (glm::vec2(from.x + 0.5f, from.y + 0.5f)/imgSizef - 0.5f) * glm::vec2(2.0f, -2.0f);
        glm::vec2 sample = sampleStart;

        glm::vec3 rowLambda = barycentric * glm::vec3(sampleStart, 1.0f);
        glm::vec3 lambda = rowLambda;

        const glm::vec3 rowLambdaStep = barycentric[1]*sampleStep.y;
        const glm::vec3 lambdaStep = barycentric[0]*sampleStep.x;

        for (size_t y = from.y; y < to.y; ++y, sample.y += sampleStep.y, rowLambda += rowLambdaStep) {
            sample.x = sampleStart.x;
            lambda = rowLambda;

            for (size_t x = from.x; x < to.x; ++x, sample.x += sampleStep.x, lambda += lambdaStep) {
                // Interpolate position and depth
                const glm::vec3 pos = posParams[0] + lambda.y*posParams[1] + lambda.z*posParams[2];
                const float z = zParams.x + lambda.y*zParams.y + lambda.z*zParams.z;

                // Test if inside triangle, then depth test
                if (!(std::signbit(lambda.x) | std::signbit(lambda.y) | std::signbit(lambda.z)) &&
                    z > -1.0f && z < depth.buffer[y*depth.stride + x])
                {
                    depth.buffer[y*depth.stride + x] = z;
                    color.buffer[y*color.stride + x] = shader(glm::vec3(sample, z), pos, face.normal);
                }
            }
        }
    }

    return faceCount;
}
