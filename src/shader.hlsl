/* TODO(lcf)(June 14, 2023):
   RNG, scalar, float2, float3, etc.
   Persistent color buffer.
   More materials
*/

RWTexture2D<unorm float4> outputTexture;

cbuffer Constants
{
    float iTime;
    float2 RenderDim;
};

typedef float3 col;

struct Ray {
    float3 pos;
    float3 dir;
};

struct Hit {
    Ray normal;
    float3 diffuse;
    float3 emissive;
    float dist;
    bool front_face;
};

struct Sphere {
    float3 pos;
    float radius;
};

float3 RayAt(Ray r, float t) {
    return r.pos + r.dir*t;
}

#define MIN_DIST 0.1f
#define MAX_DIST 1000.0f
bool HitSphere(Sphere s, Ray r, out Hit h) {
    bool hit = false;
    float3 oc = r.pos - s.pos;
    float a = dot(r.dir, r.dir);
    float b = dot(oc, r.dir) /* * 2.0 */;
    float c = dot(oc, oc) - s.radius*s.radius;
    float disc4 = b*b - a*c; /* discriminant divided by 4 */
    h.dist = 0.0;
    h.normal = r;
    h.diffuse = float3(0.0f, 0.0f, 0.0f);
    h.emissive = float3(0.0f, 0.0f, 0.0f);
    h.front_face = false;
    if (disc4 > 0.0f) {
        h.dist = (-b - sqrt(disc4)) / a;
        if (h.dist > MIN_DIST || MAX_DIST > h.dist) {
            h.dist = (-b + sqrt(disc4)) / a;
        }
        if (MIN_DIST < h.dist && h.dist < MAX_DIST) {
            hit = true;
            h.normal.pos = RayAt(r, h.dist);
            h.normal.dir = normalize(h.normal.pos - s.pos);
            h.front_face = dot(r.dir, h.normal.dir) < 0.0f;
            h.normal.dir.xy *= -1;
            if (!h.front_face) {
                h.normal.dir = -h.normal.dir;
            }
            h.diffuse = 0.5*(h.normal.dir+1.0);
            h.emissive = h.diffuse;
        }
    }
    
    return hit;
}

bool HitScene(Ray r, out Hit h) {
    bool hit = false; Hit newh;
    h.dist = MAX_DIST;

    Sphere s1 = {float3(0, 0, -2), 0.5};
    if (HitSphere(s1, r, newh)) {
        if (newh.dist < h.dist) {
            hit = true;
            h = newh;
        }
    }

    Sphere s2 = {float3(0, -100.5, -2), 100};
    if (HitSphere(s2, r, newh)) {
        if (newh.dist < h.dist) {
            hit = true;
            h = newh;
        }
    }

    return hit;
}


col RayColor(Ray r) {
    col ret;

    Hit h;
    if (HitScene(r, h)) {
        ret = h.diffuse;
    } else {
        float t = 0.5*(r.dir.y+1.0);
        ret = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.5f, 0.5f, 1.0f), t);
    }

    return ret;
}

[numthreads(16, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    float aspectRatio = (float) RenderDim.y / (float) RenderDim.x;
    float3 rayPos = float3(0.0f, 0.0f, 0.0f);
    float3 rayTarget = float3(-1.0 + 2.0*(threadId.xy / RenderDim), -1.0);
    rayTarget.y *= -aspectRatio;
    float3 rayDir = normalize(rayTarget - rayPos);
    Ray r;
    r.pos = rayPos;
    r.dir = rayDir;
    
    outputTexture[threadId.xy] = float4(RayColor(r), 1.0f);
} 
