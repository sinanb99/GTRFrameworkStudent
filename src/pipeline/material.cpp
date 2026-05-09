
#include "material.h"

#include "../core/includes.h"
#include "../gfx/texture.h"
#include "../gfx/shader.h"

using namespace SCN;

std::map<std::string, Material*> Material::sMaterials;
uint32 Material::s_last_index = 0;
Material Material::default_material;

const char* SCN::texture_channel_str[] = { "ALBEDO","EMISSIVE","OPACITY","METALLIC_ROUGHNESS","OCCLUSION","NORMALMAP" };


Material* Material::Get(const char* name)
{
	assert(name);
	std::map<std::string, Material*>::iterator it = sMaterials.find(name);
	if (it != sMaterials.end())
		return it->second;
	return NULL;
}

void Material::registerMaterial(const char* name)
{
	this->name = name;
	sMaterials[name] = this;
}

Material::~Material()
{
	if (name.size())
	{
		auto it = sMaterials.find(name);
		if (it != sMaterials.end())
			sMaterials.erase(it);
	}
}

void Material::Release()
{
	std::vector<Material *>mats;

	for (auto mp : sMaterials)
	{
		Material *m = mp.second;
		mats.push_back(m);
	}

	for (Material *m : mats)
	{
		delete m;
	}
	sMaterials.clear();
}

// Transparency checker for Assignment 1
bool Material::isTransparent() const {
	return alpha_mode == SCN::eAlphaMode::BLEND;
}

void Material::bind(GFX::Shader* shader) {
	// First, configure the OpenGL state with the material settings =======================
	{
		// Select the blending
		if (alpha_mode == SCN::eAlphaMode::BLEND)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		else
			glDisable(GL_BLEND);

		// Select if render both sides of the triangles
		if (two_sided)
			glDisable(GL_CULL_FACE);
		else
			glEnable(GL_CULL_FACE);

		// Check if any error
		assert(glGetError() == GL_NO_ERROR);
	}

	// Bind the textures and set uniforms =======================
	/* Before implementing Assignment 2, we are computing every texture with ALBEDO.
	   Now we need to swtich for the metallic_roughness, the normal map and else.
	*/
	{
		GFX::Texture* texture = textures[SCN::eTextureChannel::ALBEDO].texture;

		// HERE =====================
		// TODO: Expand rfor the rest of materials (when you need to)
		//	texture = emissive_texture;
		//	texture = metallic_roughness_texture;
		//	texture = normal_texture;
		//	texture = occlusion_texture;
		// ==========================

		// We always force a default albedo texture
		if (texture == NULL)
			texture = GFX::Texture::getWhiteTexture(); //a 1x1 white texture

		shader->setUniform("u_color", color);

		if (texture)
			shader->setUniform("u_texture", texture, 0);

		// We are adding the roughness here and adding it as a uniform to send it to our shader (Assignment 2)
		shader->setUniform("u_roughness", roughness_factor);

		// This is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
		shader->setUniform("u_alpha_cutoff", alpha_mode == SCN::eAlphaMode::MASK ? alpha_cutoff : 0.001f);
	}
}
