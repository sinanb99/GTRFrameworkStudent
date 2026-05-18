#pragma once
#include "scene.h"
#include "prefab.h"
#include "light.h"
#include <vector>

//forward declarations
class Camera;
class Skeleton;
namespace GFX {
	class Shader;
	class Mesh;
	class FBO;
}

namespace SCN {

	class Prefab;
	class Material;


	// For the switch between multi and single pass:
	enum eRenderMode {
		SINGLE_PASS,
		MULTI_PASS
	};

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;

		// Assignment 4 Toggle
		bool use_deferred;

		// This is added to decide what mode we are using (0 for Single, 1 for Multi)
		int render_mode;

		GFX::Texture* skybox_cubemap;
		SCN::Scene* scene;

		// G-Buffer FBO: stores color (target 0) and normals (target 1) + depth
		GFX::FBO* gbuffer_fbo;

		// Light FBO: final lighting accumulation buffer (same size as screen)
		GFX::FBO* light_fbo;

		// Screen dimensions
		int screen_width;
		int screen_height;

		//updated every frame
		// Updated for Assignment 4
		Renderer(const char* shaders_atlas_filename, int width, int height);
		// Shadow map resources
		GFX::FBO* shadow_fbo = nullptr;
		Camera* light_camera = nullptr;
		Matrix44 light_viewprojection;
		float shadow_bias = 0.001f;
		int shadow_light_index = 0;
		bool shadow_front_face_culling = true;

		GFX::FBO* shadow_fbos[4];

		// Array to store the view-projection matrix for each light
		Matrix44 light_viewprojections[4];

		// And declare the method:
		void renderShadowMap(SCN::Scene* scene);
		std::vector<SCN::LightEntity*> lights_list; // We create a list of lights, so we can work through all the lights we get (Need to be a lightentity obviously)

		//updated every frame
		Renderer(const char* shaders_atlas_filename);

		//just to be sure we have everything ready for the rendering
		void setupScene();

		void uploadLights(GFX::Shader* shader, const std::vector<LightEntity*>& light);

		//add here your functions
		//...

		void collectLights(Scene* scene);

		void parseSceneEntities(SCN::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		// Forward pipeline (original single/multi pass logic)
		void renderForward(SCN::Scene* scene, Camera* camera);

		// Deferred pipeline entry point
		void renderDeferred(SCN::Scene* scene, Camera* camera);

		// G-Buffer
		void renderGBuffer(Camera* camera);

		// Single pass deferred: ambient + directional via fullscreen quad
		void renderDeferredAmbientAndDirectional(Camera* camera);

		// Light-Volumes: point and spot lights as spheres
		void renderLightVolumes(Camera* camera);

		// Transparent rendering 
		void renderTransparencies(Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void showUI();


	};

};