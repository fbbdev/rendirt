#pragma once

#include "rendirt.hpp"

#include <cstdint>
#include <fstream>
#include <string>

namespace debug {
    void writeTriangle(std::ostream& stream, glm::vec3 const& p1, glm::vec3 const& p2, glm::vec3 const& p3) {
        glm::vec3 normal = glm::cross(p2 - p1, p3 - p1);
        stream.write(reinterpret_cast<char const*>(&normal), sizeof(normal));
        stream.write(reinterpret_cast<char const*>(&p1), sizeof(p1));
        stream.write(reinterpret_cast<char const*>(&p2), sizeof(p2));
        stream.write(reinterpret_cast<char const*>(&p3), sizeof(p3));

        std::uint16_t attr = 0;
        stream.write(reinterpret_cast<char const*>(&attr), sizeof(attr));
    }

    void exportBVH(std::string const& name, std::vector<rd::BVHNode> const& bvh) {
        std::ofstream file(name, std::ofstream::binary);

        file.write("bvh                                                                             ", 80);

        std::uint32_t count = std::uint32_t(bvh.size()) * 12;
        file.write(reinterpret_cast<char const*>(&count), sizeof(count));

        for (auto const& node: bvh) {
            glm::vec3 p1 = node.bbox.from, p2 = node.bbox.to;
            writeTriangle(file, { p2.x, p2.y, p1.z }, { p2.x, p1.y, p1.z }, p1);
            writeTriangle(file, p1, { p1.x, p2.y, p1.z }, { p2.x, p2.y, p1.z });

            writeTriangle(file, p2, { p2.x, p1.y, p2.z }, { p2.x, p1.y, p1.z });
            writeTriangle(file, { p2.x, p1.y, p1.z }, { p2.x, p2.y, p1.z }, p2);

            writeTriangle(file, { p1.x, p2.y, p2.z }, { p1.x, p1.y, p2.z }, { p2.x, p1.y, p2.z });
            writeTriangle(file, { p2.x, p1.y, p2.z }, p2, { p1.x, p2.y, p2.z });

            writeTriangle(file, { p1.x, p2.y, p1.z }, p1, { p1.x, p1.y, p2.z });
            writeTriangle(file, { p1.x, p1.y, p2.z }, { p1.x, p2.y, p2.z }, { p1.x, p2.y, p1.z });

            writeTriangle(file, { p2.x, p1.y, p2.z }, { p1.x, p1.y, p2.z }, p1);
            writeTriangle(file, p1, { p2.x, p1.y, p1.z }, { p2.x, p1.y, p2.z });

            writeTriangle(file, { p1.x, p2.y, p1.z }, { p1.x, p2.y, p2.z }, p2);
            writeTriangle(file, p2, { p2.x, p2.y, p1.z }, { p1.x, p2.y, p1.z });
        }
    }
} /* namespace debug */
