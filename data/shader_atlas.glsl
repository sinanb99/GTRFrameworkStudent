//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
lighting basic.vs lighting.fs
multi basic.vs multi.fs
plain basic.vs plain.fs
lighting basic.vs lighting.fs
lighting_PBR basic.vs lighting_PBR.fs
gbuffer basic.vs gbuffer.fs
deferred quad.vs deferred.fs
lightvolume basic.vs lightvolume.fs
ssao quad.vs ssao.fs

\pbr_math
const float PI = 3.14159265359;

// 1. Normal Distribution Function (GGX / Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.0001); // Prevent division by 0
}

// 2. Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0; // Use k derived for analytic lights
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, 0.0001);
}

// 3. Smith's Method for Geometry Shadowing
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// 4. Fresnel Equation (Schlick's Approximation)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

\perturbNormal
// From https://github.com/glslify/glsl-perturb-normal/blob/master/cotangent-frame.glsl
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv)
{
	// get edge vectors of the pixel triangle
	vec3 dp1 = dFdx(p);
	vec3 dp2 = dFdy(p);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);

	// solve the linear system
	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	// construct a scale-invariant frame 
	float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
	return mat3(T * invmax, B * invmax, N);
}

// assume N, the interpolated vertex normal and 
// WP the world position
vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	mat3 TBN = cotangent_frame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}

\basic.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;

uniform vec3 u_camera_pos;

uniform mat4 u_model;
uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\quad.vs

#version 330 core

in vec3 a_vertex;
in vec2 a_coord;
out vec2 v_uv;

void main()
{	
	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
}


\flat.fs

#version 330 core

uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}


\texture.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	if(color.a < u_alpha_cutoff)
		discard;

	FragColor = color;
}


\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	FragColor = color;
}


\multi.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, uv );

	if(color.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize(v_normal);

	FragColor = color;
	NormalColor = vec4(N,1.0);
}


\depth.fs

#version 330 core

uniform vec2 u_camera_nearfar;
uniform sampler2D u_texture; //depth map
in vec2 v_uv;
out vec4 FragColor;

void main()
{
	float n = u_camera_nearfar.x;
	float f = u_camera_nearfar.y;
	float z = texture(u_texture,v_uv).x;
	if( n == 0.0 && f == 1.0 )
		FragColor = vec4(z);
	else
		FragColor = vec4( n * (z + 1.0) / (f + n - z * (f - n)) );
}

\gbuffer.fs
#version 330 core
in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_alpha_cutoff;

uniform sampler2D u_normal_texture;
uniform int u_has_normal_map;

uniform sampler2D u_metallic_roughness_texture;
uniform int u_has_metallic_roughness_map;
uniform float u_metallic_factor;
uniform float u_roughness_factor;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_perturbed_normal;
layout(location = 2) out vec4 out_geometric_normal;

// Helper function to build the TBN matrix using screen derivatives
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv)
{
	vec3 dp1 = dFdx(p);
	vec3 dp2 = dFdy(p);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);

	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
	return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	mat3 TBN = cotangent_frame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}

void main()
{
	vec4 color = u_color * texture(u_texture, v_uv);
	if (color.a < u_alpha_cutoff) discard;

	vec3 N_geo = normalize(v_normal);
	vec3 N_perturbed = N_geo;

	if(u_has_normal_map != 0)
	{
		vec3 normal_pixel = texture(u_normal_texture, v_uv).xyz;
		normal_pixel = normal_pixel * 2.0 - 1.0;
		N_perturbed = perturbNormal(v_normal, v_world_position, v_uv, normal_pixel);
	}

	out_albedo = color;
	out_perturbed_normal = vec4(N_perturbed * 0.5 + 0.5, 1.0);
	out_geometric_normal = vec4(N_geo * 0.5 + 0.5, 1.0);

	float ao = 1.0;
    float roughness = u_roughness_factor;
    float metallic = u_metallic_factor;

    if (u_has_metallic_roughness_map != 0) {
        vec3 mr_sample = texture(u_metallic_roughness_texture, v_uv).rgb;
        ao = mr_sample.r;         // R: baked ambient occlusion
        roughness = mr_sample.g;  // G: roughness
        metallic = mr_sample.b;   // B: metalness
    }

    out_albedo = vec4(color.rgb, ao); // Store AO in albedo alpha
    out_perturbed_normal = vec4(N_perturbed * 0.5 + 0.5, roughness); // Store Roughness here
    out_geometric_normal = vec4(N_geo * 0.5 + 0.5, metallic);         // Store Metalness here
}



