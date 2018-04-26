#include <cmath>
#include <algorithm>

inline float to_radians(float d)
{
     return d / 180 * M_PI;
}

inline float to_degrees(float d)
{
     return d * 180 / M_PI;
}

struct vec4
{
    float x, y, z, w;
    vec4() {}
    vec4(const vec4&v) :
        x(v.x), y(v.y), z(v.z), w(v.w)
    {}
    vec4(float v) :
        x(v), y(v), z(v), w(1)
    {}
    vec4(float x_, float y_, float z_, float w_) :
        x(x_), y(y_), z(z_), w(w_)
    {}
    vec4& operator=(const vec4& p)
    {
        x = p.x;
        y = p.y;
        z = p.z;
        w = p.w;
        return *this;
    }
    vec4& set(float x_, float y_, float z_, float w_)
    {
        x = x_;
        y = y_;
        z = z_;
        w = w_;
        return *this;
    }
};


struct vec3
{
    float x, y, z;
    vec3() {}
    vec3(const vec3&v) :
        x(v.x), y(v.y), z(v.z)
    {}
    vec3(float v) :
        x(v), y(v), z(v)
    {}
    vec3(float x_, float y_, float z_) :
        x(x_), y(y_), z(z_)
    {}
    vec3& operator=(const vec3& p)
    {
        x = p.x;
        y = p.y;
        z = p.z;
        return *this;
    }
    vec3& set(float x_, float y_, float z_)
    {
        x = x_;
        y = y_;
        z = z_;
        return *this;
    }
    const vec3& store(float *f, unsigned int i) const
    {
        f[i * 3 + 0] = x;
        f[i * 3 + 1] = y;
        f[i * 3 + 2] = z;
        return *this;
    }
};

inline vec3 operator-(const vec3& v1, const vec3& v2)
{
    vec3 v;
    v.set(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z);
    return v;
}

inline vec3 operator*(const vec3& v1, const vec3& v2)
{
    vec3 v;
    v.set(v1.x * v2.x, v1.y * v2.y, v1.z * v2.z);
    return v;
}

inline vec3 operator+(const vec3& v1, const vec3& v2)
{
    vec3 v;
    v.set(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z);
    return v;
}

inline vec3 max(const vec3& v0, const vec3& v1)
{
    return vec3(std::max(v0.x, v1.x), std::max(v0.y, v1.y), std::max(v0.z, v1.z));
}

inline vec3 min(const vec3& v0, const vec3& v1)
{
    return vec3(std::min(v0.x, v1.x), std::min(v0.y, v1.y), std::min(v0.z, v1.z));
}

inline vec3 cross(const vec3& v0, const vec3& v1)
{
    vec3 v;
    v.x = v0.y * v1.z - v0.z * v1.y;
    v.y = v0.z * v1.x - v0.x * v1.z;
    v.z = v0.x * v1.y - v0.y * v1.x;
    return v;
}

inline vec3 operator/(const vec3& v, float d)
{
    vec3 r;
    r.set(v.x / d, v.y / d, v.z / d);
    return r;
}

inline vec3 operator*(const vec3& v, float d)
{
    vec3 r;
    r.set(v.x * d, v.y * d, v.z * d);
    return r;
}

