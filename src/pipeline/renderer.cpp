#include "renderer.h"

#include <algorithm> //sort

#include <vector>
#include <cmath>
#include <string>


#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"
#include "scene.h"


using namespace SCN;


// Light intensity is part of the framework already somewhere. Same with color
// Normal vector is important because we then know where the light hits the mesh (maybe even reflections)
// Normalize a vectore ONLY if a value is a direction (COlor of the surface is a physical quantity, so we 
// dont normalize that)
// Dot product, to measure the difference between directions. (The higher, the closer is the direction) 
// -> clamp negative values (because we dont compute negative light)
// reflect -> order is important
// Ambient contribution is the same for the whole scene, but not the others change for every pixel.
// 
// 
// Be careful about OPENGL because it changes the global config
// use the fixed pipeline of OPenGL for rendering a mesh
// -> we are only changing code for the vertex shader and the fragment shader
// 
// Uniform we send with setUniform 
// 
// 
// for phong implementation -> try to use float numbers at some point for computational and detail reasons
// 
// No attenuation fordirectional light but we are doing it for the L vector (use LightEntity's Front)
// 
// Take front vector and light vector. Cosine magic -> to determine the degree of the cone of light.
// But we want it to be a bit diffused, for softer borders and a more realistic look hence we add another
// parameter.-> alpha_max as full spotlight angle
// alpha_min as heart of the cone of light
// and inbetween the hard light and the soft light (alpha max vs. alpha min) we have the falloff)
// 
// Always make sure that you are using the same length in the arrays (for GPU purposes) 
// 
// Look at extra assignments for a better grade. 
//

//some globals
GFX::Mesh sphere;


// This is our struct that describes the renderables. 
struct sRenderable
{
	GFX::Mesh* mesh; // Pointer to the Vertex, or Index Buffer. This is the "what" of the object (3D coordiantes, normals,...)
	SCN::Material* material; // This defines Shaders and Textures, tells GPU how to interpret light, color, and roughness
	Matrix44 model; // The transfomration to move from object space to world space
	float distance; // If the material is see-through, we must order by distance. (Z-Direction)
};

std::vector<sRenderable> render_list; // render_list that includes everything that is to be rendered.
std::vector<sRenderable> opaque_list; // only not see through things
std::vector<sRenderable> transparent_list; // See through things, ordered the other way around

// Phong Lighting
std::vector<LightEntity*> lights_list;


Renderer::Renderer(const char* shader_atlas_filename, int width, int height)
{
	render_wireframe = false;
	render_boundaries = false;
	render_mode = SINGLE_PASS; // Initialize it to single pass by default
	scene = nullptr;
	skybox_cubemap = nullptr;
	screen_width = width;
	screen_height = height;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();

	// G-Buffer: color target 0 = albedo, color target 1 = packed normals
	gbuffer_fbo = new GFX::FBO();
	gbuffer_fbo->create(width, height, 3, GL_RGBA, GL_UNSIGNED_BYTE, true);

	// Light FBO
	light_fbo = new GFX::FBO();
	light_fbo->create(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, true);
	shadow_light_index = 1;

	// 3.1 — Depth-only FBO, 1024x1024
	//shadow_fbo = new GFX::FBO();
	//shadow_fbo->setDepthOnly(1024, 1024);
	// Initialize the array to null
	for (int i = 0; i < 4; ++i) {
		shadow_fbos[i] = nullptr;
	}

	for (int i = 0; i < 4; ++i) {
		shadow_fbos[i] = new GFX::FBO();
		shadow_fbos[i]->setDepthOnly(1024, 1024);
	}
	light_camera = new Camera();
}

void Renderer::setupScene()
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;
}

/** * Recursively flattens the scene hierarchy into a linear render list.
 * Transforms local node coordinates into World Space for the GPU.
 */
void addSubtree(Node* node) {
	if (!node) return;
	render_list.push_back({
		.mesh = node->mesh,
		.material = node->material,
		.model = node->getGlobalMatrix()
		});
	for (Node* child : node->children) addSubtree(child);
}

