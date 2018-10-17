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
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/normal.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>

using namespace rendirt;

// Model methods
void Model::updateBoundingBox() {
    if (vertices.empty())
        boundingBox_ = { { 0, 0, 0 }, { 0, 0, 0 } };
    else
        boundingBox_ = std::accumulate(std::next(vertices.begin()), vertices.end(),
            AABB{ vertices.front(), vertices.front() },
            [](AABB const& box, glm::vec3 const& vec) -> AABB {
                return { glm::min(box.from, vec), glm::max(box.to, vec) };
            });
}

namespace {
    static constexpr std::size_t InvalidIndex = std::numeric_limits<std::size_t>::max();

    struct BVHObject {
        size_t face;
        AABB bbox;
        glm::vec3 centroid;
    };

    AABB accumulateBbox(AABB const& bbox, BVHObject const& obj) {
        return AABB{ glm::min(bbox.from, obj.bbox.from), glm::max(bbox.to, obj.bbox.to) };
    }

    template<typename Iterator>
    void buildBVHSide(std::vector<BVHNode>& bvh, AABB bbox,
                      Iterator base, Iterator first, Iterator last,
                      size_t targetLoad)
    {
        size_t index = bvh.size();
        bvh.push_back(BVHNode{
            bbox, true,
            size_t(first - base),
            size_t(last - base)
        });

        if (last - first <= std::intptr_t(targetLoad))
            return;

        // Sort objects by centroid position along the largest dimension
        // of the bounding box
        glm::vec3 diag = glm::abs(bbox.to - bbox.from);
        std::uint8_t dir = (diag.y > diag.x) ? ((diag.z > diag.y) ? 2 : 1)
                                             : ((diag.z > diag.x) ? 2 : 0);
        std::sort(first, last, [dir](BVHObject const& l, BVHObject const& r) {
            return l.centroid[dir] < r.centroid[dir];
        });

        glm::vec3 centroid = (bbox.from + bbox.to) / 2.0f;
        AABB childBbox = first->bbox;

        Iterator median = first + (last - first)/2;
        AABB medianBbox = {};

        Iterator split = std::next(first);
        for (; split != last; ++split) {
            childBbox.from = glm::min(childBbox.from, split->bbox.from);
            childBbox.to = glm::max(childBbox.to, split->bbox.to);

            if (split == median)
                medianBbox = childBbox;

            if (split->centroid[dir] > centroid[dir])
                break;
        }

        // If empty, revert to median splitting
        if (split == last) {
            split = median;
            childBbox = medianBbox;
        }

        bvh[index].leaf = false;
        bvh[index].leftOrFirstFace = bvh.size();
        buildBVHSide(bvh, childBbox, base, first, split, targetLoad);

        childBbox = std::accumulate(std::next(split), last, split->bbox, accumulateBbox);

        bvh[index].rightOrLastFace = bvh.size();
        return buildBVHSide(bvh, childBbox, base, split, last, targetLoad);
    }
} /* namespace */

void Model::rebuildBVH(size_t targetLoad) {
    bvh_.clear();

    if (faces.empty() || vertices.empty())
        return;

    std::vector<BVHObject> objects;
    objects.reserve(faces.size());

    size_t index = 0;
    std::transform(faces.begin(), faces.end(), std::back_inserter(objects), [this,&index](Face const& face) {
        AABB bbox = {
            glm::min(vertices[face.vertex[0]], glm::min(vertices[face.vertex[1]], vertices[face.vertex[2]])),
            glm::max(vertices[face.vertex[0]], glm::max(vertices[face.vertex[1]], vertices[face.vertex[2]]))
        };

        return BVHObject{index++, bbox, (bbox.from + bbox.to) / 2.0f};
    });

    bvh_.reserve(2*objects.size() + 1);
    buildBVHSide(bvh_, boundingBox(), objects.begin(), objects.begin(), objects.end(), targetLoad + 1);

    std::vector<Face> sortedFaces;

    sortedFaces.reserve(objects.size());
    std::transform(objects.begin(), objects.end(), std::back_inserter(sortedFaces), [this](BVHObject const& obj) {
        return faces[obj.face];
    });

    faces.swap(sortedFaces);
    sortedFaces.clear();
    sortedFaces.shrink_to_fit();

    std::vector<size_t> remap(vertices.size(), InvalidIndex);
    std::vector<glm::vec3> sortedVertices;

    sortedVertices.reserve(vertices.size());

    for (auto& face: faces) {
        for (auto& vertex: face.vertex) {
            if (remap[vertex] == InvalidIndex) {
                remap[vertex] = sortedVertices.size();
                sortedVertices.push_back(vertices[vertex]);
            }

            vertex = remap[vertex];
        }
    }

    vertices.swap(sortedVertices);
    sortedVertices.clear();
    sortedVertices.shrink_to_fit();

    bvh_.shrink_to_fit();
}