inline float dot(const vec3& v1, const vec3& v2)
{
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

inline vec3 normalize(const vec3& v)
{
    return v / sqrtf(dot(v, v));
}

struct ray {
    vec3 o; /* origin */
    vec3 d; /* normalized direction */
};

struct box3d {
    vec3 boxmin;
    vec3 boxmax;
    box3d(const vec3& boxmin_, const vec3& boxmax_) :
        boxmin(boxmin_),
        boxmax(boxmax_)
    {}
    box3d() :
        boxmin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
        boxmax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max())
    {
    }
    vec3 center() const
    {
        return (boxmin + boxmax) * .5;
    }
    vec3 dim() const // for any dimension for which min > max, returns 0
    {
        return max(vec3(0, 0, 0), boxmax - boxmin);
    }
    box3d& add(const vec3& v)
    {
        const float bumpout = .00001;
        boxmin = min(boxmin, v - bumpout);
        boxmax = max(boxmax, v + bumpout);
        return *this;
    }
    box3d& add(const vec3& c, float r)
    {
        const float bumpout = 1.0001;
        boxmin = min(boxmin, c - r * bumpout);
        boxmax = max(boxmax, c + r * bumpout);
        return *this;
    }
    box3d& add(const vec3& addmin, const vec3& addmax)
    {
        boxmin = min(addmin, boxmin);
        boxmax = max(addmax, boxmax);
        return *this;
    }
    box3d& add(const box3d& box)
    {
        boxmin = min(box.boxmin, boxmin);
        boxmax = max(box.boxmax, boxmax);
        return *this;
    }
    box3d& add(const vec3& v0, const vec3& v1, const vec3& v2)
    {
        add(v0);
        add(v1);
        add(v2);
        return *this;
    }

};

inline void add_sphere(vec3 *C1, float *R1, const vec3& C2, float R2)
{
    static float epsilon = .000001;

    vec3 d = C2 - *C1;
    float len = sqrtf(dot(d, d));

    if(len + R2 <= *R1) {
        *R1 += epsilon;
        return;
    }

    if(len + *R1 <= R2) {
        *C1 = C2;
        *R1 = R2 + epsilon;
        return;
    }

    vec3 dhat = d - len;
    float rprime = (*R1 + R2 + len) / 2;
    vec3 cprime = *C1 + dhat * (rprime - *R1);

    *C1 = cprime;
    *R1 = rprime + epsilon;
}