\deferred.fs

#version 330 core

in vec2 v_uv;

// G-Buffer Textures
uniform sampler2D u_color_texture;         // RGB: Albedo, A: Ambient Occlusion (AO)
uniform sampler2D u_normal_texture;        // RGB: Perturbed Normal, A: Roughness
uniform sampler2D u_geo_normal_texture;    // RGB: Geometric Normal, A: Metalness
uniform sampler2D u_depth_texture;         // Depth buffer

// Camera matrices and positions
uniform mat4 u_inverse_viewprojection;
uniform vec3 u_camera_position;
uniform vec3 u_ambient_light;

// Analytical Light Uniforms
#define MAX_LIGHTS 4
uniform int u_num_lights;
uniform int u_light_types[MAX_LIGHTS];
uniform vec3 u_light_positions[MAX_LIGHTS];
uniform vec3 u_light_directions[MAX_LIGHTS];
uniform vec3 u_light_colors[MAX_LIGHTS];
uniform float u_light_intensities[MAX_LIGHTS];

// Output channel
out vec4 FragColor;

// --- Cook-Torrance PBR Constants & Functions ---
const float PI = 3.14159265359;

// 1. Trowbridge-Reitz GGX Normal Distribution Function (D)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.0001); // Safe delta
}

// 2. Geometry Schlick-GGX Sub-function (G1)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, 0.0001);
}