void parseNode(Node* node, Camera* cam) {
	if (!node) {
		return;
	}

	if (node->mesh) {
		BoundingBox worldBox = transformBoundingBox(node->getGlobalMatrix(), node->mesh->box);
		if (cam)
		{
			char res = cam->testBoxInFrustum(worldBox.center, worldBox.halfsize);

			if (res == CLIP_OUTSIDE)
				return;

			if (res == CLIP_INSIDE)
			{
				addSubtree(node);
				return;
			}
		}
	}

	render_list.push_back({
		.mesh = node->mesh,
		.material = node->material,
		.model = node->getGlobalMatrix()
		});

	for (Node* child : node->children) {
			parseNode(child, cam);  // Iterating through children
		}
}


void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) {
	render_list.clear();
	opaque_list.clear();
	transparent_list.clear();
	lights_list.clear();


	
	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];

		if (!entity->visible) {
			continue;
		}


		// If the entity is a Prefab, cast it and traverse its internal scene graph for rendering. 
		if (entity->getType() == eEntityType::PREFAB) {

			PrefabEntity* e = (PrefabEntity*)entity;

			parseNode(&(entity->root), cam);

		}

		// Here we get the lights list, we do it just like the PREFABs -> Now we can work with the lights list.
		if (entity->getType() == eEntityType::LIGHT) {

			LightEntity* light = (LightEntity*)entity; // Here we are specifically telling the compiler that we are working with a LightEntity so we have access to other values.

			lights_list.push_back(light); // Push back the light we just established into a list. 
		}

	}

	// For loop to implement transparent and opaque list
	for (sRenderable& r : render_list) {
		//compute distance from camera to object using model translation
		if (cam)
			r.distance = r.model.getTranslation().distance(cam->eye);
		else
			r.distance = 0.0f;

		// alpha cutoff as opaque and BLEND as transparent
		// Check for transparency -> add to list, else opaque
		if (r.material && r.material->isTransparent())
			transparent_list.push_back(r);
		else
			opaque_list.push_back(r);
		
	}


		// Store Lights
		// ...
	
	
}

void Renderer::renderDeferred(SCN::Scene* scene, Camera* camera)
{
	renderGBuffer(camera);
	renderDeferredAmbientAndDirectional(camera);
	renderLightVolumes(camera);
	renderTransparencies(camera);

	// Present accumulation frame buffer directly onto screen viewport
	light_fbo->color_textures[0]->toViewport();
}


void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();


	//Camera updates are processed here before rendering

	camera->updateViewMatrix();
	camera->updateProjectionMatrix();
	camera->extractFrustum();

	parseSceneEntities(scene, camera);

	renderShadowMap(scene);

	if (use_deferred)
		renderDeferred(scene, camera);
	else
	{
		renderForward(scene, camera);
	}
} 

void Renderer::renderForward(SCN::Scene * scene, Camera * camera) {

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (skybox_cubemap)
		renderSkybox(skybox_cubemap);


	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);


	// Opaque pass instead of just calling a material.
	for (sRenderable& opaque_call : opaque_list) {
		renderMeshWithMaterial(opaque_call.model, opaque_call.mesh, opaque_call.material);
	}

	// Transparency pass and resort first 
	std::sort(transparent_list.begin(), transparent_list.end(),
		[](const sRenderable& a, const sRenderable& b) {return a.distance > b.distance; });
	
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	for (const sRenderable& call : transparent_list) {
		renderMeshWithMaterial(call.model, call.mesh, call.material);
	}

	// Restore defaults
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);


}