static const float mat4_identity[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

inline vec4 operator*(const float m[16], const vec4& in)
{
    int i;
    float t[4];

    for(i = 0; i < 4; i++) {
        t[i] =
            m[0 + i] * in.x + 
            m[4 + i] * in.y + 
            m[8 + i] * in.z + 
            m[12 + i] * in.w;
    }

    return vec4(t[0], t[1], t[2], t[3]);
}

inline void mat4_make_identity(float mat[16])
{
    memcpy(mat, mat4_identity, sizeof(mat4_identity));
}

inline float mat4_determinant(const float mat[16])
{
    return (mat[0] * mat[5] - mat[1] * mat[4]) *
        (mat[10] * mat[15] - mat[11] * mat[14]) + 
        (mat[2] * mat[4] - mat[0] * mat[6]) *
        (mat[9] * mat[15] - mat[11] * mat[13]) + 
        (mat[0] * mat[7] - mat[3] * mat[4]) *
        (mat[9] * mat[14] - mat[10] * mat[13]) + 
        (mat[1] * mat[6] - mat[2] * mat[5]) *
        (mat[8] * mat[15] - mat[11] * mat[12]) + 
        (mat[3] * mat[5] - mat[1] * mat[7]) *
        (mat[8] * mat[14] - mat[10] * mat[12]) + 
        (mat[2] * mat[7] - mat[3] * mat[6]) *
        (mat[8] * mat[13] - mat[9] * mat[12]);
}

inline void mat4_transpose(const float mat[16], float result[16])
{
    float tmp[16];
    int i, j;

    memcpy(tmp, mat, sizeof(tmp));
    for(i = 0; i < 4; i++) {
        for(j = 0; j < 4; j++) {
            result[i + j * 4] = tmp[j + i *4] ;
        }
    }
}

inline int mat4_invert(const float mat[16], float inv[16])
{
    int         i, rswap;
    float       det, div, swap;
    float       hold[16];
    const float EPSILON = .00001;

    memcpy(hold, mat, sizeof(mat[0]) * 16);
    memcpy(inv, mat4_identity, sizeof(mat[0]) * 16);
    det = mat4_determinant(mat);
    if(fabs(det) < EPSILON) /* singular? */ {
        return -1;
    }

    rswap = 0;
    /* this loop isn't entered unless [0 + 0] > EPSILON and det > EPSILON,
         so rswap wouldn't be 0, but I initialize so as not to get warned */
    if(fabs(hold[0]) < EPSILON)
    {
        if(fabs(hold[1]) > EPSILON) {
            rswap = 1;
        } else if(fabs(hold[2]) > EPSILON) {
            rswap = 2;
        } else if(fabs(hold[3]) > EPSILON) {
            rswap = 3;
        }

        for(i = 0; i < 4; i++)
        {
            swap = hold[i * 4 + 0];
            hold[i * 4 + 0] = hold[i * 4 + rswap];
            hold[i * 4 + rswap] = swap;

            swap = inv[i * 4 + 0];
            inv[i * 4 + 0] = inv[i * 4 + rswap];
            inv[i * 4 + rswap] = swap;
        }
    }
        
    div = hold[0];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 0] /= div;
        inv[i * 4 + 0] /= div;
    }

    div = hold[1];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 1] -= div * hold[i * 4 + 0];
        inv[i * 4 + 1] -= div * inv[i * 4 + 0];
    }
    div = hold[2];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 2] -= div * hold[i * 4 + 0];
        inv[i * 4 + 2] -= div * inv[i * 4 + 0];
    }
    div = hold[3];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 3] -= div * hold[i * 4 + 0];
        inv[i * 4 + 3] -= div * inv[i * 4 + 0];
    }

    if(fabs(hold[5]) < EPSILON){
        if(fabs(hold[6]) > EPSILON) {
            rswap = 2;
        } else if(fabs(hold[7]) > EPSILON) {
            rswap = 3;
        }

        for(i = 0; i < 4; i++)
        {
            swap = hold[i * 4 + 1];
            hold[i * 4 + 1] = hold[i * 4 + rswap];
            hold[i * 4 + rswap] = swap;

            swap = inv[i * 4 + 1];
            inv[i * 4 + 1] = inv[i * 4 + rswap];
            inv[i * 4 + rswap] = swap;
        }
    }

    div = hold[5];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 1] /= div;
        inv[i * 4 + 1] /= div;
    }

    div = hold[4];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 0] -= div * hold[i * 4 + 1];
        inv[i * 4 + 0] -= div * inv[i * 4 + 1];
    }
    div = hold[6];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 2] -= div * hold[i * 4 + 1];
        inv[i * 4 + 2] -= div * inv[i * 4 + 1];
    }
    div = hold[7];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 3] -= div * hold[i * 4 + 1];
        inv[i * 4 + 3] -= div * inv[i * 4 + 1];
    }

    if(fabs(hold[10]) < EPSILON){
        for(i = 0; i < 4; i++)
        {
            swap = hold[i * 4 + 2];
            hold[i * 4 + 2] = hold[i * 4 + 3];
            hold[i * 4 + 3] = swap;

            swap = inv[i * 4 + 2];
            inv[i * 4 + 2] = inv[i * 4 + 3];
            inv[i * 4 + 3] = swap;
        }
    }

    div = hold[10];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 2] /= div;
        inv[i * 4 + 2] /= div;
    }

    div = hold[8];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 0] -= div * hold[i * 4 + 2];
        inv[i * 4 + 0] -= div * inv[i * 4 + 2];
    }
    div = hold[9];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 1] -= div * hold[i * 4 + 2];
        inv[i * 4 + 1] -= div * inv[i * 4 + 2];
    }
    div = hold[11];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 3] -= div * hold[i * 4 + 2];
        inv[i * 4 + 3] -= div * inv[i * 4 + 2];
    }

    div = hold[15];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 3] /= div;
        inv[i * 4 + 3] /= div;
    }

    div = hold[12];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 0] -= div * hold[i * 4 + 3];
        inv[i * 4 + 0] -= div * inv[i * 4 + 3];
    }
    div = hold[13];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 1] -= div * hold[i * 4 + 3];
        inv[i * 4 + 1] -= div * inv[i * 4 + 3];
    }
    div = hold[14];
    for(i = 0; i < 4; i++)
    {
        hold[i * 4 + 2] -= div * hold[i * 4 + 3];
        inv[i * 4 + 2] -= div * inv[i * 4 + 3];
    }
    
    return 0;
}