// 3. Smith's Method for Geometry Shadowing (G)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// 4. Fresnel Equation using Schlick's Approximation (F)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // --- Step 1: Read Depth and Reconstruct World Position ---
    float depth = texture(u_depth_texture, v_uv).x;
    
    // If it's the background skybox/clear color depth value, discard the fragment
    if (depth >= 1.0) {
        discard;
    }

    // Turn screen-space UV coordinates back into World Space Positions
    vec4 screen_pos = vec4(v_uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world_pos = u_inverse_viewprojection * screen_pos;
    vec3 WP = world_pos.xyz / world_pos.w;

    // --- Step 2: Unpack G-Buffer Channels ---
    vec4 albedo_ao_sample = texture(u_color_texture, v_uv);
    vec4 norm_rough_sample = texture(u_normal_texture, v_uv);
    vec4 geo_metal_sample = texture(u_geo_normal_texture, v_uv);

    // Color/Albedo properties and embedded Material Properties
    vec3 albedo = albedo_ao_sample.rgb;
    float ao = albedo_ao_sample.a;
    float roughness = norm_rough_sample.a;
    float metallic = geo_metal_sample.a;

    // Reconstruct Normal Vectors from standard [0, 1] texture range back to [-1, 1]
    vec3 N = normalize(norm_rough_sample.xyz * 2.0 - 1.0);
    vec3 N_geo = normalize(geo_metal_sample.xyz * 2.0 - 1.0);

    // Clamp parameters slightly to maintain microfacet evaluation stability
    roughness = clamp(roughness, 0.05, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    // Camera view vector computation
    vec3 V = normalize(u_camera_position - WP);
    float NdotV = max(dot(N, V), 0.0);

    // Surface base reflectivity (0.04 for dielectric surfaces, albedo for metal conductors)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 total_direct_lighting = vec3(0.0);

    // --- Step 3: Global Scene Lighting Iteration Loop ---
    for (int i = 0; i < u_num_lights; ++i) {
        vec3 L;
        float attenuation = 1.0;

        if (u_light_types[i] == 0) { // Point Light
            L = u_light_positions[i] - WP;
            float distance = length(L);
            L = normalize(L);
            attenuation = 1.0 / (distance * distance + 0.001);
        } 
        else if (u_light_types[i] == 1) { // Spot Light
            L = u_light_positions[i] - WP;
            float distance = length(L);
            L = normalize(L);
            attenuation = 1.0 / (distance * distance + 0.001);
            
            float light_to_pixel_angle = dot(L, normalize(u_light_directions[i] * -1.0));
            float cosine_cutoff = 0.92; // Match spot threshold criteria boundary
            if (light_to_pixel_angle < cosine_cutoff) {
                attenuation = 0.0;
            }
        } 
        else if (u_light_types[i] == 2) { // Directional Light
            // FIX 1: Ensure direction vector points FROM the surface TO the light source
            L = normalize(u_light_directions[i] * -1.0);
            attenuation = 1.0; 
        }

        float NdotL = max(dot(N, L), 0.0);
        
        // FIX 2: Relax or remove the geometric normal check for directional lights
        // Some frameworks populate out_geo_normal with unperturbed attributes that flip alignment 
        // on flat background/infinite planes, causing NdotL_geo to evaluate to 0.0 and kill the light.
        float NdotL_geo = (u_light_types[i] == 2) ? 1.0 : max(dot(N_geo, L), 0.0);

        if (NdotL > 0.0 && NdotL_geo > 0.0) {
            float shadow_factor = 1.0; 
            vec3 light_radiance = u_light_colors[i] * u_light_intensities[i] * attenuation * shadow_factor;

            vec3 H = normalize(V + L);

            // Cook-Torrance Microfacet Evaluations
            float D = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 numerator = D * G * F;
            float denominator = 4.0 * NdotV * NdotL;
            vec3 specular = numerator / max(denominator, 0.001); 

            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic; 

            total_direct_lighting += (kD * albedo / PI + specular) * light_radiance * NdotL;
        }
    }

    // --- Step 4: Ambient Indirect Setup ---
    vec3 ambient = u_ambient_light * albedo * ao;

    // Combine Accumulations
    vec3 final_color = ambient + total_direct_lighting;

    // Output Final Color
    FragColor = vec4(final_color, 1.0);
}

\lightvolume.fs
#version 330 core
uniform sampler2D u_color_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_depth_texture;
uniform mat4 u_inverse_viewprojection;

uniform vec3 u_camera_position;
uniform vec2 u_screen_size;

// Individual light arrays from uploadLights
uniform vec3 u_light_positions[1];
uniform vec3 u_light_colors[1];
uniform float u_light_intensities[1];
uniform vec3 u_light_directions[1];
uniform int u_light_types[1];
uniform vec2 u_light_cones[1];

// Localized single active shadow uniforms
uniform mat4 u_light_viewprojection;
uniform sampler2D u_shadow_map;
uniform int u_cast_shadows;
uniform float u_shadow_bias;

out vec4 FragColor;

void main()
{
	vec2 uv = gl_FragCoord.xy / u_screen_size;
	float depth = texture(u_depth_texture, uv).x;
	if (depth >= 1.0) discard;

	vec4 albedo = texture(u_color_texture, uv);
	vec3 N = normalize(texture(u_normal_texture, uv).xyz * 2.0 - 1.0);

	vec4 screen_pos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec4 world_pos = u_inverse_viewprojection * screen_pos;
	vec3 WP = world_pos.xyz / world_pos.w;

	vec3 L_vec = u_light_positions[0] - WP;
	float dist = length(L_vec);
	vec3 L = normalize(L_vec);

	float attenuation = 1.0 / (1.0 + dist * dist);

	if(u_light_types[0] == 2)
	{
		vec3 D = normalize(u_light_directions[0]);
		float cos_angle = dot(D, L);
		float spot_factor = smoothstep(u_light_cones[0].y, u_light_cones[0].x, cos_angle);
		attenuation *= spot_factor;
	}

	// --- SINGLE VOLUME SHADOW CALCULATION ---
	float shadow_factor = 1.0;
	if(u_cast_shadows != 0)
	{
		vec4 light_clip_pos = u_light_viewprojection * vec4(WP, 1.0);
		vec3 proj_coords = light_clip_pos.xyz / light_clip_pos.w;
		proj_coords = proj_coords * 0.5 + 0.5;

		if(proj_coords.x >= 0.0 && proj_coords.x <= 1.0 &&
		   proj_coords.y >= 0.0 && proj_coords.y <= 1.0)
		{
			float current_depth = proj_coords.z;
			float closest_depth = texture(u_shadow_map, proj_coords.xy).r;

			if(current_depth > closest_depth + u_shadow_bias)
			{
				shadow_factor = 0.0;
			}
		}
	}

	float NdotL = max(dot(N, L), 0.0);
	float micro_shadow = clamp(NdotL * 4.0, 0.0, 1.0); 

	vec3 light_energy = u_light_colors[0] * u_light_intensities[0] * attenuation * shadow_factor;
	vec3 lighting = albedo.xyz * light_energy * NdotL * micro_shadow;

	FragColor = vec4(lighting, 1.0);
}

\instanced.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;

in mat4 u_model;

uniform vec3 u_camera_pos;

uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}