namespace {
    // Model loading helpers
    void deduplicateVertices(std::vector<glm::vec3>& vertices,
                             std::vector<Face>& faces) {
        if (vertices.size() < 2)
            return;

        std::vector<size_t> sorted(vertices.size());
        std::iota(sorted.begin(), sorted.end(), size_t(0));

        // Sort by component value
        std::stable_sort(sorted.begin(), sorted.end(), [&vertices](size_t l, size_t r) {
            if (vertices[l].x < vertices[r].x) {
                return true;
            } else if (vertices[l].x == vertices[r].x) {
                if (vertices[l].y < vertices[r].y)
                    return true;
                else if (vertices[l].y == vertices[r].y)
                    return vertices[l].z < vertices[r].z;
            }

            return false;
        });

        std::vector<size_t> remap(vertices.size());
        std::iota(remap.begin(), remap.end(), size_t(0));

        // Deduplicate and build reverse mapping
        for (auto cur = sorted.begin(), it = std::next(sorted.begin()), end = sorted.end(); it != end; ++it) {
            if (vertices[*it] == vertices[*cur])
                remap[*it] = *cur;
            else
                cur = it;
        }

        // Relocate vertices
        size_t cur = 0;
        for (size_t pos = 1; pos < remap.size(); ++pos) {
            if (remap[pos] == pos) {
                if (++cur != pos) {
                    remap[pos] = cur;
                    vertices[cur] = vertices[pos];
                }
            } else {
                remap[pos] = remap[remap[pos]];
            }
        }

        vertices.erase(vertices.begin() + cur + 1, vertices.end());
        vertices.shrink_to_fit();

        // Update vertex indices
        for (auto& face: faces) {
            face.vertex[0] = remap[face.vertex[0]];
            face.vertex[1] = remap[face.vertex[1]];
            face.vertex[2] = remap[face.vertex[2]];
        }
    }

    // STL format parsing helpers
    size_t skipWhitespace(std::istream& stream, size_t limit = -1) {
        size_t count = 0;

        while (std::isspace(stream.peek()) && count < limit) {
            stream.ignore();
            ++count;
        }

        return count;
    }

    struct STLFace {
        glm::vec3 normal;
        glm::vec3 vertex[3];
    };
} /* namespace */

Model::Error Model::loadTextSTL(std::istream& stream, bool useNormals, bool verified) {
    std::string tok;

    vertices.clear();
    faces.clear();
    bvh_.clear();

    stream >> std::skipws;

    if (!verified) {
        // Read and verify 'solid' signature
        stream >> tok;
        if (!stream) // If error or eof, we have a problem
            return FileTruncated;
        else if (tok != "solid")
            return UnexpectedToken;
    }

    // Read (and ignore) model name and comment line
    stream.ignore(std::numeric_limits<std::streamsize>::max(), stream.widen('\n'));
    if (!stream)
        return FileTruncated;

    stream >> tok;
    if (stream.fail())
        return FileTruncated;

    STLFace face = {};

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

        vertices.insert(vertices.end(), face.vertex, face.vertex+3);
        faces.push_back(Face{
            { vertices.size()-3, vertices.size()-2, vertices.size()-1 },
            face.normal
        });

        // Update bounding box
        if (first) {
            first = false;
            boundingBox_ = {
                glm::min(face.vertex[0], glm::min(face.vertex[1], face.vertex[2])),
                glm::max(face.vertex[0], glm::max(face.vertex[1], face.vertex[2]))
            };
        }

        boundingBox_ = {
            glm::min(boundingBox_.from, glm::min(face.vertex[0], glm::min(face.vertex[1], face.vertex[2]))),
            glm::max(boundingBox_.to,   glm::max(face.vertex[0], glm::max(face.vertex[1], face.vertex[2])))
        };

        // Read next face or end of model
        stream >> tok;
        if (stream.fail())
            return FileTruncated;
    }

    if (tok != "endsolid")
        return tok.empty() ? FileTruncated : UnexpectedToken;

    vertices.shrink_to_fit();
    faces.shrink_to_fit();
    deduplicateVertices(vertices, faces);

    return Ok;
}