// G-Buffer renderer for Assignment 4
void Renderer::renderGBuffer(Camera* camera)
{
	gbuffer_fbo->bind();
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	GFX::Shader* shader = GFX::Shader::Get("gbuffer");
	if (!shader) {
		gbuffer_fbo->unbind();
		return;
	}

	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	// Pass extra parameters your gbuffer.fs might expect
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_time", (float)getTime());

	for (sRenderable& call : opaque_list) {
		if (!call.mesh || !call.material) continue;
		if (call.material->alpha_mode == SCN::eAlphaMode::BLEND) continue; // Skip transparency

		// Set base transforms and colors
		shader->setUniform("u_model", call.model);
		shader->setUniform("u_color", call.material->color);
		shader->setUniform("u_alpha_cutoff",
			call.material->alpha_mode == SCN::eAlphaMode::MASK ? call.material->alpha_cutoff : 0.001f); //

		// 1. Bind the Albedo (Color) Texture to slot 0
		GFX::Texture* albedo_tex = call.material->textures[SCN::eTextureChannel::ALBEDO].texture;
		if (!albedo_tex) albedo_tex = GFX::Texture::getWhiteTexture(); // fallback
		shader->setUniform("u_texture", albedo_tex, 0);

		// 2. Bind the Normal Map Texture to slot 1 and flag the shader
		GFX::Texture* normal_tex = call.material->textures[SCN::eTextureChannel::NORMALMAP].texture;
		if (normal_tex) {
			shader->setUniform("u_normal_texture", normal_tex, 1); // Bind normal map to texture unit 1
			shader->setUniform("u_has_normal_map", (int)1);
		}
		else {
			shader->setUniform("u_has_normal_map", (int)(0));
		}

		// Handle two-sided rendering if specified by the material
		if (call.material->two_sided) glDisable(GL_CULL_FACE);
		else glEnable(GL_CULL_FACE);

		// Render the geometry
		call.mesh->render(GL_TRIANGLES);
	}

	shader->disable();
	glEnable(GL_CULL_FACE); // restore defaults
	gbuffer_fbo->unbind();
}

void Renderer::renderDeferredAmbientAndDirectional(Camera* camera)
{
	// Blending and depth for the base pass mapping inside the light_fbo
	gbuffer_fbo->depth_texture->copyTo(light_fbo->depth_texture);

	light_fbo->bind();
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	if (skybox_cubemap) renderSkybox(skybox_cubemap);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);

	GFX::Shader* shader = GFX::Shader::Get("deferred");
	if (!shader) {
		light_fbo->unbind();
		return;
	}

	shader->enable();

	// Bind depth map and properties for screen-space coordinate evaluations
	shader->enable();
	shader->setUniform("u_color_texture", gbuffer_fbo->color_textures[0], 0);
	shader->setUniform("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
	shader->setUniform("u_geo_normal_texture", gbuffer_fbo->color_textures[2], 3); // Bind to unit 3
	shader->setUniform("u_depth_texture", gbuffer_fbo->depth_texture, 2);

	shader->setUniform("u_inverse_viewprojection", camera->inverse_viewprojection_matrix);
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_shadow_bias", shadow_bias);

	// Group and pass directional light vectors down
	std::vector<LightEntity*> dir_lights;
	for (auto* l : lights_list) {
		if (l->light_type == eLightType::DIRECTIONAL) dir_lights.push_back(l);
	}
	uploadLights(shader, dir_lights);

	// Bind shadow maps for directional
	for (int i = 0; i < dir_lights.size(); ++i) {
		if (i >= 4) break;

		if (shadow_fbos[i] && shadow_fbos[i]->depth_texture) {
			std::string vp_name = "u_light_viewprojections[" + std::to_string(i) + "]";
			std::string sm_name = "u_shadow_maps[" + std::to_string(i) + "]";
			std::string cast_name = "u_cast_shadows[" + std::to_string(i) + "]";

			shader->setUniform(vp_name.c_str(), light_viewprojections[i]);
			shader->setUniform(sm_name.c_str(), shadow_fbos[i]->depth_texture, 4 + i);
			shader->setUniform(cast_name.c_str(), (int)dir_lights[i]->cast_shadows);
		}
	}

	GFX::Mesh* quad = GFX::Mesh::getQuad();
	quad->render(GL_TRIANGLES);

	for (int i = 0; i < 4; ++i) {
		glActiveTexture(GL_TEXTURE4 + i);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glActiveTexture(GL_TEXTURE0); // Return to safe default
	

	shader->disable();
	light_fbo->unbind();
}

void Renderer::renderLightVolumes(Camera* camera)
{
	light_fbo->bind();

	// Explicitly target localized intersection testing using depth states
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GREATER);
	glDepthMask(GL_FALSE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE); // Pure Additive Blending 

	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT); // Rasterize backfaces inside volume coordinates

	GFX::Shader* shader = GFX::Shader::Get("lightvolume");
	if (!shader) {
		glDepthFunc(GL_LESS);
		glDisable(GL_BLEND);
		glCullFace(GL_BACK);
		light_fbo->unbind();
		return;
	}

	shader->enable();

	shader->setUniform("u_color_texture", gbuffer_fbo->color_textures[0], 0);
	shader->setUniform("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
	shader->setUniform("u_depth_texture", gbuffer_fbo->depth_texture, 2);


	shader->setUniform("u_inverse_viewprojection", camera->inverse_viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_screen_size", vec2((float)screen_width, (float)screen_height));
	shader->setUniform("u_shadow_bias", shadow_bias);

	// Track true indices to assign proper shadow attachments
	for (int light_idx = 0; light_idx < lights_list.size(); ++light_idx) {
		auto* light = lights_list[light_idx];
		if (light->light_type == eLightType::DIRECTIONAL) continue;

		std::vector<LightEntity*> single_light = { light };
		uploadLights(shader, single_light);

		// --- BIND SINGLE ACTIVE SHADOW RESOURCE FOR THIS VOLUME ---
		if (light_idx < 4 && light->cast_shadows && shadow_fbos[light_idx]) {
			shader->setUniform("u_light_viewprojection", light_viewprojections[light_idx]);
			shader->setUniform("u_shadow_map", shadow_fbos[light_idx]->depth_texture, 8); // Bind to Unit 8
			shader->setUniform("u_cast_shadows", (int)true);
		}
		else {
			shader->setUniform("u_cast_shadows", (int)false);
		}

		Matrix44 light_model = light->root.getGlobalMatrix();
		Vector3f light_pos = light_model.getTranslation();

		Matrix44 volume_model;
		volume_model.setIdentity();
		volume_model.setTranslation(light_pos.x, light_pos.y, light_pos.z);
		volume_model.scale(light->max_distance, light->max_distance, light->max_distance);

		shader->setUniform("u_model", volume_model);
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);

		sphere.render(GL_TRIANGLES);
	}

	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0); // Return to safe default

	shader->disable();

	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glCullFace(GL_BACK);
	light_fbo->unbind();
}

