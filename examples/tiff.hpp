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

namespace tiff {
    inline std::ostream& writeFields(std::ostream& stream) {
        return stream;
    }

    template<typename T, typename... Next>
    inline std::ostream& writeFields(std::ostream& stream, T const& field, Next&&... next) {
        stream.write(reinterpret_cast<char const*>(&field), sizeof(field));
        return writeFields(stream, std::forward<Next>(next)...);
    }

    template<typename T, typename U>
    inline T left_aligned_cast(U const& value) {
        static constexpr size_t maxSize = (sizeof(U) > sizeof(T)) ? sizeof(U) : sizeof(T);
        std::uint8_t tmp[maxSize];
        *reinterpret_cast<U*>(tmp) = value;
        return *reinterpret_cast<T*>(tmp);
    }

    enum class ByteOrder : std::uint16_t {
        Auto = 0,
        LittleEndian = 0x4949,
        BigEndian = 0x4d4d,
    };

    struct Header {
        ByteOrder byteOrder;
        std::uint16_t signature = 42;
        std::uint32_t firstIFD = Size;

        static constexpr size_t Size = sizeof(byteOrder) + sizeof(signature) + sizeof(firstIFD);

        explicit Header(ByteOrder order = ByteOrder::Auto)
            : byteOrder(order)
        {
            if (order == ByteOrder::Auto) {
                std::uint16_t sig = 42;
                byteOrder = (reinterpret_cast<char*>(&sig)[0] == 42) ? ByteOrder::LittleEndian : ByteOrder::BigEndian;
            }
        }

        std::ostream& write(std::ostream& stream) {
            return writeFields(stream, byteOrder, signature, firstIFD);
        };
    };

    struct IFDEntry {
        enum Type : std::uint16_t {
            Byte = 1,
            Ascii = 2,
            Short = 3,
            Long = 4,
            Rational = 5,
            SByte = 6,
            Undefined = 7,
            SShort = 8,
            SLong = 9,
            SRational = 10,
            Float = 11,
            Double = 12
        };

        std::uint16_t tag;
        Type type;
        std::uint32_t count;
        std::uint32_t offset;

        static constexpr size_t Size = sizeof(tag) + sizeof(type) +
                                       sizeof(count) + sizeof(offset);

        std::ostream& write(std::ostream& stream) {
            return writeFields(stream, tag, type, count, offset);
        }
    };

    template<std::uint16_t TTag, IFDEntry::Type TType, size_t TValueSize = 0>
    struct Field : IFDEntry {
        static constexpr std::uint16_t Tag = TTag;
        static constexpr IFDEntry::Type Type = TType;
        static constexpr size_t ValueSize = TValueSize;

        template<typename... Args>
        explicit Field(std::uint32_t count, std::uint32_t offset)
            : IFDEntry{ Tag, Type, count, offset }
            {}
    };

    struct NewSubfileType : Field<254, IFDEntry::Long> {
        explicit NewSubfileType(bool scaled = false, bool page = false, bool mask = false)
            : Field(1, std::uint32_t(scaled) | (std::uint32_t(page) << 1) | (std::uint32_t(mask) << 2))
            {}
    };

    struct ImageWidth : Field<256, IFDEntry::Long> {
        explicit ImageWidth(std::uint32_t value) : Field(1, value) {}
    };

    struct ImageLength : Field<257, IFDEntry::Long> {
        explicit ImageLength(std::uint32_t value) : Field(1, value) {}
    };

    template<size_t TCount>
    struct BitsPerSample : Field<258, IFDEntry::Short, TCount*sizeof(std::uint16_t)> {
        using F = Field<258, IFDEntry::Short, TCount*sizeof(std::uint16_t)>;

        static constexpr size_t Count = TCount;

        explicit BitsPerSample(std::uint32_t offset) : F(Count, offset) {}
    };

    template<>
    struct BitsPerSample<1> : Field<258, IFDEntry::Short> {
        static constexpr size_t Count = 1;

        explicit BitsPerSample(std::uint16_t bits)
            : Field(1, left_aligned_cast<std::uint32_t>(bits))
            {}
    };

    template<>
    struct BitsPerSample<2> : Field<258, IFDEntry::Short> {
        static constexpr size_t Count = 2;

        explicit BitsPerSample(std::uint16_t bits1, std::uint16_t bits2) : Field(1, 0) {
            char value[sizeof(std::uint16_t[2])];
            *reinterpret_cast<std::uint16_t*>(value) = bits1;
            *reinterpret_cast<std::uint16_t*>(value + sizeof(std::uint16_t)) = bits2;
            offset = *reinterpret_cast<std::uint32_t*>(value);
        }
    };

    struct Compression : Field<259, IFDEntry::Short> {
        enum Mode : std::uint16_t {
            None = 1,
            CCITT = 2,
            PackBits = 32773
        };

        explicit Compression(Mode mode)
            : Field(1, left_aligned_cast<std::uint32_t>(mode))
            {}
    };

    struct PhotometricInterpretation : Field<262, IFDEntry::Short> {
        enum Interpretation : std::uint16_t {
            WhiteIsZero = 0,
            BlackIsZero = 1,
            RGB = 2,
            Palette = 3,
            TransparencyMask = 4
        };

        explicit PhotometricInterpretation(Interpretation interpretation)
            : Field(1, left_aligned_cast<std::uint32_t>(interpretation))
            {}
    };

    template<size_t TCount>
    struct StripOffsets : Field<273, IFDEntry::Long, TCount*sizeof(std::uint32_t)> {
        using F = Field<273, IFDEntry::Long, TCount*sizeof(std::uint32_t)>;

        static constexpr size_t Count = TCount;

