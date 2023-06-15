/* TODO(lcf)(June 14, 2023):
   RNG, scalar, float2, float3, etc.
   Persistent color buffer.
   More materials
*/

RWTexture2D<unorm float4> backbuffer : register(u0);
RWTexture2D<float4> compute : register(u1);

cbuffer Constants
{
    uint iFrame;
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
    float3 albedo;
    float3 emissive;
    float dist;
    bool front_face;
};

struct Sphere {
    float3 pos;
    float radius;
};

float Random(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float3 RandomUnit(float3 seed)
{
    float3 p = float3(0.0f, 0.0f, 0.0f);
    do
    {
        p = 2.0f * float3(Random(seed.xy), Random(seed.yz), Random(seed.zx)) - float3(0.99f, 0.99f, 0.99f);
    }
    while (length(p * p) > 1.0f);

    return p;
}

float3 RayAt(Ray r, float t)
{
    return r.pos + r.dir*t;
}

#define MIN_DIST 0.1f
#define MAX_DIST 1000.0f
bool HitSphere(Sphere s, Ray r, out Hit h)
{
    bool hit = false;
    float3 oc = r.pos - s.pos;
    float a = dot(r.dir, r.dir);
    float b = dot(oc, r.dir) /* * 2.0 */;
    float c = dot(oc, oc) - s.radius*s.radius;
    float disc4 = b*b - a*c; /* discriminant divided by 4 */
    h.dist = 0.0;
    h.normal = r;
    h.albedo = float3(0.0f, 0.0f, 0.0f);
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
            /* h.albedo = 0.5*(h.normal.dir+1.0); */
            /* h.emissive = h.albedo; */
        }
    }
    
    return hit;
}

bool HitScene(Ray r, out Hit h)
{
    bool hit = false; Hit newh;
    h.dist = MAX_DIST;

    Sphere s1 = {float3(0.1, 0, -2), 0.5};
    if (HitSphere(s1, r, newh)) {
        if (newh.dist < h.dist) {
            hit = true;
            h = newh;
            h.albedo = float3(0.0, 0.5, 0.0);
            h.emissive = float3(1.0, 0.0, 0.35);
        }
    }

    Sphere s2 = {float3(0, -100.5, -2), 100};
    if (HitSphere(s2, r, newh)) {
        if (newh.dist < h.dist) {
            hit = true;
            h = newh;
            h.albedo = float3(0.2, 0.4, 0.4);
            h.emissive = float3(0.0, 0.0, 0.0);
        }
    }

    return hit;
}


col RayColor(Ray r)
{
    col ret = float3(0.0, 0.0, 0.0);
    col atten = float3(1.0, 1.0, 1.0);

    for (int b = 0; b < 8; b++) {
        Hit h;
        h.dist = MAX_DIST;
        HitScene(r, h);
        
        if (h.dist == MAX_DIST) {
            float t = 0.5*(r.dir.y+1.0);
            ret += atten*lerp(float3(1.0f, 1.0f, 1.0f), float3(0.5f, 0.5f, 1.0f), t);
            break;
        }

        r.pos = h.normal.pos + h.normal.dir*0.001;
        r.dir = normalize(h.normal.dir + RandomUnit(r.dir + iTime));

        ret += h.emissive*atten;
        atten *= h.albedo;
    }

    return ret;
}

[numthreads(16, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    float aspectRatio = (float) RenderDim.y / (float) RenderDim.x;
    float2 sv = float2(-1.0 + 2.0*(threadId.xy / (RenderDim-1.0)));
    float3 rand = normalize(threadId.xyz+threadId.zxy+iTime);
    float3 colaccum = 0.0;
    const int rays = 16;
    for (int i = 0; i < rays; i++) {
        rand = RandomUnit(rand);
        float3 rayPos = float3(sv, 0);
        float3 rayTarget = float3(2.0*sv, -2.0) + rand/RenderDim.xyx;
        rayPos.y *= -aspectRatio;
        rayTarget.y *= -aspectRatio;
        float3 rayDir = normalize(rayTarget - rayPos);
        Ray r;
        r.pos = rayPos;
        r.dir = rayDir;

        colaccum += RayColor(r) / (float)rays;
        /* colaccum = rand; */
    }

    float inno = 1.0 / (iFrame+1.0);
    compute[threadId.xy] = lerp(compute[threadId.xy],float4(colaccum, 1.0f), inno);
    /* compute[threadId.xy] = float4(compute[threadId.xy].xyz + 0.01*colaccum, 1.0f); */
    backbuffer[threadId.xy] = compute[threadId.xy];
} 