\lighting.fs

#version 330 core

// From basic.vs
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

// Material uniforms
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_roughness;
uniform float u_alpha_cutoff;

// Camera uniform
uniform vec3 u_camera_position;

// Light Uniforms we just set for Assignment 2
uniform int u_num_lights;
uniform vec3 u_light_positions[10];
uniform vec3 u_light_colors[10];
uniform float u_light_intensities[10];
uniform vec3 u_light_directions[10];
uniform int u_light_types[10];
uniform vec2 u_light_cones[10];

// New Uniforms for normal mapping
uniform sampler2D u_normal_texture;
uniform int u_has_normal_map;

//Shadow map uniforms
uniform mat4 u_light_viewprojections[4];
uniform sampler2D u_shadow_maps[4];
uniform int u_cast_shadows[4];
uniform float u_shadow_bias;

// Updating the shininess here
uniform float u_shininess;

out vec4 FragColor;

mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv)
{
	// get edge vectors of the pixel triangle
	vec3 dp1 = dFdx(p);
	vec3 dp2 = dFdy(p);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);

	// solve the linear system
	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	// construct a scale-invariant frame 
	float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
	return mat3(T * invmax, B * invmax, N);
}

// assume N, the interpolated vertex normal and 
// WP the world position
vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	mat3 TBN = cotangent_frame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}

