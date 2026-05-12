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

		// This is added to decide what mode we are using (0 for Single, 1 for Multi)
		int render_mode;

		GFX::Texture* skybox_cubemap;

		SCN::Scene* scene;

		std::vector<SCN::LightEntity*> lights_list; // We create a list of lights, so we can work through all the lights we get (Need to be a lightentity obviously)

		//updated every frame
		Renderer(const char* shaders_atlas_filename );

		//just to be sure we have everything ready for the rendering
		void setupScene();

		void uploadLights(GFX::Shader* shader, const std::vector<LightEntity*>& light);

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