inline void mat4_make_translation(float x, float y, float z, float matrix[16])
{
    mat4_make_identity(matrix);
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
}

inline void mat4_make_scale(float x, float y, float z, float matrix[16])
{
    mat4_make_identity(matrix);
    matrix[0] = x;
    matrix[5] = y;
    matrix[10] = z;
}

inline void mat4_mult(const float m1[16], const float m2[16], float r[16])
{
    float t[16];
    int i, j;

    for(j = 0; j < 4; j++) {
        for(i = 0; i < 4; i++) {
           t[i * 4 + j] = m1[i * 4 + 0] * m2[0 * 4 + j] +
               m1[i * 4 + 1] * m2[1 * 4 + j] +
               m1[i * 4 + 2] * m2[2 * 4 + j] +
               m1[i * 4 + 3] * m2[3 * 4 + j];
        }
    }

    memcpy(r, t, sizeof(t));
}

inline void mat4_get_rotation(const float m[16], float r[4])
{
    float cosine;
    float d;

    cosine = (m[0] + m[5] + m[10] - 1.0f) / 2.0f;

    if(cosine > 1.0){
#if defined(DEBUG)
        fprintf(stderr, "XXX acos of greater than 1! (clamped)\n");
#endif // DEBUG
        cosine = 1.0;
    }
    if(cosine < -1.0){
#if defined(DEBUG)
        fprintf(stderr, "XXX acos of less than -1! (clamped)\n");
#endif // DEBUG
        cosine = -1.0;
    }

    r[0] = (float)acos(cosine);

#if defined(DEBUG)
    if(r[0] != r[0]) /* isNAN */
        abort();
#endif // DEBUG

    r[1] = (m[6] - m[9]);
    r[2] = (m[8] - m[2]);
    r[3] = (m[1] - m[4]);

    d = sqrt(r[1] * r[1] +
        r[2] * r[2] +
        r[3] * r[3]);

    r[1] /= d;
    r[2] /= d;
    r[3] /= d;
}

inline void mat4_make_rotation(float a, float x, float y, float z, float matrix[16])
{
    float c, s, t;

    c = (float)cos(a);
    s = (float)sin(a);
    t = 1.0f - c;

    matrix[0] = t * x * x + c;
    matrix[1] = t * x * y + s * z;
    matrix[2] = t * x * z - s * y;
    matrix[3] = 0;

    matrix[4] = t * x * y - s * z;
    matrix[5] = t * y * y + c;
    matrix[6] = t * y * z + s * x;
    matrix[7] = 0;

    matrix[8] = t * x * z + s * y;
    matrix[9] = t * y * z - s * x;
    matrix[10] = t * z * z + c;
    matrix[11] = 0;

    matrix[12] = 0;
    matrix[13] = 0;
    matrix[14] = 0;
    matrix[15] = 1;
}

inline void rotation_mult_rotation(const float rotation1[4], const float rotation2[4], float result[4])
{
    float matrix1[16];
    float matrix2[16];
    float matrix3[16];

    mat4_make_rotation(rotation1[0], rotation1[1], rotation1[2],
        rotation1[3], matrix1);
    mat4_make_rotation(rotation2[0], rotation2[1], rotation2[2],
        rotation2[3], matrix2);
    mat4_mult(matrix2, matrix1, matrix3);
    mat4_get_rotation(matrix3, result);
}

