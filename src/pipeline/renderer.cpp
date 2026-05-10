#include "renderer.h"

#include <algorithm> //sort

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

Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	scene = nullptr;
	skybox_cubemap = nullptr;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();

	// Shadow map init — add at end of Renderer constructor
	shadow_fbo = nullptr;
	light_camera = nullptr;
	shadow_bias = 0.005f;
	shadow_front_face_culling = false;
	shadow_light_index = 0;
	// 3.1 — Depth-only FBO, 1024x1024
	shadow_fbo = new GFX::FBO();
	shadow_fbo->setDepthOnly(1024, 1024);
	light_camera = new Camera();
}

void Renderer::setupScene()
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;
}

// Also from class
struct sRenderable
{
	GFX::Mesh* mesh;
	SCN::Material* material;
	Matrix44 model;
	float distance;
};

std::vector<sRenderable> render_list;
std::vector<sRenderable> opaque_list;
std::vector<sRenderable> transparent_list;

// Phong Lighting
std::vector<LightEntity*> lights_list;

//To include children and partial rendering!
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
		BoundingBox center = transformBoundingBox(node->getGlobalMatrix(), node->mesh->box);
		char res = cam->testBoxInFrustum(center.center, center.halfsize);
		if (res == CLIP_OUTSIDE) {
			return;
		}
		else if (res == CLIP_INSIDE) {
			addSubtree(node);
			return;
		}
	}


	// Push back is a very handy way to initialize structures and opjects
	render_list.push_back({
		.mesh = node->mesh,
		.material = node->material,
		.model = node->getGlobalMatrix()
		});


	for (Node* child : node->children) {
		parseNode(child, cam);
	}
}


void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) {
	// HERE =====================
	// TODO: GENERATE RENDERABLES
	// ==========================
	render_list.clear();
	opaque_list.clear();
	transparent_list.clear();
	lights_list.clear();


	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];

		if (!entity->visible) {
			continue;
		}

		// From Class!!
		if (entity->getType() == eEntityType::PREFAB) {
			//
			PrefabEntity* e = (PrefabEntity*)entity;

			parseNode(&(entity->root), cam);

			// Implement transparency here???
		}

		if (entity->getType() == eEntityType::LIGHT) {
			LightEntity* l = (LightEntity*)entity;

			// parseNode(&(entity->root), cam)
			lights_list.push_back(l);
		}


		// Store Prefab Entitys
		// ...
		//		Store Children Prefab Entities

		// Store Lights
		// ...
	}

	// another for loop to implement our transprant and opaque lists.
	for (sRenderable& r : render_list) {
		// compute distance from camera to object using model translation
		if (cam)
			r.distance = r.model.getTranslation().distance(cam->eye);
		else
			r.distance = 0.0f;

		// ALPHA cutout as opaque and BLEND as transparent
		// If it is transparent, add it to transparent list, else opaque
		if (r.material && r.material->isTransparent())
			transparent_list.push_back(r);
		else
			opaque_list.push_back(r);
	}


	// Debug for whether Lights are perceived correctly.
	// std::cout << "lights_list=" << lights_list.size() << std::endl;

	// Debug for what is rendering
	// std::cout << "render_list=" << render_list.size() << " opaque=" << opaque_list.size() << " transparent=" << transparent_list.size() << std::endl;

}


void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();

	camera->updateViewMatrix();
	camera->updateProjectionMatrix();
	camera->extractFrustum();

	parseSceneEntities(scene, light_camera);
	renderShadowMap(scene);
	parseSceneEntities(scene, camera);

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (skybox_cubemap)
		renderSkybox(skybox_cubemap);

	// HERE =====================
	// TODO: RENDER RENDERABLES
	// ==========================
	// From class

	// Global Bool masks
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);

	// We had an if call here with if call.material.a
	// Opaque pass
	for (sRenderable& call : opaque_list) {
		renderMeshWithMaterial(call.model, call.mesh, call.material);
	}

	// Change names for renderables
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

// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	//shader = GFX::Shader::Get("texture");
	shader = GFX::Shader::Get("lighting");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	// FOR PHONG
	if (material->isTransparent()) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE); // Don't write to depth buffer
	}
	else {
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE); // Write to depth buffer
	}

	float t = getTime();
	shader->setUniform("u_time", t);

	// PHONG MULTICALL
	std::vector<vec3> light_positions, light_colors, light_fronts;
	std::vector<float> light_intensities, light_types;
	std::vector<vec2> light_cones;

	for (size_t i = 0; i < lights_list.size(); ++i) {
		LightEntity* light = lights_list[i];
		//load arrays of multicall
		light_positions.push_back(light->root.getGlobalMatrix().getTranslation());
		light_colors.push_back(light->color);
		light_intensities.push_back(light->intensity);
		light_fronts.push_back(light->root.getGlobalMatrix().frontVector());
		light_types.push_back((float)light->light_type);

		//Convert angles to radians for shader (cosine magic)
		float cos_min = cos(light->cone_info.y * DEG2RAD); // cos(min)
		float cos_max = cos(light->cone_info.x * DEG2RAD); // cos(max)

		light_cones.push_back(vec2(cos_min, cos_max));
	}

	shader->setUniform("u_num_lights", (int)light_positions.size());
	shader->setUniform3Array("u_light_fronts", (float*)light_fronts.data(), light_positions.size());
	shader->setUniform1Array("u_light_types", light_types.data(), light_positions.size());
	shader->setUniform3Array("u_light_positions", (float*)light_positions.data(), light_positions.size());
	shader->setUniform3Array("u_light_colors", (float*)light_colors.data(), light_positions.size());
	shader->setUniform1Array("u_light_intensities", light_intensities.data(), light_positions.size());
	// Send shadow map uniforms to shader
	if (shadow_fbo && shadow_fbo->depth_texture) {
		shader->setUniform("u_shadow_map", shadow_fbo->depth_texture, 1);
		shader->setUniform("u_light_viewprojection", light_viewprojection);
		shader->setUniform("u_shadow_bias", shadow_bias);
		shader->setUniform("u_shadow_maps[0]", shadow_fbo->depth_texture, 1);
		shader->setUniform("u_shadow_maps[1]", shadow_fbo->depth_texture, 2);
		shader->setUniform("u_shadow_maps[2]", shadow_fbo->depth_texture, 3);
		shader->setUniform("u_shadow_maps[3]", shadow_fbo->depth_texture, 4);
	}

	if (light_cones.size() > 0) {
		shader->setUniform2Array("u_light_cones", (float*)light_cones.data(), light_cones.size());
	}



	//PHONG MULTICALL
	shader->setUniform("u_model", model);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	//upload uniforms, single call
	//shader->setUniform("u_model", model);


	// Upload camera uniforms
	//shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	//shader->setUniform("u_camera_position", camera->eye);


	// Upload Light Arrays
	int num_lights = (int)light_positions.size();


	//PHONG LIGHTING
	//if (!lights_list.empty())
	//{
	//	LightEntity* light = lights_list[0];

	//	shader->setUniform("u_light_color", light->color);
	//	shader->setUniform("u_light_intensity", light->intensity);

	//	// UNSICHER
	//	vec3 light_pos = light->root.getGlobalMatrix().getTranslation();
	//	shader->setUniform("u_light_position", light_pos);
	//}


	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	material->bind(shader);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	//glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);


}

#ifndef SKIP_IMGUI

void Renderer::showUI()
{

	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	ImGui::Separator();
	ImGui::Text("Shadow Map");
	ImGui::SliderFloat("Shadow Bias", &shadow_bias, 0.0001f, 0.05f);
	ImGui::Checkbox("Front Face Culling", &shadow_front_face_culling);
	ImGui::SliderInt("Shadow Light Index", &shadow_light_index, 0, 5);

	if (ImGui::TreeNode("Material")) {
		// Example for the default material or a selected node's material
		ImGui::SliderFloat("Shininess", &Material::default_material.roughness_factor, 0.0f, 1.0f);
		ImGui::ColorEdit3("Color", Material::default_material.color.v);
		ImGui::TreePop();

	}
	//add here your stuff
	//...
}


void Renderer::renderShadowMap(SCN::Scene* scene)
{
	// Find the shadow-casting light by index
	LightEntity* light = nullptr;
	int found = 0;
	for (BaseEntity* e : scene->entities) {
		if (e->getType() == eEntityType::LIGHT) {
			if (found == shadow_light_index) { light = (LightEntity*)e; break; }
			found++;
		}
	}
	if (!light || !light->cast_shadows) return;

	// 3.2.1 — Configure light camera
	Matrix44 light_model = light->root.getGlobalMatrix();
	Vector3f light_pos = light_model.getTranslation();
	Vector3f light_front = light_model.frontVector().normalize();
	Vector3f light_up = Vector3f(0, 1, 0);
	if (fabs(light_front.dot(light_up)) > 0.99f)
		light_up = Vector3f(0, 0, 1); // avoid gimbal lock

	light_camera->lookAt(light_pos, light_pos + light_front, light_up);

	if (light->light_type == eLightType::SPOT) {
		// cone_info.y = half-cone of outer cone; setPerspective needs full FOV
		float fov = light->cone_info.y * 2.0f;
		light_camera->setPerspective(fov, 1.0f,
			light->near_distance, light->max_distance);
	}
	else if (light->light_type == eLightType::DIRECTIONAL) {
		float area = light->area;
		light_camera->setOrthographic(-area, area, -area, area,
			light->near_distance, light->max_distance);
	}
	light_camera->updateViewMatrix();
	light_camera->updateProjectionMatrix();
	light_viewprojection = light_camera->viewprojection_matrix;

	// 3.2.2 — Render depth pass to FBO
	shadow_fbo->bind();
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);

	if (shadow_front_face_culling) {
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT); // 3.4.2
	}

	GFX::Shader* shader = GFX::Shader::Get("flat");
	if (shader) {
		shader->enable();
		shader->setUniform("u_viewprojection", light_viewprojection);
		shader->setUniform("u_color", Vector4f(1, 1, 1, 1));
		for (sRenderable& r : opaque_list) {
			if (!r.mesh || !r.material) continue;
			if (r.material->alpha_mode == SCN::eAlphaMode::BLEND) continue;
			shader->setUniform("u_model", r.model);
			r.mesh->render(GL_TRIANGLES);
		}
		shader->disable();
	}

	// Restore state
	if (shadow_front_face_culling) glCullFace(GL_BACK);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDisable(GL_CULL_FACE);
	shadow_fbo->unbind();
}
#else
void Renderer::showUI() {}
#endif