        explicit StripOffsets(std::uint32_t offset) : F(Count, offset) {}
    };

    template<>
    struct StripOffsets<1> : Field<273, IFDEntry::Long> {
        static constexpr size_t Count = 1;

        explicit StripOffsets(std::uint32_t offset) : Field(Count, offset) {}
    };

    struct SamplesPerPixel : Field<277, IFDEntry::Short> {
        explicit SamplesPerPixel(std::uint16_t value)
            : Field(1, left_aligned_cast<std::uint32_t>(value))
            {}
    };

    struct RowsPerStrip : Field<278, IFDEntry::Long> {
        explicit RowsPerStrip(std::uint32_t value)
            : Field(1, value)
            {}
    };

    template<size_t TCount>
    struct StripByteCounts : Field<279, IFDEntry::Long, TCount*sizeof(std::uint32_t)> {
        using F = Field<279, IFDEntry::Long, TCount*sizeof(std::uint32_t)>;

        static constexpr size_t Count = TCount;

        explicit StripByteCounts(std::uint32_t offset) : F(Count, offset) {}
    };

    template<>
    struct StripByteCounts<1> : Field<279, IFDEntry::Long> {
        static constexpr size_t Count = 1;

        explicit StripByteCounts(std::uint32_t byteCount) : Field(Count, byteCount) {}
    };

    struct XResolution : Field<282, IFDEntry::Rational, sizeof(std::uint32_t[2])> {
        explicit XResolution(std::uint32_t offset) : Field(1, offset) {}
    };

    struct YResolution : Field<283, IFDEntry::Rational, sizeof(std::uint32_t[2])> {
        explicit YResolution(std::uint32_t offset) : Field(1, offset) {}
    };

    struct ResolutionUnit : Field<296, IFDEntry::Short> {
        enum Unit : std::uint16_t {
            None = 1,
            Inch = 2,
            Centimeter = 3
        };

        explicit ResolutionUnit(Unit unit)
            : Field(1, left_aligned_cast<std::uint32_t>(unit))
            {}
    };

    enum SampleInterpretation : std::uint16_t {
        Unspecified = 0,
        AssociatedAlpha = 1,
        UnassociatedAlpha = 2
    };

    template<size_t TCount>
    struct ExtraSamples : Field<338, IFDEntry::Short, TCount*sizeof(SampleInterpretation)> {
        using F = Field<338, IFDEntry::Short, TCount*sizeof(SampleInterpretation)>;

        static constexpr size_t Count = TCount;

        explicit ExtraSamples(std::uint32_t offset) : F(Count, offset) {}
    };

    template<>
    struct ExtraSamples<1> : Field<338, IFDEntry::Short> {
        static constexpr size_t Count = 1;

        explicit ExtraSamples(SampleInterpretation interpretation)
            : Field(Count, left_aligned_cast<std::uint32_t>(interpretation))
            {}
    };

    template<>
    struct ExtraSamples<2> : Field<338, IFDEntry::Short> {
        static constexpr size_t Count = 2;

        explicit ExtraSamples(SampleInterpretation intp1, SampleInterpretation intp2)
            : Field(Count, 0)
        {
            char value[sizeof(SampleInterpretation[2])];
            *reinterpret_cast<SampleInterpretation*>(value) = intp1;
            *reinterpret_cast<SampleInterpretation*>(value + sizeof(SampleInterpretation)) = intp2;
            offset = *reinterpret_cast<std::uint32_t*>(value);
        }
    };

    template<typename = void>
    std::ostream& writeTIFF(std::ostream& stream, rendirt::Image<rendirt::Color> const& img) {
        std::uint16_t fieldCount = 14;
        std::uint32_t valueOffset =
            Header::Size + sizeof(fieldCount) + fieldCount*IFDEntry::Size + sizeof(std::uint32_t);
        size_t dataOffset = valueOffset + BitsPerSample<4>::ValueSize + XResolution::ValueSize + YResolution::ValueSize;

        Header().write(stream);

        // IFD size
        writeFields(stream, fieldCount);

        // Fields
        NewSubfileType().write(stream);
        ImageWidth(img.width).write(stream);
        ImageLength(img.height).write(stream);
        BitsPerSample<4>(valueOffset).write(stream);
        Compression(Compression::None).write(stream);
        PhotometricInterpretation(PhotometricInterpretation::RGB).write(stream);
        StripOffsets<1>(dataOffset).write(stream);
        SamplesPerPixel(4).write(stream);
        RowsPerStrip(img.height).write(stream);
        StripByteCounts<1>(img.width*img.height*sizeof(rendirt::Color)).write(stream);
        XResolution(valueOffset + BitsPerSample<4>::ValueSize).write(stream);
        YResolution(valueOffset + BitsPerSample<4>::ValueSize + XResolution::ValueSize).write(stream);
        ResolutionUnit(ResolutionUnit::Inch).write(stream);
        ExtraSamples<1>(SampleInterpretation::AssociatedAlpha).write(stream);

        // Next IFD pointer (= 0, end)
        writeFields(stream, std::uint32_t(0));

        // BitsPerSample (array of four shorts)
        writeFields(stream, std::uint16_t(8), std::uint16_t(8), std::uint16_t(8), std::uint16_t(8));

        // XResolution, YResolution (two rational values): 300dpi
        writeFields(stream, std::uint32_t(300), std::uint32_t(1),
                            std::uint32_t(300), std::uint32_t(1));

        for (size_t line = 0, end = img.height*img.stride; line < end; line += img.stride)
            stream.write(reinterpret_cast<char const*>(img.buffer + line), img.width*sizeof(rendirt::Color));

        return stream;
    }
} /* namespace tiff */