void main()
{
	// We prepare the vectors for Phong - N, V
	vec3 N_geo = normalize(v_normal);								// Normal vector, so direction the surface is "facing"
	vec3 N = N_geo;
	
	if(u_has_normal_map != 0)
	{
		// Get normal from texture (always 0 to 1 range)
		vec3 normal_pixel = texture(u_normal_texture, v_uv).xyz;

		// Remap to -1 to 1 range
		normal_pixel = normal_pixel * 2.0 - 1.0;

		// Perturb the geometric normal_pixel
		N = perturbNormal(v_normal, v_world_position, v_uv, normal_pixel);
	}
	
	
	vec3 V = normalize(u_camera_position - v_world_position);	// The direction from the pixel on the objects surface towards the camera.

	// Get base texture color
	vec4 tex_color = texture(u_texture, v_uv);					// Getting color of the texture
	vec3 base_color = u_color.rgb * tex_color.rgb;				// calculating base color

	// Alpha test (from Assignment 1)
	if(u_color.a * tex_color.a < u_alpha_cutoff)
		discard;

	// Ambient component 
	vec3 ambient = base_color * 0.1; // 0.1 is adjustable but used for a low light.

	// Accumulator for direct light
	vec3 total_direct_light = vec3(0.0);

	// Calculate Phong Shininess from Roughness
	// High roughness (1.0) -> low power (dull)
	// low roughness (0.0) -> high power (shiny)
	// float shininess = pow(2.0, (1.0 - u_roughness) * 10.0);

	// Loop through lights
	for(int i = 0; i < u_num_lights; i++)
	{
		// as we need to switch between light types, we will only initialize a few things
		vec3 L;
		float attenuation = 1.0;

		float shadow_factor = 1.0; // Standard: no shadow

        // Calculating shadow
if(i < 4 && u_cast_shadows[i] != 0)
{
    // Convert world space to homogeneous space
    vec4 light_clip_pos =
        u_light_viewprojections[i]
        * vec4(v_world_position, 1.0);

    // Perspective division
    vec3 proj_coords =
        light_clip_pos.xyz / light_clip_pos.w;

    // Transform from Clip Space [-1,1]
    // to Texture Space [0,1]
    proj_coords = proj_coords * 0.5 + 0.5;

    // Only calculate when inside shadow map
    if(proj_coords.x >= 0.0 && proj_coords.x <= 1.0 &&
       proj_coords.y >= 0.0 && proj_coords.y <= 1.0)
    {
        float current_depth = proj_coords.z;

        float closest_depth =
            texture(
                u_shadow_maps[i],
                proj_coords.xy
            ).r;

        

        if(current_depth > closest_depth + u_shadow_bias)
        {
            shadow_factor = 0.0;
        }
    }
}
		
		if(u_light_types[i] == 1) // Point light
		{
			vec3 L_vec = u_light_positions[i] - v_world_position;
			float dist = length(L_vec);
			L = normalize(L_vec); // normalize after getting distance

			// Attenuation (Light intensity falls off with distance squared) Works only for point lights like that
			attenuation = 1.0 / (1.0 + dist * dist);
		}
		else if(u_light_types[i] == 2) // Spot light
		{
			vec3 L_vec = u_light_positions[i] - v_world_position;
			float dist = length(L_vec);
			L = normalize(L_vec);

			// Distance falloff is the same as Point Light
			attenuation = 1.0 / (1.0 + dist * dist);

			// Cone Falloff
			vec3 D = normalize(u_light_directions[i]);
			float cos_angle = dot(D, L); //L is the direction from Light to pixel

			// Interpolate between inner and outer cone
			float spot_factor = smoothstep(u_light_cones[i].y, u_light_cones[i].x, cos_angle);
			attenuation *= spot_factor;
		}
		else if(u_light_types[i] == 3) // Directional light
		{
			// This looks correct while executed

			// L is the direction towards the light source.
			// We negate the light's front vector.
			L = normalize(u_light_directions[i] * -1.0);

			// Directional do not attenuate
			attenuation = 1.0;
		}


        // We multiply the lightenergy with shadow_factor
        vec3 light_energy = u_light_colors[i] * u_light_intensities[i] * attenuation * shadow_factor;

		// Diffuse (Lambert)
		float NdotL = max(0.0, dot(N, L));
		float NdotL_geo = max(0.0, dot(N_geo, L)); // Physical limit
		vec3 diffuse = (NdotL * NdotL_geo) * light_energy;

		// Specular (PHONG)
		//spec_strenth controls intensity independent of shape, makes it less camera dependent
		float spec_strength = 1.0 - u_roughness;
		vec3 R = reflect(-L, N);
		float RdotV = max(0.0, dot(R, V));
		float spec_factor = pow(RdotV, u_shininess);

		//multiplying base_color tints the color of the light 
		//vec3 specular = (NdotL_geo > 0.0) ? (spec_factor * spec_strength * light_energy * base_color) : vec3(0.0);

		vec3 specular = (NdotL_geo > 0.0) ? (spec_factor * spec_strength * light_energy) : vec3(0.0);

		total_direct_light += (diffuse * base_color) + specular;
	}

	// Final Color
	FragColor = vec4(ambient + total_direct_light, u_color.a * tex_color.a);
}

\plain.fs

#version 330 core

out vec4 FragColor;

void main(){
FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}

\lighting_PBR.fs

#version 330 core

// Material attributes passed from vertex shader
in vec3 v_position;
in vec3 v_normal;
in vec2 v_uv;

// Camera and scene variables
uniform vec3 u_camera_position;
uniform vec3 u_ambient_light;

// Material factors
uniform vec4 u_color;
uniform float u_metallic_factor;
uniform float u_roughness_factor;

