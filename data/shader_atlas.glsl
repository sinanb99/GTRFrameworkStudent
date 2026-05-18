//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
lighting basic.vs lighting.fs
multi basic.vs multi.fs
plain basic.vs plain.fs
lighting basic.vs lighting.fs
<<<<<<< HEAD
lighting_PBR basic.vs lighting_PBR.fs
=======
gbuffer basic.vs gbuffer.fs
deferred quad.vs deferred.fs
lightvolume basic.vs lightvolume.fs
>>>>>>> origin/Assignment_4

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
uniform bool u_has_normal_map;

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

	if(u_has_normal_map)
	{
		vec3 normal_pixel = texture(u_normal_texture, v_uv).xyz;
		normal_pixel = normal_pixel * 2.0 - 1.0;
		N_perturbed = perturbNormal(v_normal, v_world_position, v_uv, normal_pixel);
	}

	out_albedo = color;
	out_perturbed_normal = vec4(N_perturbed * 0.5 + 0.5, 1.0);
	out_geometric_normal = vec4(N_geo * 0.5 + 0.5, 1.0);
}

\deferred.fs
#version 330 core
in vec2 v_uv;

uniform sampler2D u_color_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_geo_normal_texture; 
uniform sampler2D u_depth_texture;
uniform mat4 u_inverse_viewprojection;

uniform vec3 u_ambient_light;
uniform vec3 u_camera_position;

uniform int u_num_lights;
uniform vec3 u_light_directions[10];
uniform vec3 u_light_colors[10];
uniform float u_light_intensities[10];

out vec4 FragColor;

