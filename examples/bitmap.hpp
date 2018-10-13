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

#pragma once

#include <cstdint>
#include <ostream>

#include "rendirt.hpp"

namespace bmp {
    struct FileHeader {
        char bfType[2] = { 'B', 'M' };
        std::uint32_t bfSize;
        std::uint16_t bfReserved1 = 0;
        std::uint16_t bfReserved2 = 0;
        std::uint32_t bfOffBits;

        std::ostream& write(std::ostream& stream) {
            stream.write(bfType, 2);
            stream.write(reinterpret_cast<char const*>(&bfSize), sizeof(bfSize));
            stream.write(reinterpret_cast<char const*>(&bfReserved1), sizeof(bfReserved1));
            stream.write(reinterpret_cast<char const*>(&bfReserved2), sizeof(bfReserved2));
            stream.write(reinterpret_cast<char const*>(&bfOffBits), sizeof(bfOffBits));
            return stream;
        }
    };

    struct CoreHeader {
        std::uint32_t bcSize = sizeof(CoreHeader);
        std::uint16_t bcWidth = 0;
        std::uint16_t bcHeight = 0;
        std::uint16_t bcPlanes = 1;
        std::uint16_t bcBitCount = 24;

        std::ostream& write(std::ostream& stream) {
            stream.write(reinterpret_cast<char const*>(&bcSize), sizeof(bcSize));
            stream.write(reinterpret_cast<char const*>(&bcWidth), sizeof(bcWidth));
            stream.write(reinterpret_cast<char const*>(&bcHeight), sizeof(bcHeight));
            stream.write(reinterpret_cast<char const*>(&bcPlanes), sizeof(bcPlanes));
            stream.write(reinterpret_cast<char const*>(&bcBitCount), sizeof(bcBitCount));
            return stream;
        }
    };

    std::ostream& writeBitmap(std::ostream& stream, rendirt::Image<rendirt::Color> const& img) {
        FileHeader fhdr;
        CoreHeader chdr;

        chdr.bcWidth = img.width;
        chdr.bcHeight = img.height;
        chdr.bcBitCount = 24;

        fhdr.bfOffBits = sizeof(FileHeader) + chdr.bcSize;
        fhdr.bfSize = fhdr.bfOffBits + (chdr.bcWidth*chdr.bcHeight*(chdr.bcBitCount >> 3));

        if (!fhdr.write(stream))
            return stream;

        if (!chdr.write(stream))
            return stream;

        for (intptr_t i = (img.height - 1)*img.stride; i >= 0; i -= img.stride + img.width)
            for (intptr_t end = i + img.width; i < end; ++i)
                stream.write(reinterpret_cast<char*>(img.buffer + i), (chdr.bcBitCount >> 3));

        return stream;
    }
} /* namespace bmp */