// Texture uniforms
uniform sampler2D u_texture; // Albedo
uniform sampler2D u_normal_texture;
uniform sampler2D u_metallic_roughness_texture;

uniform bool u_has_texture;
uniform bool u_has_normal_map;
uniform bool u_has_metallic_roughness_map;

// Uniforms for light configurations
#define MAX_LIGHTS 4
uniform int u_num_lights;
uniform int u_light_types[MAX_LIGHTS];
uniform vec3 u_light_positions[MAX_LIGHTS];
uniform vec3 u_light_directions[MAX_LIGHTS];
uniform vec3 u_light_colors[MAX_LIGHTS];
uniform float u_light_intensities[MAX_LIGHTS];

// Output
out vec4 FragColor;

const float PI = 3.14159265359;

// 1. Trowbridge-Reitz GGX Normal Distribution Function (D)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.0001);
}

// 2. Geometry Schlick-GGX Sub-function (G1)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, 0.0001);
}

// 3. Smith's Method for Geometry Shadowing (G)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// 4. Fresnel Equation using Schlick's Approximation (F)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cotangent frame normal perturbation helper 
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
    return mat3(T * invmax, B * invmax, N);
}

void main() {
    // --- Step 1: Resolve Base Material Parameters ---
    vec4 albedo_sample = u_has_texture ? texture(u_texture, v_uv) : vec4(1.0);
    vec4 albedo = albedo_sample * u_color;
    
    float ao = 1.0;
    float roughness = u_roughness_factor;
    float metallic = u_metallic_factor;
    
    if (u_has_metallic_roughness_map) {
        vec3 mr_sample = texture(u_metallic_roughness_texture, v_uv).rgb;
        ao = mr_sample.r;
        roughness = mr_sample.g;
        metallic = mr_sample.b;
    }
    
    roughness = clamp(roughness, 0.05, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    // --- Step 2: Set Up Normal & View Directions ---
    vec3 N = normalize(v_normal);
    if (u_has_normal_map) {
        vec3 tangent_normal = texture(u_normal_texture, v_uv).xyz * 2.0 - 1.0;
        mat3 TBN = cotangent_frame(N, v_position, v_uv);
        N = normalize(TBN * tangent_normal);
    }
    
    vec3 V = normalize(u_camera_position - v_position);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);
    vec3 total_direct_lighting = vec3(0.0);

    // --- Step 3: Direct Light Analytical Loop ---
    for (int i = 0; i < u_num_lights; ++i) {
        vec3 L;
        float attenuation = 1.0;

        if (u_light_types[i] == 1) { // Point Light (Notice match with your C++ framework types!)
            L = u_light_positions[i] - v_position;
            float distance = length(L);
            L = normalize(L);
            attenuation = 1.0 / (distance * distance + 0.001);
        } 
        else if (u_light_types[i] == 2) { // Spot Light
            L = u_light_positions[i] - v_position;
            float distance = length(L);
            L = normalize(L);
            attenuation = 1.0 / (distance * distance + 0.001);
            
            float light_to_pixel_angle = dot(L, normalize(u_light_directions[i] * -1.0));
            float cosine_cutoff = 0.92; 
            if (light_to_pixel_angle < cosine_cutoff) {
                attenuation = 0.0;
            }
        } 
        else if (u_light_types[i] == 3) { // Directional Light
            L = normalize(u_light_directions[i] * -1.0);
        }
        else {
            continue; // Unknown or disabled light type
        }

        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0) {
            vec3 light_radiance = u_light_colors[i] * u_light_intensities[i] * attenuation;

            float D = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 numerator = D * G * F;
            float denominator = 4.0 * NdotV * NdotL;
            vec3 specular = numerator / max(denominator, 0.001);

            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic;

            total_direct_lighting += (kD * albedo.rgb / PI + specular) * light_radiance * NdotL;
        }
    }

    // --- Step 4: Output Accumulation ---
    vec3 ambient = u_ambient_light * albedo.rgb * ao;
    vec3 linear_color = ambient + total_direct_lighting;

    // Reinhardt Tone Mapping to turn high HDR exposures back to screen-safe ranges [0, 1]
    vec3 mapped_color = linear_color / (linear_color + vec3(1.0));
    
    // Gamma Correction
    mapped_color = pow(mapped_color, vec3(1.0 / 2.2));

    FragColor = vec4(mapped_color, albedo.a);
}

