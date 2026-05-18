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
	gbuffer_fbo->create(width, height, 2, GL_RGBA, GL_UNSIGNED_BYTE, true);

	// Light FBO
	light_fbo = new GFX::FBO();
	light_fbo->create(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, true);
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
		char res = cam->testBoxInFrustum(worldBox.center, worldBox.halfsize);
		if (res == CLIP_OUTSIDE) {
			return;
		}
		else if (res == CLIP_INSIDE) {
			addSubtree(node);
			return;
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



void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();

	//Camera updates are processed here before rendering

	camera->updateViewMatrix();
	camera->updateProjectionMatrix();
	camera->extractFrustum();

	parseSceneEntities(scene, camera);

	if (use_deferred)
		renderDeferred(scene, camera);
	else
	{
		renderForward(scene, camera);
	}
} // NEED TO REMOVE THIS SOON FOR TESTING

void Renderer::renderForward(SCN::Scene * scene, Camera * camera) {

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if(skybox_cubemap)
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

	glEnable(GL_DEPTH_TEST);
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

	for (sRenderable& call : opaque_list) {
		if (!call.mesh || !call.material) continue;
		call.material->bind(shader);
		shader->setUniform("u_model", call.model);
		call.mesh->render(GL_TRIANGLES);
	}

	shader->disable();
	gbuffer_fbo->unbind();
}

void Renderer::renderDeferredAmbientAndDirectional(Camera* camera)
{
	// Blending and depth for the base pass mapping inside the light_fbo
	gbuffer_fbo->depth_texture->copyTo(light_fbo->depth_texture);

	light_fbo->bind();
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

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
	gbuffer_fbo->color_textures[0]->bind(0);
	shader->setUniform("u_color_texture", 0);
	gbuffer_fbo->color_textures[1]->bind(1);
	shader->setUniform("u_normal_texture", 1);
	gbuffer_fbo->depth_texture->bind(2);
	shader->setUniform("u_depth_texture", 2);

	shader->setUniform("u_inverse_viewprojection", camera->inverse_viewprojection_matrix);
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_camera_position", camera->eye);

	// Group and pass directional light vectors down
	std::vector<LightEntity*> dir_lights;
	for (auto* l : lights_list) {
		if (l->light_type == eLightType::DIRECTIONAL) dir_lights.push_back(l);
	}
	uploadLights(shader, dir_lights);

	GFX::Mesh* quad = GFX::Mesh::getQuad();
	quad->render(GL_TRIANGLES);

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

	gbuffer_fbo->color_textures[0]->bind(0);
	shader->setUniform("u_color_texture", 0);
	gbuffer_fbo->color_textures[1]->bind(1);
	shader->setUniform("u_normal_texture", 1);
	gbuffer_fbo->depth_texture->bind(2);
	shader->setUniform("u_depth_texture", 2);

	shader->setUniform("u_inverse_viewprojection", camera->inverse_viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_screen_size", vec2((float)screen_width, (float)screen_height));

	for (auto* light : lights_list) {
		if (light->light_type == eLightType::DIRECTIONAL) continue;

		// Frame light arrays down individually for localized intersections
		std::vector<LightEntity*> single_light = { light };
		uploadLights(shader, single_light);

		Matrix44 model;
		model.setTranslation(light->model.getTranslation());
		model.scale(light->max_distance, light->max_distance, light->max_distance);
		shader->setUniform("u_model", model);
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);

		sphere.render(GL_TRIANGLES);
	}

	shader->disable();

	// Standardize pipeline settings out to default 
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glCullFace(GL_BACK);
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


// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	// FOR TESTING WE CAN TURN THIS ON AGAIN
	//shader = GFX::Shader::Get("texture");
	
	// For Assignment 2, we are changing this to lighting!
	shader = GFX::Shader::Get("lighting");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	material->bind(shader);

	// We split for the single and multi-pass but first we need to set the common uniforms:
	
	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t);

	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// We don't even need Multi-pass so we will let it stay like this.
	uploadLights(shader, lights_list);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	//glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

#ifndef SKIP_IMGUI
void Renderer::showUI()
{
	// 1. Basic Checkboxes
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

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