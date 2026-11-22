#version 450

const int kMaxLights = 9;
const int kMaxTiles  = 13;

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(set = 3, binding = 0) uniform sampler2D normalTex;
layout(set = 4, binding = 0) uniform sampler2D mrTex;
layout(set = 5, binding = 0) uniform sampler2D emissiveTex;
layout(set = 6, binding = 0) uniform sampler2D occlusionTex;

layout(std140, set = 2, binding = 0) uniform Scene
{
    vec4 head;
    vec4 camPos;
    mat4 tileMat[kMaxTiles];
    vec4 tileRect[kMaxTiles];
    vec4 tileParam[kMaxTiles];
    vec4 lightPos[kMaxLights];
    vec4 lightColor[kMaxLights];
    vec4 lightDir[kMaxLights];
    vec4 lightMisc[kMaxLights];
};

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inWorld;
layout(location = 4) in float inLit;
layout(location = 5) in float inFlags;
layout(location = 6) in vec4 inMaterial;
layout(location = 7) in vec4 inEmissive;

const float PI = 3.14159265;

layout(location = 0) out vec4 outColor;

float linearDepth(float z, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - z * (farZ - nearZ));
}

float tileVisibility(int t, vec3 wp, vec3 n, vec3 lightOrigin)
{
    vec4 prm = tileParam[t];
    bool perspective = prm.w > 0.0;
    float offset = perspective ? prm.x * length(wp - lightOrigin) : prm.x;
    vec4 c = tileMat[t] * vec4(wp + n * offset, 1.0);
    if (c.w <= 0.0)
        return 1.0;
    vec3 p  = c.xyz / c.w;
    vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0 || p.z < 0.0)
        return 1.0;

    vec4 rect   = tileRect[t];
    vec2 texel  = 1.0 / (rect.zw * head.w);
    float lit   = 0.0;
    float mine  = perspective ? linearDepth(p.z, prm.z, prm.w) - prm.y : p.z - prm.y;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            vec2 suv      = clamp(uv + vec2(float(x), float(y)) * texel, vec2(0.0), vec2(1.0));
            float stored  = texture(shadowMap, rect.xy + suv * rect.zw).r;
            float other   = perspective ? linearDepth(stored, prm.z, prm.w) : stored;
            lit += (mine <= other) ? 1.0 : 0.0;
        }
    return lit / 9.0;
}

int pointFace(vec3 d)
{
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
        return d.x >= 0.0 ? 0 : 1;
    if (a.y >= a.z)
        return d.y >= 0.0 ? 2 : 3;
    return d.z >= 0.0 ? 4 : 5;
}

float distributionGgx(float nh, float rough)
{
    float a  = rough * rough;
    float a2 = a * a;
    float d  = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-6);
}