\ssao.fs
#Version 330 core

in vec2 v_uv;
out float FragColor;

uniform sampler2D u_normal_texture;
uniform sampler2D u_depth_texture;

uniform mat4 u_viewprojection;
uniform mat4 u_inverse_viewprojection;

uniform vec3 u_samples[64];
uniform int u_num_samples;
uniform float u_radius;

void main() {
    // 1. Reconstruct current pixel's World Space position from Depth Buffer
    float depth = texture(u_depth_texture, v_uv).r;
    if (depth == 1.0) {
        FragColor = 1.0; // Skybox background remains fully unoccluded
        return;
    }
    
    // Transform coordinates from Screen UV space back to World Coordinates
    vec4 clip_space_pos = vec4(v_uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world_space_pos = u_inverse_viewprojection * clip_space_pos;
    vec3 world_pos = world_space_pos.xyz / world_space_pos.w;

    // 2. Fetch the G-Buffer world normal
    vec3 N = texture(u_normal_texture, v_uv).xyz * 2.0 - 1.0;
    N = normalize(N);

    // --- Step 2.4.1: Building the Per-Pixel Hemisphere Rotation TBN Matrix ---
    // Generate a quick per-pixel random pseudo-vector based on screen coordinates
    vec3 random_dir = normalize(vec3(sin(gl_FragCoord.x * 12.9898), cos(gl_FragCoord.y * 78.233), 0.0));
    vec3 T = normalize(random_dir - N * dot(random_dir, N));
    vec3 B = cross(N, T);
    mat3 rotmat = mat3(T, B, N);

    float occlusion = 0.0;
    int samples_checked = 0;

    // 3. Direct Loop sampling over kernel array
    for (int i = 0; i < u_num_samples; ++i) {
        // Rotate hemisphere position to point cleanly outward relative to surface normal orientation
        vec3 sample_offset = rotmat * u_samples[i];
        vec3 sample_world_pos = world_pos + (sample_offset * u_radius);

        // Project the 3D offset point back onto the 2D view screen coordinate coordinates
        vec4 sample_projected = u_viewprojection * vec4(sample_world_pos, 1.0);
        vec3 sample_ndc = sample_projected.xyz / sample_projected.w;
        vec2 sample_screen_uv = sample_ndc.xy * 0.5 + 0.5;

        // Clip checks to guarantee target coordinate lands inside the current display window bounds
        if (sample_screen_uv.x >= 0.0 && sample_screen_uv.x <= 1.0 && sample_screen_uv.y >= 0.0 && sample_screen_uv.y <= 1.0) {
            samples_checked++;

            // Extract the true geometric depth present at the projected location
            float real_buffer_depth = texture(u_depth_texture, sample_screen_uv).r;
            vec4 real_clip = vec4(sample_screen_uv * 2.0 - 1.0, real_buffer_depth * 2.0 - 1.0, 1.0);
            vec4 real_world = u_inverse_viewprojection * real_clip;
            vec3 real_world_pos = real_world.xyz / real_world.w;

            // --- Step 2.8: Range Checking (Included here for proper artifact prevention) ---
            float range_check = smoothstep(0.0, 1.0, u_radius / abs(world_pos.z - real_world_pos.z));

            // Compare the Z depths. If the real surface position is closer to the camera 
            // than our sample point, then our sample point is hidden inside geometry (occluded)
            if (real_world_pos.z > sample_world_pos.z + 0.005) {
                occlusion += 1.0 * range_check;
            }
        }
    }

    // Average the collected samples to calculate our final occlusion multiplier
    float ao_factor = 1.0 - (occlusion / float(max(samples_checked, 1)));
    FragColor = clamp(ao_factor, 0.0, 1.0);
}