#ifndef GAMING_GAME_MATH_H
#define GAMING_GAME_MATH_H

#define Kilobytes(n) ((n) * 1024LL)
#define Megabytes(n) ((n) * Kilobytes(1024))

#define Clamp(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// Math runtime

#define PI 3.141592653589793238462643383279502884f

static float Sqrt(const float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }

    float r = x * 0.5f;
    for (int i = 0; i < 8; ++i) {
        r = 0.5f * (r + x / r);
    }

    return r;
}

static float Sin(float x) {
    // reduce to [-pi, pi]
    const float pi2 = PI * 2.0f;
    x = x - (float) (int) (x / pi2) * pi2;

    if (x > PI) {
        x -= pi2;
    }

    if (x < -PI) {
        x += pi2;
    }

    // x - x^3 / 6 + x^5 / 120 - x^7 / 5040 + x^9/362880
    const float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f - x2 / 362880.0f))));
}

static float Cos(const float x) {
    return Sin(x + PI * 0.5f);
}

static float Tan(const float x) {
    const float c = Cos(x);

    return Sin(x) / c;
}

static float Lerp(const float a, const float b, const float t) {
    return a + t * (b - a);
}

// NOTE: see https://github.com/libsdl-org/SDL_ttf/blob/main/examples/testgputext/SDL_math3d.h

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

static Vec3 Vector3(const float x, const float y, const float z) {
    return (Vec3){.x = x, .y = y, .z = z};
}

static Vec3 Vec3_Sub(const Vec3 vec1, const Vec3 vec2) {
    return Vector3(vec1.x - vec2.x, vec1.y - vec2.y, vec1.z - vec2.z);
}

static Vec3 Vec3_Add(const Vec3 vec1, const Vec3 vec2) {
    return Vector3(vec1.x + vec2.x, vec1.y + vec2.y, vec1.z + vec2.z);
}

static Vec3 Vec3_Scale(const Vec3 vec, const float scale) {
    return Vector3(vec.x * scale, vec.y * scale, vec.z * scale);
}

static Vec3 Vec3_Cross(const Vec3 vec1, const Vec3 vec2) {
    return Vector3(
        vec1.y * vec2.z - vec1.z * vec2.y,
        vec1.z * vec2.x - vec1.x * vec2.z,
        vec1.x * vec2.y - vec1.y * vec2.x
    );
}

static float Vec3_Magnitude(const Vec3 vec) {
    return Sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
}

static Vec3 Vec3_Normalize(const Vec3 vec) {
    const float mag = Vec3_Magnitude(vec);

    if (mag == 0) {
        return (Vec3){0, 0, 0};
    }

    if (mag == 1) {
        return vec;
    }

    return (Vec3){vec.x / mag, vec.y / mag, vec.z / mag};
}

static float Vec3_Dot(const Vec3 vec1, const Vec3 vec2) {
    return vec1.x * vec2.x + vec1.y * vec2.y + vec1.z * vec2.z;
}

typedef struct {
    float x;
    float y;
    float z;
    float w;
} Vec4;

static Vec4 Vector4(const float x, const float y, const float z, const float w) {
    return (Vec4){.x = x, .y = y, .z = z, .w = w};
}

#define WHITE Vector4(1.0f, 1.0f, 1.0f, 1.0f)
#define BLACK Vector4(0.0f, 0.0f, 0.0f, 1.0f)
#define RED Vector4(1.0f, 0.0f, 0.0f, 1.0f)
#define GREEN Vector4(0.0f, 1.0f, 0.0f, 1.0f)
#define BLUE Vector4(0.0f, 0.0f, 1.0f, 1.0f)
#define GRAY Vector4(0.5f, 0.5f, 0.5f, 1.0f)

typedef struct {
    float x;
    float y;
} Vec2;

/**
 * The matrix is stored in column major format
 **/
typedef struct {
    union {
        float m[4][4];
    };
} Mat4X4;

static Mat4X4 Matrix4X4(
    const float m00, const float m10, const float m20, const float m30,
    const float m01, const float m11, const float m21, const float m31,
    const float m02, const float m12, const float m22, const float m32,
    const float m03, const float m13, const float m23, const float m33
) {
    return (Mat4X4){
        .m[0][0] = m00, .m[1][0] = m10, .m[2][0] = m20, .m[3][0] = m30,
        .m[0][1] = m01, .m[1][1] = m11, .m[2][1] = m21, .m[3][1] = m31,
        .m[0][2] = m02, .m[1][2] = m12, .m[2][2] = m22, .m[3][2] = m32,
        .m[0][3] = m03, .m[1][3] = m13, .m[2][3] = m23, .m[3][3] = m33
    };
}

static Mat4X4 Matrix_Perspective(const float fov_y, const float aspect_ratio, const float near, const float far) {
    const float n = near;
    const float t = Tan(fov_y / 2.0f) * n;
    const float r = t * aspect_ratio;

    return Matrix4X4(
        n / r, 0, 0, 0,
        0, n / t, 0, 0,
        0, 0, 0.0f, n,
        0, 0, -1.0f, 0.0f
    );
}

static Mat4X4 Matrix_LookAt(const Vec3 pos, const Vec3 target, const Vec3 up) {
    const Vec3 d = Vec3_Normalize(Vec3_Sub(target, pos));
    Vec3 u = Vec3_Normalize(up);
    const Vec3 r = Vec3_Normalize(Vec3_Cross(u, d));
    u = Vec3_Cross(r, d);

    return Matrix4X4(
        r.x, r.y, r.z, -Vec3_Dot(r, pos),
        u.x, u.y, u.z, -Vec3_Dot(u, pos),
        -d.x, -d.y, -d.z, Vec3_Dot(d, pos),
        0, 0, 0, 1
    );
}

static Mat4X4 Matrix_RotationY(const float angle) {
    const float cos = Cos(angle);
    const float sin = Sin(angle);

    return Matrix4X4(
        cos, 0, sin, 0,
        0, 1, 0, 0,
        -sin, 0, cos, 0,
        0, 0, 0, 1
    );
}

static Mat4X4 Matrix_Multiply(const Mat4X4 mat1, const Mat4X4 mat2) {
    Mat4X4 res;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int x = 0; x < 4; x++) {
                sum += mat1.m[x][j] * mat2.m[i][x];
            }
            res.m[i][j] = sum;
        }
    }

    return res;
}

static Mat4X4 Matrix_OrthographicScreen(const float width, const float height) {
    return Matrix4X4(
        2.0f / width, 0, 0, -1.0f,
        0, -2.0f / height, 0, 1.0f,
        0, 0, -1.0f, 0,
        0, 0, 0, 1
    );
}

static Mat4X4 Matrix_Translation(const Vec3 v) {
    return Matrix4X4(
        1, 0, 0, v.x,
        0, 1, 0, v.y,
        0, 0, 1, v.z,
        0, 0, 0, 1
    );
}

static Mat4X4 Matrix_Scale(const Vec3 v) {
    return Matrix4X4(
        v.x, 0, 0, 0,
        0, v.y, 0, 0,
        0, 0, v.z, 0,
        0, 0, 0, 1.0f
    );
}

typedef struct {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
} Vertex;

#endif //GAMING_GAME_MATH_H