Model::Error Model::loadBinarySTL(std::istream& stream, bool useNormals, size_t skipped) {
    // Reassign to free excess memory
    vertices = std::vector<glm::vec3>();
    faces = std::vector<Face>();
    bvh_.clear();

    // Skip header
    stream.ignore(80 - skipped);

    // Read size (32 bit unsigned integer)
    uint32_t size = 0;
    stream.read(reinterpret_cast<char*>(&size), sizeof(uint32_t));
    if (stream.gcount() < std::streamsize(sizeof(uint32_t)))
        return FileTruncated;

    vertices.reserve(3*size);
    faces.reserve(size);

    STLFace face = {};
    uint16_t attrs = 0;

    for (uint32_t i = 0; i < size; ++i) {
        stream.read(reinterpret_cast<char*>(&face), sizeof(STLFace));
        if (stream.gcount() < std::streamsize(sizeof(STLFace)))
            return FileTruncated;

        // Ignore attrs: they should be zero, some programs use them
        // as color values
        stream.read(reinterpret_cast<char*>(&attrs), sizeof(uint16_t));
        if (stream.gcount() < std::streamsize(sizeof(uint16_t)))
            return FileTruncated;

        // Recompute normal (some programs are known to write garbage)
        if (!useNormals)
            face.normal = glm::triangleNormal(face.vertex[0], face.vertex[1], face.vertex[2]);

        vertices.insert(vertices.end(), face.vertex, face.vertex+3);
        faces.push_back(Face{
            { vertices.size()-3, vertices.size()-2, vertices.size()-1 },
            face.normal
        });

        // Update bounding box
        if (i == 0)
            boundingBox_ = {
                glm::min(face.vertex[0], glm::min(face.vertex[1], face.vertex[2])),
                glm::max(face.vertex[0], glm::max(face.vertex[1], face.vertex[2]))
            };

        boundingBox_ = {
            glm::min(boundingBox_.from, glm::min(face.vertex[0], glm::min(face.vertex[1], face.vertex[2]))),
            glm::max(boundingBox_.to,   glm::max(face.vertex[0], glm::max(face.vertex[1], face.vertex[2])))
        };
    }

    deduplicateVertices(vertices, faces);

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

    return (mode == Text) ? loadTextSTL(stream, useNormals, skipped > 0)
                          : loadBinarySTL(stream, useNormals, skipped);
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
namespace {
    struct Ray {
        glm::vec3 origin;
        glm::vec3 direction;
        glm::vec3 invDirection;

        explicit Ray(glm::vec3 o, glm::vec3 d)
            : origin(o), direction(d), invDirection(1.0f/d)
            {}

        Ray(Ray const&) = default;
        Ray(Ray&&) = default;

        glm::vec3 operator()(float t) const {
            return origin + t*direction;
        }

        std::tuple<bool, float, float> intersectAABB(AABB const& aabb) const {
            auto from = (aabb.from - origin) * invDirection;
            auto to = (aabb.to - origin) * invDirection;

            auto tmin = glm::compMax(glm::min(from, to));
            auto tmax = glm::compMin(glm::max(from, to));

            return { tmax > glm::max(tmin, 0.0f), tmin, tmax };
        }

        std::tuple<bool, float, glm::vec2> intersectTriangle(Face const& face, std::vector<glm::vec3> const& vertices) {
            return {};
        }
    };

    std::tuple<bool, float, AABB> searchBVHNode(std::vector<BVHNode> const& bvh, BVHNode const& node,
                                                std::tuple<bool, float, float> const& nodeInt, Ray const& ray) {
        if (node.leaf)
            return { true, std::get<1>(nodeInt), node.bbox };

        auto int1 = ray.intersectAABB(bvh[node.left()].bbox);
        auto int2 = ray.intersectAABB(bvh[node.right()].bbox);

        if (!(std::get<0>(int1) && std::get<0>(int2))) {
            if (std::get<0>(int1))
                return searchBVHNode(bvh, bvh[node.left()], int1, ray);
            else if (std::get<0>(int2))
                return searchBVHNode(bvh, bvh[node.right()], int2, ray);
            else
                return { false, 0.0f, AABB{} };
        }

        bool order = std::get<1>(int1) <= std::get<1>(int2);
        auto const& firstNode = order ? bvh[node.left()] : bvh[node.right()];
        auto const& firstInt = order ? int1 : int2;
        auto const& secondNode = order ? bvh[node.right()] : bvh[node.left()];
        auto const& secondInt = order ? int2 : int1;

        auto res = searchBVHNode(bvh, firstNode, firstInt, ray);
        if (std::get<0>(res) && std::get<1>(res) <= std::get<1>(secondInt))
            return res;
        return searchBVHNode(bvh, secondNode, secondInt, ray);
    }

    std::tuple<bool, float, AABB> searchBVH(std::vector<BVHNode> const& bvh, Ray const& ray) {
        if (bvh.empty())
            return { false, 0.0f, AABB{} };

        auto rootInt = ray.intersectAABB(bvh.front().bbox);

        if (!std::get<0>(rootInt))
            return { false, 0.0f, AABB{} };

        return searchBVHNode(bvh, bvh.front(), rootInt, ray);
    }
} /* namespace */

size_t rendirt::render(Image<Color> const& color, Image<float> const& depth,
                       Model const& model, glm::mat4 const& modelViewProj,
                       Shader const& shader, CullingMode cullingMode)
{
    assert(color.width == depth.width && color.height == depth.height);

    using vec2s = glm::vec<2, size_t>;
    const vec2s imgSize(color.width, color.height);
    const glm::vec2 imgSizef(imgSize);

    const glm::vec2 sampleStep = glm::vec2(2.0f, -2.0f)/imgSizef;
    const glm::vec2 sampleOffset = sampleStep/2.0f;
    glm::vec2 sampleTileRow = glm::vec2(-1.0f, 1.0f) + sampleOffset;

    const glm::mat4 invMVP = glm::inverse(modelViewProj);

    constexpr unsigned int Near = 0, Far = 1;

    const glm::vec4 rayStart[2] = {
        invMVP*glm::vec4(sampleTileRow, 1.0f, 1.0f),
        invMVP*glm::vec4(sampleTileRow, -1.0f, 1.0f)
    };

    const glm::vec4 rayEnd[2] = {
        invMVP*glm::vec4(1.0f + sampleOffset.x, -1.0f + sampleOffset.y, 1.0f, 1.0f),
        invMVP*glm::vec4(1.0f + sampleOffset.x, -1.0f + sampleOffset.y, -1.0f, 1.0f)
    };

    glm::vec3 rayOrgTileRow = rayStart[Near]/rayStart[Near].w;
    glm::vec3 rayDirTileRow = glm::vec3(rayStart[Far]/rayStart[Far].w) - rayOrgTileRow;

    const glm::vec3 rayOrgEnd = rayEnd[Near]/rayEnd[Near].w;
    const glm::vec3 rayDirEnd = glm::vec3(rayEnd[Far]/rayEnd[Far].w) - rayOrgEnd;

    const glm::vec2 rayOrgStep = glm::vec2(rayOrgEnd - rayOrgTileRow)/imgSizef;
    const glm::vec2 rayDirStep = glm::vec2(rayDirEnd - rayDirTileRow)/imgSizef;

    if (model.bvh().empty())
        return 0;

    for (size_t ty = 0; ty < color.height; ty += 32) {
        glm::vec3 rayOrgTileCol = rayOrgTileRow, rayDirTileCol = rayDirTileRow;
        glm::vec2 sampleTileCol = sampleTileRow;

        for (size_t tx = 0; tx < color.width; tx += 32) {
            glm::vec3 rayOrgRow = rayOrgTileCol, rayDirRow = rayDirTileCol;
            glm::vec2 sampleRow = sampleTileCol;

            size_t yend = glm::min(ty + 32, color.height);
            size_t xend = glm::min(tx + 32, color.width);

            // Tile loop
            for (size_t y = ty; y < yend; ++y) {
                glm::vec3 rayOrg = rayOrgRow, rayDir = rayDirRow;
                glm::vec2 sample = sampleRow;

                for (size_t x = tx; x < xend; ++x) {
                    Ray ray(rayOrg, rayDir);
                    auto bvhInt = searchBVH(model.bvh(), ray);

                    color.buffer[y*color.stride + x] =
                        std::get<0>(bvhInt) ? Color(glm::vec3(std::get<1>(bvhInt)*255.0f), 255) : Color(0, 0, 0, 255);

                    rayOrg.x += rayOrgStep.x;
                    rayDir.x += rayDirStep.x;
                    sample.x += sampleStep.x;
                }

                rayOrgRow.y += rayOrgStep.y;
                rayDirRow.y += rayDirStep.y;
                sampleRow.y += sampleStep.y;
            }

            rayOrgTileCol.x += 32*rayOrgStep.x;
            rayDirTileCol.x += 32*rayDirStep.x;
            sampleTileCol.x += 32*sampleStep.x;
        }

        rayOrgTileRow.y += 32*rayOrgStep.y;
        rayDirTileRow.y += 32*rayDirStep.y;
        sampleTileRow.y += 32*sampleStep.y;
    }

    return 0;
}
