# rendirt

*rendirt* is a bare-bones C++ software rendering library for triangle meshes.
The library is able to load STL models both in binary and ASCII format.
In fact, offline rendering of STL model thumbnails is its primary use case.
From this point of view, *rendirt* means *render it*.

**But beware!** *rendirt* also stands for *dirty renderer*. This thing is as
simple as possible, quite inflexible and mostly unoptimized. Clocking in
at ~400 LOCs, it does its (very limited) work in reasonable time and
that's all. This is not meant to be an example of state-of-the-art graphics
programming. Decent speed is only achieved with compiler optimizations enabled. Still, the debug build manages to render ~400k tris at 800x600 px in less than
1 second and simpler models in less than 100 milliseconds. It becomes one order
of magnitude faster when compiler-optimized. *(DISCLAIMER: those are not
accurate measures, just average execution times to give an idea)*.

The renderer implements a fixed-pipeline vertex processor with optional
face culling (enabled by default) and a programmable fragment shader.
The fragment shader function has access to fragment position and depth,
interpolated object-space position and normal. Some predefined shaders are
provided.

The awesome [glm](https://glm.g-truc.net/0.9.9/index.html) library is used
extensively. Headers for its 0.9.9.2 version are included in the repository,
but any other recent version will do. The license can be found [below](#glm).

## Contents

  1. [Building](#building)
  2. [It works!](#it-works)
  3. [Show me the code](#show-me-the-code)
  4. [A picture is worth a thousand words](#a-picture-is-worth-a-thousand-words)
  5. [API Reference](#api-reference)
  6. [License](#license)

# Building

*rendirt* uses the meson build system for peace of mind. This is not strictly
necessary since the project consists of [a single header](rendirt.hpp)
(`rendirt.hpp`) and a [single source file](rendirt.cpp) (`rendirt.cpp`) which
can be compiled directly by any C++11 conformant compiler, provided that *glm*
is available in the include path.

To build the *rendirt* static library and examples:
```sh
$ meson build && ninja -C build
```

To use the release configuration (optimized binaries):
```sh
$ meson build --buildtype=release && ninja -C build
```

If you can't use *meson* or prefer not to, compiling and linking each *cpp*
file from the [examples folder](examples) together with `rendirt.cpp` will
do the trick.

# It works!

The `render` example will load the given STL model and save the rendered image
as `render.tiff` in the current directory:
```sh
$ build/examples/render path/to/file.stl
```

The `animation` example requires SDL2. It will load the given model and
display an animated view. Various parameters can be tweaked by pressing keys,
see command output for instructions. Decent frame rates can be achieved only
with the release build (with optimization enabled).
```sh
$ build/examples/animation path/to/file.stl
```

Interesting test models can be downloaded
[here](http://people.sc.fsu.edu/~jburkardt/data/stla/stla.html). They're not
included in this repository because of size and licensing.

# Show me the code

Here it comes! This is the bare minimum to obtain an image. For more details
and variations look into the [examples](examples) folder.

```c++
namespace rd = rendirt;

std::ifstream file("/path/to/file.stl");
if (!file) {
    // Error handling
}

rd::Model model;
rd::Model::Error err = model.loadSTL(file);
if (err != rd::Model::Ok) {
    // Error handling
}

file.close();

std::vector<rd::Color> colorBuffer(800*600);
std::vector<float> depthBuffer(800*600);
Image<rd::Color> image(colorBuffer.data(), 800, 600);
Image<float> depth(depthBuffer.data(), 800, 600);

image.clear(Color(0, 0, 0, 255));
depth.clear(1.0f); // Important!

rd::Camera view(
    { 0.0f, 0.0f, 5.0f },
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f });

rd::Projection proj(
    rd::Projection::Perspective,
    glm::half_pi<float>(),
    width, height,
    0.1f, 100.0f);

size_t faceCount = rd::render(
    image, depth, model, proj * view,
    rd::shaders::position(model.boundingBox()));

// Do something with the image
```

# A picture is worth a thousand words

Four pictures should add up to a whopping four thousand words then.
Sample renders, one for each predefined shader, are included in folder
[examples/images](examples/images). Here they are:

### Depth shader

![Image rendered with depth shader](examples/images/depth.png)

### Position shader

![Image rendered with position shader](examples/images/position.png)

### Normal shader

![Image rendered with normal shader](examples/images/normal.png)

### Diffuse directional lighting shader

![Image rendered with diffuse directional shader](examples/images/diffuseDirectional.png)

# API Reference

To use the library, make sure that *glm* is available in the include path, then
include `rendirt.hpp` and link with `rendirt.cpp` or with the static library
produced by the build system. The API is contained within the `rendirt`
namespace.

  - [`rendirt::render()`](#rendirtrender)
  - [`enum rendirt::CullingMode`](#enum-rendirtcullingmode)
  - [`struct rendirt::Image<T>`](#struct-rendirtimaget)
  - [`using rendirt::Shader`](#using-rendirtshader)
  - [`class rendirt::Model`](#class-rendirtmodel)
  - [`struct rendirt::Face`](#struct-rendirtface)
  - [`struct rendirt::AABB`](#struct-rendirtaabb)
  - [`using rendirt::Color`](#using-rendirtcolor)
  - [Utilities](#utilities)
  - [Shaders](#shaders)

## `rendirt::render()`

The main entry point is the `rendirt::render` function. It takes in all
necessary resources and parameters and fills the `color` buffer with the
rendered image. On return, the `depth` buffer is filled with fragment depth
data, which may be useful for rendering other models to the same image or for
post-processing.

```c++
size_t render(Image<Color> const& color, Image<float> const& depth,
              Model const& model, glm::mat4 const& modelViewProj,
              Shader const& shader, CullingMode cullingMode = CullCW);
```

### Arguments

  - `color`: a valid buffer of type [`Image<Color>`](#struct-rendirtimaget)
    that will be filled with image data.
  - `depth`: a valid buffer of type [`Image<float>`](#struct-rendirtimaget)
    that will be used for depth testing. This buffer *must* have the same width
    and height as the `color` one: debug builds use `assert` to ensure this
    condition holds; release builds just assume this is the case. When doing a
    clean render, this buffer must be reset to a value of `1.0f` (e.g. by
    calling `depth.clear(1.0f)`).
  - `model`: a [`Model`](#class-rendirtmodel) instance containing mesh data to
    be rendered.
  - `modelViewProj`: a 4x4 matrix to be used for vertex processing. It should
    be the product, in order, of the projection matrix, the view matrix, and
    the model matrix when applicable.
  - `shader`: the fragment shader function (see documentation for the
    [`Shader`](#using-rendirtshader) type).
  - `cullingMode`: a value from the [`CullingMode`](#enum-rendirtcullingmode)
    enum that specifies whether face culling should be performed, and how. The
    default value is `CullCW`.

### Return value

The number of triangles actually rendered (i.e. not culled or clipped).

## `enum rendirt::CullingMode`

Values of the `CullingMode` enum specify whether and how face culling is to
be performed.

```c++
enum CullingMode {
    CullNone,
    CullCW,
    CullCCW,
    CullBack = CullCW,
    CullFront = CullCCW
};
```

### Values

  - `CullNone`: do not perform face culling.
  - `CullCW`: cull triangles with clockwise
    [winding order](https://www.khronos.org/opengl/wiki/Face_Culling#Winding_order).
  - `CullCCW`: cull triangles with counter-clockwise winding order.
  - `CullBack`: an alias for `CullCW` (the most common approach is to consider
    triangles with CW winding back-facing).
  - `CullFront`: an alias for `CullCCW` (following the same reasoning).

## `struct rendirt::Image<T>`

`Image<T>` instances represent weak references to rectangular buffers of
elements of type `T`, specified by a pointer to the first element (`buffer`),
width and height in elements, and stride (number of elements from one row to
the next). The idea is that the element with coordinates `(x, y)` can be
accessed by an expression like this: `buffer[y*stride + x]`.

```c++
template<typename T>
struct Image {
    explicit constexpr Image(T* buffer, size_t width, size_t height);
    explicit constexpr Image(T* buffer, size_t width, size_t height, size_t stride);

    void clear(T value);

    T* buffer;
    size_t width;
    size_t height;
    size_t stride;
};
```

### Fields

  - `buffer`: pointer to the first element of the buffer.
  - `width`: size, in elements, of a single row.
  - `height`: total number of rows.
  - `stride`: distance, in elements, from the first element of any row to the
    first element of the next

### Constructors

```c++
explicit constexpr Image(T* buffer, size_t width, size_t height);
explicit constexpr Image(T* buffer, size_t width, size_t height, size_t stride);
```

The two constructors simply assign their arguments to fields of the same name.
When stride is omitted (first overload) it is made equal to width.

### Methods

```c++
void clear(T value);
```

Fills the buffer with the specified value.

**Arguments:**

  - `value`: any value of type T.

## `using rendirt::Shader`

The `Shader` type is an alias for a `std::function` type capable of holding
fragment shader functions. A shader function (or functor) takes as arguments
the fragment position in clip space (including depth), the interpolated
position in object space and the face normal, and uses this input (and
possibly other data) to compute the fragment color.

```c++
using Shader = std::function<Color(glm::vec3 frag, glm::vec3 pos, glm::vec3 normal)>;
```

### Arguments

  - `frag`: a 3-float vector equal to the coordinates of the current fragment
    in clip space. The third component is the depth value.
  - `pos`: a 3-float vector equal to the interpolated position of the fragment
    on the triangle, in object coordinates.
  - `normal`: a 3-float vector equal to the normal of the triangle to which
    the current fragment belongs.

### Return value

The [`Color`](#using-rendirtcolor) of the fragment as computed by the shader.

## `class rendirt::Model`

The `Model` class is a thin wrapper around `std::vector<Face>` representing
a triangle mesh as a list of [`Face`](#struct-rendirtface)s. Additional
methods for computing the bounding box and loading STL files are provided.

```c++
class Model : public std::vector<Face> {
public:
    enum Error;
    enum Mode;

    using std::vector<Face>::vector;

    AABB const& boundingBox() const;
    glm::vec3 center() const;

    void updateBoundingBox();

    Error loadSTL(std::istream& stream, Mode mode = Guess);
    Error loadSTL(std::istream& stream, bool useNormals, Mode mode = Guess);

    static char const* errorString(Error err);
};
```

### Types

```c++
enum Model::Error {
    Ok,
    InvalidToken,
    UnexpectedToken,
    FileTruncated,
    GuessFailed
};
```

The `Model::Error` enum is used for error reporting by the STL loader.
Possible values are:

  - `Model::Ok`: model loaded successfully.
  - `Model::InvalidToken`: the ASCII STL parser could not parse a number.
  - `Model::UnexpectedToken`: the ASCII STL parser found a wrong token where
    another one was expected.
  - `Model::FileTruncated`: input data (either binary or ASCII) was shorter than
    expected.
  - `Model::GuessFailed`: the STL loader could not determine the input format
    reliably.

```c++
enum Model::Mode {
    Guess,
    Text,
    Binary
};
```

Values of the `Model::Mode` enum specify the format of input data for the STL
loader, or ask the loader to detect it automatically. Possible values are:

  - `Model::Guess`: detect the format automatically. 
  - `Model::Text`: input data is in ASCII format.
  - `Model::Binary`: input data is in binary format.

### Static members

```c++
char const* Model::errorString(Error err);
```

Takes an error code from the `Model::Error` enum and returns a static-allocated
NULL-terminated string containing a brief description of the error.

**Arguments:**

  - `err`: any value from the `Model::Error` enum.

### Constructors

```c++
using std::vector<Face>::vector;
```

All `std::vector<Face>` constructors are inherited with public access.

### Methods

```c++
AABB const& Model::boundingBox() const;
```

Returns a cached [`AABB`](#struct-rendirtaabb) instance whose value
represents the bounding box of the mesh (if the cache is up to date,
see `updateBoundingBox`).

```c++
glm::vec3 Model::center() const;
```

Returns a 3-float vector equal to the centroid of the cached bounding box.

```c++
void updateBoundingBox();
```

Recomputes the bounding box from mesh data and updates the cached value.

```c++
Error loadSTL(std::istream& stream, Mode mode = Guess);
Error loadSTL(std::istream& stream, bool useNormals, Mode mode = Guess);
```

Empties the model, reads data from an input stream and parses it according to
the specified `Mode`. Returns `Model::Ok` on success and one of the error codes
from enum `Model::Error` on failure. On success, the object contains a list
of facets loaded from the STL file and the cached bounding box is up to date.

**Arguments:**

  - `stream`: an input stream from which data should be read.
  - `useNormals` (second overload): since some programs are known to write
    garbage in the normal fields of STL files, the loader recomputes all
    normals assuming that all triangles have counter-clockwise winding
    (right-hand rule: `n = normalize((v1 - v0) x (v2 - v0))`). Setting this
    parameter to `true` disables that behavior and keeps normals as they are
    in STL data.
  - `mode`: a value from the `Model::Mode` enum specifying the input format or
    asking the loader to infer it automatically. The default value is
    `Model::Guess` (autodetect). The detection algorithm reads at most 80 bytes
    from the stream (size of the binary format header), skips whitespace and
    looks for the `"solid"` token. If the token is found, the input is read in
    ASCII mode, otherwise it is read in binary mode. If it is not possible to
    exclude the presence of the solid token (e.g. because the header is all
    whitespace, or 77 whites followed by `"sol"` etc.) the loader fails with
    error `Model::GuessFailed`. In this case, it is guaranteed that exactly 80
    bytes have been consumed from the stream.

## `struct rendirt::Face`

`Face` instances represent a triangle by specifing its normal vector and three
vertices. The structure is modeled on the STL format's facet specification.

```c++
struct Face {
    glm::vec3 normal;
    glm::vec3 vertex[3];
};
```

### Fields

  - `normal`: a 3-float vector equal to the normal of the triangle.
  - `vertex`: an array of three 3-float vectors, one for each vertex of the
    triangle.

## `struct rendirt::AABB`

`AABB` instances represent axis-aligned bounding boxes specified by their two
extreme corners. **WARNING:** most functions in *rendirt* assume each component
of the `from` vector is less than or equal to the corresponding component of
the `to` vector.

```c++
struct AABB {
    glm::vec3 from;
    glm::vec3 to;
};
```

### Fields

  - `from`: a 3-float vector equal to the minimal corner of the bounding box.
  - `to`: a 3-float vector equal to the maximal corner of the bounding box.

## `using rendirt::Color`

The `Color` type is an alias for a *glm* vector of four bytes, capable of
representing a color in RGBA32 format.

```c++
using Color = glm::vec<4, uint8_t>;
```

## Utilities

### `struct rendirt::Projection`

```c++
struct Projection : glm::mat4 {
    using glm::mat4::mat;

    static constexpr struct FrustumTag {} Frustum = {};
    static constexpr struct PerspectiveTag {} Perspective = {};
    static constexpr struct OrthographicTag {} Orthographic = {};

    explicit Projection(FrustumTag, float left, float right, float bottom,
                        float top, float near, float far);

    explicit Projection(PerspectiveTag, float fov, float width, float height,
                        float near, float far);

    explicit Projection(OrthographicTag, float left, float right, float bottom,
                        float top, float zNear, float zFar);
};
```

The `Projection` struct is a wrapper around `glm::mat4` (4x4 float matrix)
providing some additional constructors that can be used to create various
kinds of projection matrix.

Please not that in all three cases depth buffer precision is affected by the values specified for near and far. The greater the ratio of far to near is, the
less effective the depth buffer will be at distinguishing between surfaces that
are near each other.

#### Constructors

```c++
using glm::mat4::mat;
```

All `glm::mat4` constructors are inherited.

```c++
explicit Projection(FrustumTag, float left, float right, float bottom,
                    float top, float near, float far);
```

Creates a transformation matrix that produces a perspective projection. The
overload is selected by passing `Projection::Frustum` as first argument.

**Arguments:**

  - `FrustumTag`: pass `Projection::Frustum` to select this overload.
  - `left`: position of the left vertical clipping plane.
  - `right`: position of the right vertical clipping plane.
  - `bottom`: position of the bottom horizontal clipping plane.
  - `top`: position of the top horizontal clipping plane.
  - `near`: distance to the near depth clipping plane. Must be positive.
  - `far`: distance to the far depth clipping plane. Must be positive.

```c++
explicit Projection(PerspectiveTag, float fov, float width, float height,
                    float near, float far);
```

Creates a transformation matrix that produces a perspective projection. The
overload is selected by passing `Projection::Perspective` as first argument.

**Arguments:**

  - `PerspectiveTag`: pass `Projection::Perspective` to select this overload.
  - `fov`: field of view angle, in radians, in the horizontal direction.
  - `width`: width of the viewport. The unit is not important as far as the
    aspect ratio (width to height) is preserved.
  - `height`: height of the viewport.
  - `near`: distance to the near depth clipping plane.
  - `far`: distance to the far depth clipping plane.

```c++
explicit Projection(OrthographicTag, float left, float right, float bottom,
                    float top, float near, float far);
```

Creates a transformation matrix that produces a parallel (orthographic)
projection. The overload is selected by passing `Projection::Orthographic` as
first argument.

**Arguments:**

  - `OrthographicTag`: pass `Projection::Orthographic` to select this overload.
  - `left`: position of the left vertical clipping plane.
  - `right`: position of the right vertical clipping plane.
  - `bottom`: position of the bottom horizontal clipping plane.
  - `top`: position of the top horizontal clipping plane.
  - `near`: distance to the near depth clipping plane.
  - `far`: distance to the far depth clipping plane.

### `struct rendirt::Camera`

```c++
struct Camera : glm::mat4 {
    using glm::mat4::mat;

    explicit Camera(glm::vec3 const& eye,
                    glm::vec3 const& center,
                    glm::vec3 const& up);
};
```

The `Camera` struct is a wrapper around `glm::mat4` (4x4 float matrix) that
provides an additional constructor for easy creation of a viewing matrix.

#### Constructors

```c++
using glm::mat4::mat;
```

All `glm::mat4` constructors are inherited.

```c++
explicit Camera(glm::vec3 const& eye,
                glm::vec3 const& center,
                glm::vec3 const& up);
```

Creates a viewing matrix derived from an eye point, a reference point
indicating the center of the scene, and an *up* vector. The matrix maps the
reference point to the negative z axis and the eye point to the origin. When a
typical projection matrix is used, the center of the scene therefore maps to
the center of the viewport. Similarly, the direction described by the *up*
vector projected onto the viewing plane is mapped to the positive y axis so
that it points upward in the viewport. The *up* vector must not be parallel to
the line of sight from the eye point to the reference point.

**Arguments:**

  - `eye`: specifies the position of the eye point.
  - `center`: specifies the position of a reference point indicating the center
    of the scene.
  - `up`: specifies the direction of the *up* vector.

## Shaders

Some predefined shaders are available under the `rendirt::shaders` namespace.

### `rendirt::shaders::depth`

```c++
Shader depth;
```

Scales the depth value of the fragment from range [-1,1] to range [0,1] and
colors the fragment according to the rule:
`color = (255*depth, 255*depth, 255*depth, 255)`.

### `rendirt::shaders::position()`

```c++
Shader position(AABB bbox);
```

Generates a shader that scales the interpolated position to make it go from
(0, 0, 0) at bbox.from up to (1, 1, 1) at bbox.to. The fragment is then colored
as follows: `color = (255*pos.x, 255*pos.y, 255*pos.z, 255)`.

### `rendirt::shaders::normal`

```c++
Shader normal;
```

Expects the face normal to be correctly normalized. Colors the fragment
according to the rule:
`color = (255*normal.x, 255*normal.y, 255*normal.z, 255)`.

### `rendirt::shaders::diffuseDirectional()`

```c++
Shader diffuseDirectional(glm::vec3 dir, Color ambient, Color diffuse);
```

Generates a shader that computes diffuse lighting with a directional light
pointing in the direction specified by `dir`. Color is computed according
to the equation:

```
color = clamp(ambient + max(0, dot(-normalize(dir), normal))*diffuse, 0, 255);
```

# License

*rendirt* is distributed under the MIT license.

Copyright (c) 2018 Fabio Massaioli

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## glm

*glm* files are distributed under the MIT license.

Copyright (c) 2005 - G-Truc Creation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