void main()
{
	float depth = texture(u_depth_texture, v_uv).x;
	if (depth >= 1.0) discard;

	vec4 albedo = texture(u_color_texture, v_uv);
	
	// Unpack both vectors properly from their native targets
	vec3 N = normalize(texture(u_normal_texture, v_uv).xyz * 2.0 - 1.0);
	vec3 N_geo = normalize(texture(u_geo_normal_texture, v_uv).xyz * 2.0 - 1.0);

	// Reconstruct 3D Position
	vec4 screen_pos = vec4(v_uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec4 world_pos = u_inverse_viewprojection * screen_pos;
	vec3 WP = world_pos.xyz / world_pos.w;

	vec3 V = normalize(u_camera_position - WP);

	vec3 ambient = albedo.xyz * u_ambient_light;
	vec3 total_direct_light = vec3(0.0);

	for (int i = 0; i < u_num_lights; i++)
	{
		vec3 L = normalize(u_light_directions[i] * -1.0);

		// --- IMPLEMENTED EXACTLY LIKE YOUR LIGHTING.FS ---
		// 1. Diffuse (Lambert) using normal map
		float NdotL = max(0.0, dot(N, L));
		
		// 2. Physical geometric limit using true mesh geometry
		float NdotL_geo = max(0.0, dot(N_geo, L)); 
		
		// Combine them together to naturally gate light-leaks without a manual threshold
		vec3 diffuse = (NdotL * NdotL_geo) * (u_light_colors[i] * u_light_intensities[i]);

		// 3. Specular (Phong)
		float spec_strength = 0.5; // Baseline fallback fallback value for deferred channel mapping
		vec3 R = reflect(-L, N);
		float RdotV = max(0.0, dot(R, V));
		float spec_factor = pow(RdotV, 32.0); // Clean shininess match
		
		// Only calculate specular highlights if the real geometry plane faces the light source
		vec3 specular = (NdotL_geo > 0.0) ? (spec_factor * spec_strength * (u_light_colors[i] * u_light_intensities[i])) : vec3(0.0);

		total_direct_light += (diffuse * albedo.xyz) + specular;
	}

	FragColor = vec4(ambient + total_direct_light, 1.0);
}


\lightvolume.fs
#version 330 core
uniform sampler2D u_color_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_depth_texture;
uniform mat4 u_inverse_viewprojection;

uniform vec3 u_camera_position;
uniform vec2 u_screen_size;

// These MUST be arrays of size 1 to receive data from uploadLights!
uniform vec3 u_light_positions[1];
uniform vec3 u_light_colors[1];
uniform float u_light_intensities[1];
uniform vec3 u_light_directions[1];
uniform int u_light_types[1];
uniform vec2 u_light_cones[1];

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

	// --- LOCAL LIGHT VALVE FIX ---
	float NdotL = max(dot(N, L), 0.0);
	
	// Soft self-shadowing factor removes lighting leaks on steep geometry angles
	float micro_shadow = clamp(NdotL * 4.0, 0.0, 1.0); 

	vec3 light_energy = u_light_colors[0] * u_light_intensities[0] * attenuation;
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
uniform bool u_has_normal_map;

//Shadow map uniforms
uniform mat4 u_light_viewprojections[4];
uniform sampler2D u_shadow_maps[4];
uniform bool u_cast_shadows[4];
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
	
	if(u_has_normal_map)
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
if(i < 4 && u_cast_shadows[i])
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

// From basic.vs
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

// Material uniforms
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_roughness;
uniform float u_alpha_cutoff;

// New PBR Uniforms
uniform sampler2D u_albedo_texture;
uniform sampler2D u_metallic_roughness_texture;
uniform bool u_has_metallic_roughness;

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
uniform bool u_has_normal_map;

//Shadow map uniforms
uniform mat4 u_light_viewprojections[4];
uniform sampler2D u_shadow_maps[4];
uniform bool u_cast_shadows[4];
uniform float u_shadow_bias;



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
	
	if(u_has_normal_map)
	{
		// Get normal from texture (always 0 to 1 range)
		vec3 normal_pixel = texture(u_normal_texture, v_uv).xyz;

		// Remap to -1 to 1 range
		normal_pixel = normal_pixel * 2.0 - 1.0;

		// Perturb the geometric normal_pixel
		//N = perturbNormal(v_normal, v_world_position, v_uv, normal_pixel);
		N = normalize(perturbNormal(N_geo, v_world_position, v_uv, normal_pixel)
);
	}
	
	
	vec3 V = normalize(u_camera_position - v_world_position);	// The direction from the pixel on the objects surface towards the camera.

	//Prepare metallic and roughness values
	float metallic = 0.0;
	float roughness = u_roughness;

	if(u_has_metallic_roughness)
	{
		vec4 mr_sample = texture(u_metallic_roughness_texture, v_uv);

		roughness = mr_sample.g;
		metallic = mr_sample.b;
	}

	// Get base texture color
	vec4 albedo_sample = texture(u_albedo_texture, v_uv);					// Getting color of the texture
	vec3 albedo = albedo_sample.rgb * u_color.rgb;				// calculating base color

	// Alpha test
	if(albedo_sample.a * u_color.a < u_alpha_cutoff)
    discard;

	// Ambient component 
	vec3 ambient = albedo * 0.1; // 0.1 is adjustable but used for a low light.

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
if(i < 4 && u_cast_shadows[i])
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

		// Specular (PBR)	
		//Fresnel term using Schlick's approximation
		vec3 F0 = mix(vec3(0.04), albedo, metallic); 

		vec3 H = normalize(V + L);

		float NdotV = max(dot(N, V), 0.0);
		float NdotH = max(dot(N, H), 0.0);
		float HdotV = max(dot(H, V), 0.0);


		// Fresnel
		vec3 F = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);

		// Simple roughness distribution
		float spec_power = mix(256.0, 2.0, roughness);
		float D = pow(NdotH, spec_power);

		// Geometry approximation
		float G = NdotL * NdotV;

		// Cook-Torrance
		vec3 specular =
			(F * D * G) /
			max(4.0 * NdotL * NdotV, 0.001);

		vec3 kd = (1.0 - F) * (1.0 - metallic);

		vec3 diffuse_pbr = kd * albedo / 3.14159;

		total_direct_light += (diffuse_pbr + specular) * light_energy * NdotL;
		}

			vec3 final_color = ambient + total_direct_light;

		FragColor = vec4(final_color, albedo_sample.a * u_color.a);

}