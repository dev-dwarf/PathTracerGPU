/* TODO(lcf)(June 14, 2023):
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

struct Material {
    float3 albedo;
    float3 specular;
    float3 emissive;
    float specularity;
    float specularRoughness;
    float3 refractive;
    float refraction;
    float refractionRoughness;
};

struct Hit {
    Ray normal;
    bool inside;
    float dist;
    Material mat;
};

struct Sphere {
    float3 pos;
    float radius;
    Material mat;
};


inline uint RNG(inout uint state)
{
    uint x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 15;
    state = x;
    return x;
}
float Random(inout uint state)
{
    return (RNG(state) & 0xFFFFFF) / 16777216.0f;
}
float3 RandomInSphere(inout uint state)
{
    float z = Random(state) * 2.0f - 1.0f;
    float t = Random(state) * 2.0f * 3.1415926f;
    float r = sqrt(max(0.0, 1.0f - z * z));
    float x = r * cos(t);
    float y = r * sin(t);
    float3 res = float3(x, y, z);
    res *= pow(Random(state), 1.0 / 3.0);
    return res;
}
float3 RandomUnit(inout uint state)
{
    float z = Random(state) * 2.0f - 1.0f;
    float a = Random(state) * 2.0f * 3.1415926f;
    float r = sqrt(1.0f - z * z);
    float x = r * cos(a);
    float y = r * sin(a);
    return float3(x, y, z);
}

float3 RayAt(Ray r, float t)
{
    return r.pos + r.dir*t;
}

#define MIN_DIST 0.001f
#define MAX_DIST 1000.0f
bool HitSphere(Sphere s, Ray r, out Hit h)
{
    h = (Hit) 0;
    
    bool hit = false;
    float3 oc = r.pos - s.pos;
    float a = dot(r.dir, r.dir);
    float b = dot(oc, r.dir) /* * 2.0 */;
    float c = dot(oc, oc) - s.radius*s.radius;
    float disc4 = b*b - a*c; /* discriminant divided by 4 */
    if (disc4 > 0.0f) {
        h.dist = (-b - sqrt(disc4)) / a;
        if (h.dist < MIN_DIST || MAX_DIST < h.dist) {
            h.dist = (-b + sqrt(disc4)) / a;
        }
        if (MIN_DIST < h.dist && h.dist < MAX_DIST) {
            hit = true;
            h.normal.pos = RayAt(r, h.dist);
            h.normal.dir = (h.normal.pos - s.pos) / s.radius;
            h.mat = s.mat;
            h.inside = dot(r.dir, h.normal.dir) > 0;
            if (h.inside) {
                h.normal.dir = -h.normal.dir;
            }
        }
    }
    
    return hit;
}


void HitScene(Ray ray, out Hit h)
{    
    Hit newh;
    h.dist = MAX_DIST;

    float3 w = float3(1.00, 1.00, 1.00);
    float3 r = float3(0.75, 0.40, 0.40);
    float3 g = float3(0.25, 0.75, 0.55);
    float3 b = float3(0.25, 0.55, 0.75);
    float3 k = float3(0.25, 0.25, 0.25);
    float3 o = float3(0.0, 0.0, 0.0);
    Sphere spheres[] = {
        {float3( 1e3-2, 0, 0), 1e3, {k, w, o, 0.8, 0.1, o, 0, 0}}, // left
        {float3(-1e3+2, 0, 0), 1e3, {k, w, o, 0.8, 0.1, o, 0, 0}}, // right
        {float3(0, 0, 1e3-02), 1e3, {k, w, o, 0.8, 0.1, o, 0, 0}}, // front (behind camera)
        {float3(0, 0,-1e3+05), 1e3, {k, w, o, 0.8, 0.1, o, 0, 0}}, // back
        {float3(0, 1e3-01, 0), 1e3, {o, b, o, 0.5, 0.1, o, 0, 0}}, // bottom
        {float3(0,-1e3+02, 0), 1e3, {k, w, r, 0.0, 0.1, o, 0, 0}}, // topt
        {float3(-1,-0.25, 2.2), 0.33, {o, b, o, 0.25, 0.03, o, 0.00, 0.0}},
        {float3( 1,-0.25, 2.2), 0.33, {w, g, o, 0.50, 0.60, o, 0.00, 0.0}},
        {float3( 0, 0.25, 2.0), 0.33, {o, o, o, 0.00, 0.00, r, 0.95, 0.1}},
        {float3( 0, 0.25, 2.0),-0.25, {o, o, o, 0.00, 0.00, w, 1.00, 0.0}},
    };

    for (int i = 0; i < 10; i++) {
        if (HitSphere(spheres[i], ray, newh)) {
            if (newh.dist < h.dist) {
                h = newh;
            }
        }
    }
}


