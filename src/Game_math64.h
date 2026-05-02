#ifndef GAMING_GAME_MATH64_H
#define GAMING_GAME_MATH64_H

typedef struct {
    double x, y, z;
} Vec3d;

static Vec3d Vector3d(const double x, const double y, const double z) {
    return (Vec3d){x, y, z};
}

static Vec3d Vec3_Cast64(const Vec3 vec) {
    return Vector3d(vec.x, vec.y, vec.z);
}

static Vec3 Vec3d_Cast32(const Vec3d vec) {
    return Vector3((float)vec.x, (float)vec.y, (float)vec.z);
}

static Vec3d Vec3d_Add(const Vec3d vec1, const Vec3d vec2) {
    return Vector3d(vec1.x + vec2.x, vec1.y + vec2.y, vec1.z + vec2.z);
}

static Vec3d Vec3d_Scale(const Vec3d vec, const float scale) {
    return Vector3d(vec.x * scale, vec.y * scale, vec.z * scale);
}

static Vec3d Vec3d_Sub(const Vec3d vec1, const Vec3d vec2) {
    return Vector3d(vec1.x - vec2.x, vec1.y - vec2.y, vec1.z - vec2.z);
}

#endif //GAMING_GAME_MATH64_H
