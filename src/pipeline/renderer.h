#pragma once
#include "scene.h"
#include "prefab.h"

#include "light.h"

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

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;

		GFX::Texture* skybox_cubemap;

		SCN::Scene* scene;

		// Shadow map resources
		GFX::FBO* shadow_fbo = nullptr;
		Camera* light_camera = nullptr;
		Matrix44 light_viewprojection;
		float shadow_bias = 0.001f;
		int shadow_light_index = 0;
		bool shadow_front_face_culling = true;

		// And declare the method:
		void renderShadowMap(SCN::Scene* scene);
		std::vector<SCN::LightEntity*> lights_list; // We create a list of lights, so we can work through all the lights we get (Need to be a lightentity obviously)

		//updated every frame
		Renderer(const char* shaders_atlas_filename);

		//just to be sure we have everything ready for the rendering
		void setupScene();

		//add here your functions
		//...

		void parseSceneEntities(SCN::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void showUI();


	};

};