col RayColor(float2 uv, Ray r, inout uint rng)
{
    const int RAY_MIN_DEPTH = 4;
    const int RAY_MAX_DEPTH = 64;
    col ret = float3(0.0, 0.0, 0.0);
    col atten = float3(1.0, 1.0, 1.0);

    Hit h;
    for (int b = 0; b < RAY_MAX_DEPTH; b++) {
        h.dist = MAX_DIST;
        HitScene(r, h);
        
        if (h.dist == MAX_DIST) {
            float t = 0.5*(r.dir.y+1.0);
            ret += atten*lerp(float3(0.5f, 0.5f, 1.0f), float3(1.0f, 1.0f, 1.0f), t);
            /* ret += atten*float3(0.039, 0.012, 0.051); */
            break;
        }


        if (h.inside) {
            atten *= exp(h.mat.refractive * h.dist);
        }

        float roll = Random(rng);
        bool doSpecular = roll < h.mat.specularity;
        bool doRefract = !doSpecular && (roll < h.mat.specularity + h.mat.refraction);
        r.pos = h.normal.pos + 0.0001*(doRefract? -h.normal.dir : h.normal.dir);
        
        /* float3 diffuseDir = normalize(h.normal.dir+RandomUnit(rng)); */
        // alt: random in hemisphere
        float3 diffuseDir = RandomUnit(rng);
        if (dot(diffuseDir, h.normal.dir) < 0.0) {
            diffuseDir = -diffuseDir;
        }
               
        float3 specularDir = reflect(r.dir, h.normal.dir);
        float IOF = 1.5;
        float3 refractDir = refract(r.dir, h.normal.dir, h.inside? IOF : 1.0 / IOF);
        if (!any(refractDir)) {
            refractDir = specularDir;
        }
        specularDir = normalize(lerp(specularDir, diffuseDir, h.mat.specularRoughness*h.mat.specularRoughness));
        refractDir = normalize(lerp(refractDir, -diffuseDir, h.mat.refractionRoughness*h.mat.refractionRoughness));

        ret += atten*h.mat.emissive;

        r.dir = doSpecular? specularDir : diffuseDir;
        r.dir = doRefract? refractDir : r.dir;

        if (!doRefract) {
            atten *= doSpecular? h.mat.specular : h.mat.albedo;
        }
        
        /* r.dir = diffuseDir; */
        /* atten *= h.mat.albedo; */

        /* Russian Roulette kill paths */
        if (b > RAY_MIN_DEPTH) {
            float p = max(atten.r, max(atten.g, atten.b));
            if (Random(rng) > p) {
                break;
            }
            atten *= 1.0 / p;
        }

        /* Uncomment to visualize normals / reflection dirs */
        /* if (uv.x > 0) { */
        /* ret = (h.normal.dir+1.0)*0.5; */
        /* } else { */
        /* ret = (r.dir+1.0)*0.5; */
        /* } */
        /* ret = refractDir; */
        /* break; */
    }

    return ret;
}

// ACES tone mapping curve fit to go from HDR to LDR
//https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x + b)) / (x*(c*x + d) + e), 0.0f, 1.0f);
}

float3 LessThan(float3 f, float value)
{
    return float3(
        (f.x < value) ? 1.0f : 0.0f,
        (f.y < value) ? 1.0f : 0.0f,
        (f.z < value) ? 1.0f : 0.0f);
}

float3 LinearToSRGB(float3 rgb)
{
    rgb = clamp(rgb, 0.0f, 1.0f);
     
    return lerp(
        pow(rgb, 1.0f / 2.4f) * 1.055f - 0.055f,
        rgb * 12.92f,
        LessThan(rgb, 0.0031308f)
    );
}


inline bool NanTest(float3 x)
{
    return ((asuint(x.r) & 0x7fffffff) > 0x7f800000)
        || ((asuint(x.g) & 0x7fffffff) > 0x7f800000)
        || ((asuint(x.b) & 0x7fffffff) > 0x7f800000);
}

[numthreads(16, 8, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    /* const float FOV = 15.0; */
    const float FOV = 60;
    const int RAYS = 1;

    float inno = 1.0 / (iFrame+1.0);
    bool doTrace = inno > 0.000000001;

    float aspectRatio = (float) RenderDim.y / (float) RenderDim.x;
    float cameraDistance = 1.0f / tan(FOV * 0.5f * 3.14 / 180.0f); 
    float2 sv = float2(-1.0 + 2.0*(threadId.xy / (RenderDim-1.0)));
    sv.y *= -aspectRatio;

    if (doTrace) {
        float3 colaccum = 0.0;
        uint rng = threadId.x * 5051 + threadId.y * 1505 + iFrame * 90210;
        for (int i = 0; i < RAYS; i++) {
            float3 rand = RandomUnit(rng);
            float2 uv = sv + rand.xy/RenderDim.xy;
            Ray r;
            r.pos = float3(uv, 0);
            r.dir = normalize(float3(uv, cameraDistance));

            colaccum += RayColor(uv, r,rng) / (float)RAYS;
        
            /* colaccum = rand; */
        }

        if (!NanTest(colaccum)) {
            compute[threadId.xy] = lerp(compute[threadId.xy],float4(colaccum, 1.0f), inno);
            /* compute[threadId.xy] = float4(compute[threadId.xy].xyz + 0.01*colaccum, 1.0f); */
        }
    }

    col base = compute[threadId.xy].rgb;
    if ((doTrace || sv.x < 0.95) && any(base)) {
        const float exposure = 0.95;
        base *= exposure;
        base = ACESFilm(base);
        base = LinearToSRGB(base);
        backbuffer[threadId.xy] = float4(base, 1.0);
    } else {
        backbuffer[threadId.xy] = float4(1, 0, 0.35, 1);
    }

} 
