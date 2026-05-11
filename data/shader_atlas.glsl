//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
lighting basic.vs lighting.fs

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

out vec4 FragColor;

void main()
{
	// We prepare the vectors for Phong - N, V
	vec3 N = normalize(v_normal);								// Normal vector, so direction the surface is "facing"
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
	float shininess = pow(2.0, (1.0 - u_roughness) * 10.0);

	// Loop through lights
	for(int i = 0; i < u_num_lights; i++)
	{
		// as we need to switch between light types, we will only initialize a few things
		vec3 L;
		float attenuation = 1.0;
		
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
			// proper spotlight
			vec3 L_vec = u_light_positions[i] - v_world_position;
			float dist = length(L_vec);
			L = normalize(L_vec);

			// Distance falloff is the same as Point Light
			attenuation = 1.0 / (1.0 + dist * dist);

			// Cone Falloff
			vec3 D = normalize(u_light_directions[i]);
			float cos_angle = dot(D, L); //-L is the direction from Light to pixel

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


		vec3 light_energy = u_light_colors[i] * u_light_intensities[i] * attenuation;

		// Diffuse (Lambert)
		float NdotL = max(0.0, dot(N, L));
		vec3 diffuse = NdotL * light_energy;

		// Specular (PHONG)
		//spec_strenth controls intensity independent of shape, makes it less camera dependent
		float spec_strength = 1.0 - u_roughness;
		vec3 R = reflect(-L, N);
		float RdotV = max(0.0, dot(R, V));
		float spec_factor = pow(RdotV, shininess);

		//multiplying base_color tints the color of the light 
		vec3 specular = (NdotL > 0.0) ? (spec_factor * spec_strength * light_energy * base_color) : vec3(0.0);


		total_direct_light += (diffuse * base_color) + specular;
	}

	// Final Color
	FragColor = vec4(ambient + total_direct_light, u_color.a * tex_color.a);
}