float visibilitySmith(float nv, float nl, float rough)
{
    float a  = rough * rough;
    float gv = nl * sqrt(nv * nv * (1.0 - a) + a);
    float gl = nv * sqrt(nl * nl * (1.0 - a) + a);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 fresnel(vec3 f0, float vh)
{
    return f0 + (1.0 - f0) * pow(1.0 - clamp(vh, 0.0, 1.0), 5.0);
}

vec3 specularTerm(vec3 n, vec3 v, vec3 l, vec3 f0, float rough, float nv)
{
    float nl = clamp(dot(n, l), 0.0, 1.0);
    vec3 h   = normalize(v + l);
    float nh = clamp(dot(n, h), 0.0, 1.0);
    float vh = clamp(dot(v, h), 0.0, 1.0);
    return distributionGgx(nh, rough) * visibilitySmith(nv, nl, rough) * fresnel(f0, vh) * nl * PI;
}

void main()
{
    vec4 base = texture(tex, inUV) * inColor;
    int matFlags = int(inFlags + 0.5);
    if ((matFlags & 16) != 0 && base.a < inEmissive.a)
        discard;
    if (inLit < 0.5)
    {
        outColor = base;
        return;
    }

    int flags = matFlags;

    vec3 n = normalize(inNormal);
    if ((flags & 32) != 0 && !gl_FrontFacing)
        n = -n;
    if ((flags & 1) != 0)
    {
        vec3 dp1  = dFdx(inWorld);
        vec3 dp2  = dFdy(inWorld);
        vec2 duv1 = dFdx(inUV);
        vec2 duv2 = dFdy(inUV);
        vec3 dp2perp = cross(dp2, n);
        vec3 dp1perp = cross(n, dp1);
        vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
        float invMax = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
        vec3 tn = texture(normalTex, inUV).xyz * 2.0 - 1.0;
        tn.xy *= inMaterial.z;
        n = normalize(mat3(t * invMax, b * invMax, n) * tn);
    }

    float metallic  = clamp(inMaterial.x, 0.0, 1.0);
    float roughness = clamp(inMaterial.y, 0.04, 1.0);
    if ((flags & 2) != 0)
    {
        vec3 mr   = texture(mrTex, inUV).rgb;
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic  = clamp(metallic * mr.b, 0.0, 1.0);
    }
    float occlusion = 1.0;
    if ((flags & 8) != 0)
        occlusion = 1.0 + inMaterial.w * (texture(occlusionTex, inUV).r - 1.0);
    vec3 emissive = inEmissive.rgb;
    if ((flags & 4) != 0)
        emissive *= texture(emissiveTex, inUV).rgb;

    vec3 v        = normalize(camPos.xyz - inWorld);
    float nv      = clamp(abs(dot(n, v)), 1e-4, 1.0);
    vec3 f0       = mix(vec3(0.04), base.rgb, metallic);
    vec3 diffuseColor = base.rgb * (1.0 - metallic);

    float ambient = head.y;
    int count    = int(head.x);

    float dirTerm = 0.0;
    vec3 dirSpec  = vec3(0.0);
    vec3 local    = vec3(0.0);
    vec3 localSpec = vec3(0.0);

    for (int i = 0; i < count; ++i)
    {
        float type = lightPos[i].w;
        int first  = int(lightMisc[i].x);
        if (type < 0.5)
            continue;

        if (type < 1.5)
        {
            float diffuse = clamp(dot(n, lightPos[i].xyz), 0.0, 1.0);
            float vis     = 1.0;
            if (first >= 0)
                vis = mix(1.0, tileVisibility(first, inWorld, n, vec3(0.0)), lightMisc[i].z);
            dirTerm += diffuse * vis;
            if (diffuse > 0.0)
                dirSpec += specularTerm(n, v, lightPos[i].xyz, f0, roughness, nv) * vis;
            continue;
        }

        vec3 toLight = lightPos[i].xyz - inWorld;
        float dist   = length(toLight);
        float range  = lightColor[i].w;
        if (dist >= range)
            continue;
        vec3 l       = toLight / max(dist, 1e-4);
        float diffuse = clamp(dot(n, l), 0.0, 1.0);
        if (diffuse <= 0.0)
            continue;

        float ratio = dist / range;
        float fall  = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
        float atten = fall * fall / (dist * dist + 1.0);

        if (type > 2.5)
        {
            float cosAngle = dot(-l, lightDir[i].xyz);
            atten *= smoothstep(lightDir[i].w, lightMisc[i].y, cosAngle);
            if (atten <= 0.0)
                continue;
        }

        float vis = 1.0;
        if (first >= 0)
        {
            int tile = type > 2.5 ? first : first + pointFace(inWorld - lightPos[i].xyz);
            vis      = mix(1.0, tileVisibility(tile, inWorld, n, lightPos[i].xyz), lightMisc[i].z);
        }
        local += lightColor[i].rgb * (diffuse * atten * vis);
        localSpec += lightColor[i].rgb * (specularTerm(n, v, l, f0, roughness, nv) * atten * vis);
    }

    vec3 lighting = vec3(ambient + (1.0 - ambient) * dirTerm) + local;
    vec3 specular = dirSpec * (1.0 - ambient) + localSpec + f0 * ambient;
    vec3 color    = (diffuseColor * lighting + specular) * occlusion + emissive;
    outColor      = vec4(color, base.a);
}