void Renderer::renderTransparencies(Camera* camera)
{
	light_fbo->bind();
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	std::sort(transparent_list.begin(), transparent_list.end(), [](const sRenderable& a, const sRenderable& b) {
		return a.distance > b.distance;
		});

	for (const sRenderable& call : transparent_list) {
		renderMeshWithMaterial(call.model, call.mesh, call.material);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	light_fbo->unbind();
}


void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	// Apply skybox necesarry config:
	// No blending, no dpeth test, we are always rendering the skybox
	// Set the culling aproppiately, since we just want the back faces
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	// Center the skybox at the camera, with a big sphere
	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	shader->setUniform("u_texture", cubemap, 0);

	sphere.render(GL_TRIANGLES);

	shader->disable();

	// Return opengl state to default
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

// We create a private helper function for our lights to use in both single and multipass
void Renderer::uploadLights(GFX::Shader* shader, const std::vector<LightEntity*>& lights)
{
	if (!shader || lights.empty()) return;

	/*
	* We initialize all our lists.
	* positions: position of the light
	* colors: color information in RGB of our lights
	* directions: frontvectors, so where our light is looking at
	* intensities: How strong is our light?
	* types: 0 for No light, 1 for Point Light, 2 for Spot Light, 3 for Directional Light
	* cones: we initialize a cone for our Spotlight.
	*/
	std::vector<vec3> positions, colors, directions; //
	std::vector<float> intensities;
	std::vector<int> types;
	std::vector<vec2> cones;


	/*
	* We initialize all our lists that are declared above.
	*/
	for (LightEntity* light : lights) {
		positions.push_back(light->root.getGlobalMatrix().getTranslation());
		colors.push_back(light->color);
		intensities.push_back(light->intensity);
		directions.push_back(light->root.getGlobalMatrix().frontVector());
		types.push_back((int)light->light_type);

		// Initialize cone of our spotlight
		float cos_inner = cos(light->cone_info.x * DEG2RAD);
		float cos_outer = cos(light->cone_info.y * DEG2RAD);
		cones.push_back(vec2(cos_inner, cos_outer));
	}

	if (shader == GFX::Shader::Get("lighting") || shader == GFX::Shader::Get("deferred") || shader == GFX::Shader::Get("lightvolume"))
	{
		shader->setUniform("u_num_lights", (int)positions.size());
		if (!positions.empty()) {
			// upload the shader uniforms so we can use them in the shader.
	// According to gemini it might be faster if we do this in renderScene instead of every Mesh. (keep in mind if we need better efficiency)
			shader->setUniform("u_num_lights", (int)positions.size());										//set a uniform for the amount of lights existing
			shader->setUniform3Array("u_light_positions", (float*)positions.data(), positions.size());  //set a uniform to access light positions
			shader->setUniform3Array("u_light_colors", (float*)colors.data(), positions.size());		//set a uniform to access light colors 
			shader->setUniform1Array("u_light_intensities", intensities.data(), positions.size());		//set a uniform to access light intensities

			// Different types for shader
			shader->setUniform3Array("u_light_directions", (float*)directions.data(), directions.size()); //set directional information
			shader->setUniform1Array("u_light_types", (int*)types.data(), types.size());

			// For Spotlights, we set the uniform for the cones here
			shader->setUniform2Array("u_light_cones", (float*)cones.data(), cones.size());

		}
	}

}


// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	// in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	// define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);

	// Choose our forward shader
	shader = GFX::Shader::Get("lighting");

	assert(glGetError() == GL_NO_ERROR);

	// no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	// Handle material blending options
	if (material->isTransparent()) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE); // Don't write to depth buffer
	}
	else {
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE); // Write to depth buffer
	}

	// 1. Upload basic transform transformations first
	shader->setUniform("u_model", model);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// 2. Bind the textures FIRST so we don't overwrite texture slot registers
	material->bind(shader);

	// 3. ONLY execute forward lighting/shadow uploads if we are natively inside the standard lighting pass
	if (shader == GFX::Shader::Get("lighting"))
	{
		uploadLights(shader, lights_list);

		int num_shadow_lights = (int)lights_list.size();
		if (num_shadow_lights > 4) num_shadow_lights = 4;
		for (int i = 0; i < num_shadow_lights; ++i)
		{
			if (!shadow_fbos[i] || !shadow_fbos[i]->depth_texture) continue;

			std::string vp_name = "u_light_viewprojections[" + std::to_string(i) + "]";
			std::string sm_name = "u_shadow_maps[" + std::to_string(i) + "]";
			std::string cast_name = "u_cast_shadows[" + std::to_string(i) + "]";

			shader->setUniform(vp_name.c_str(), light_viewprojections[i]);
			shader->setUniform(sm_name.c_str(), shadow_fbos[i]->depth_texture, 4 + i);
			shader->setUniform(cast_name.c_str(), (int)lights_list[i]->cast_shadows);
		}

		shader->setUniform("u_shadow_bias", shadow_bias);
	}

	// Upload time factor for shaders
	float t = getTime();
	shader->setUniform("u_time", t);

	// Wireframe override toggle check
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// Execute the Draw Call
	mesh->render(GL_TRIANGLES);

	// 4. CLEAN up active shadow texture units immediately after drawing geometry
	if (shader == GFX::Shader::Get("lighting")) {
		for (int i = 0; i < 4; ++i) {
			glActiveTexture(GL_TEXTURE4 + i);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}
	glActiveTexture(GL_TEXTURE0); // Fall back to safe default target slot

	// Disable active program
	shader->disable();

	// Restore global raster defaults
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Renderer::renderShadowMap(SCN::Scene* scene)
{
	//Calculate the shadow FBO for every light 
	for (int i = 0; i < lights_list.size(); ++i) {
		LightEntity* light = lights_list[i];
		if (!light->cast_shadows) continue;

		//Light camera set up
		mat4 light_model = light->root.getGlobalMatrix();
		vec3 light_pos = light_model.getTranslation();
		Vector3f light_front = light_model.frontVector().normalize();
		Vector3f light_up = Vector3f(0, 1, 0);
		if (fabs(light_front.dot(light_up)) > 0.99f)
			light_up = Vector3f(0, 0, 1); // avoid gimbal lock

		light_camera->lookAt(light_pos, light_pos - light_front, light_up);

		//Projection type depending on the light type
		if (light->light_type == eLightType::DIRECTIONAL) {
			float area = light->area / 2.0f;
			light_camera->setOrthographic(-area, area, -area, area,
				light->near_distance, light->max_distance);
		}
		else if (light->light_type == eLightType::SPOT) {
			// cone_info.y = half-cone of outer cone; setPerspective needs full FOV
			float fov = light->cone_info.y * 2.0f;
			light_camera->setPerspective(fov, 1.0f,
				light->near_distance, light->max_distance);
		}

		light_camera->updateViewMatrix();
		light_camera->updateProjectionMatrix();

		light_viewprojections[i] = light_camera->viewprojection_matrix;

		//parseSceneEntities(scene, light_camera);

		//Start rendering to the FBO
		shadow_fbos[i]->bind();
		glViewport(0, 0, 1024, 1024);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glClear(GL_DEPTH_BUFFER_BIT);
		//Check for Frontface culling for shadow acne reduction
		if (shadow_front_face_culling) {
			glEnable(GL_CULL_FACE);
			glFrontFace(GL_CW);
		}

		GFX::Shader* shader = GFX::Shader::Get("plain");
		shader->enable();


		shader->setUniform("u_viewprojection", light_viewprojections[i]);

		for (sRenderable& r : opaque_list) {
			if (!r.mesh || !r.material) continue;
			if (r.material->alpha_mode == SCN::eAlphaMode::BLEND) continue;
			shader->setUniform("u_model", r.model);
			r.mesh->render(GL_TRIANGLES);
		}
		shader->disable();


		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glFrontFace(GL_CCW);
		glDisable(GL_CULL_FACE);

		shadow_fbos[i]->unbind();
	}

	glActiveTexture(GL_TEXTURE0);
}

#ifndef SKIP_IMGUI
void Renderer::showUI()
{
	// Pipeline Switch toggle requirement
	ImGui::Text("Pipeline Selection:");
	ImGui::Checkbox("Use Deferred Renderer", &use_deferred);
	ImGui::Separator();

	// 1. Basic Checkboxes
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	ImGui::Separator();
	ImGui::Text("Shadow Map");
	ImGui::SliderFloat("Shadow Bias", &shadow_bias, 0.0001f, 0.05f);

	ImGui::Checkbox("Front Face Culling", &shadow_front_face_culling);
	ImGui::SliderInt("Shadow Light Index", &shadow_light_index, 0, 5);

	SCN::Material* mat = nullptr;

	// 2. Selection Logic
	if (SCN::Node::s_selected && SCN::Node::s_selected->material) {
		mat = SCN::Node::s_selected->material;
	}
	else if (SCN::BaseEntity::s_selected && SCN::BaseEntity::s_selected->getType() == SCN::eEntityType::PREFAB) {
		SCN::PrefabEntity* pref = (SCN::PrefabEntity*)SCN::BaseEntity::s_selected;

		// Recursive search for the first material in the tree
		std::vector<SCN::Node*> nodes_to_check;
		nodes_to_check.push_back(&(pref->root));

		while (!nodes_to_check.empty()) {
			SCN::Node* current = nodes_to_check.back();
			nodes_to_check.pop_back();

			if (current->material) {
				mat = current->material;
				break;
			}

			for (SCN::Node* child : current->children) {
				nodes_to_check.push_back(child);
			}
		}
	}

	// 3. Drawing the UI
	ImGui::Separator();
	if (mat) {

		if (mat->shininess <= 1.0f) {
			mat->shininess = pow(2.0f, (1.0f - mat->roughness_factor) * 10.0f);
		}

		// Use a persistent ID for the Tree
		if (ImGui::TreeNodeEx((void*)(intptr_t)mat->index, ImGuiTreeNodeFlags_DefaultOpen, "Material: %s", mat->name.empty() ? "unnamed" : mat->name.c_str()))
		{
			// Link Roughness to Shininess
			if (ImGui::SliderFloat("Roughness", &mat->roughness_factor, 0.0f, 1.0f)) {
				mat->shininess = pow(2.0f, (1.0f - mat->roughness_factor) * 10.0f);
			}

			// Link Shininess to Roughness
			if (ImGui::SliderFloat("Phong Shininess", &mat->shininess, 1.0f, 1024.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
				mat->roughness_factor = 1.0f - (log2(mat->shininess) / 10.0f);
			}

			ImGui::TreePop();
		}
	}
	else {
		ImGui::TextDisabled("(No material found in selection)");
	}
}
#else
void Renderer::showUI() {}
#endif