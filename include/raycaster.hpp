#pragma once


#include <SFML/Graphics.hpp>
#include "SVO.hpp"
#include "utils.hpp"


struct RayContext
{
	float distance = 0.0f;
	uint32_t complexity = 0U;
	uint32_t bounds = 0U;
	int32_t gi_bounce = 2U;
};


struct Sample
{
	double update_count = 0.0f;
	double r = 0.0f;
	double g = 0.0f;
	double b = 0.0f;
};


struct GIContribution
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float intensity = 0.0f;
	float n_points;
};

struct ColorResult
{
	sf::Color color = sf::Color::Black;
	float distance = 0.0f;
};


struct RayCaster
{
	const float eps = 0.001f;

	RayCaster(const SVO& svo_, const sf::Vector2i& render_size_)
		: svo(svo_)
		, render_size(render_size_)
	{
		render_image.create(render_size.x, render_size.y);
		image_side.loadFromFile("C:/Users/jeant/Documents/Code/cpp/CpuVoxelRaycaster/res/grass_side_16x16.bmp");
		image_top.loadFromFile("C:/Users/jeant/Documents/Code/cpp/CpuVoxelRaycaster/res/grass_top_16x16.bmp");

		colors.resize(render_size_.x);
		for (auto& v : colors) {
			v.resize(render_size_.y);
		}
	}

	void setLightPosition(const glm::vec3& position)
	{
		light_position = position;
	}

	void renderRay(const sf::Vector2i pixel, const glm::vec3& start, const glm::vec3& direction, float time)
	{
		const uint32_t x = pixel.x;
		const uint32_t y = pixel.y;

		RayContext context;
		context.distance = 0.0f;

		ColorResult result = castRay(start, direction, 1.5f * time, context);

		sf::Color old_color = render_image.getPixel(pixel.x, pixel.y);

		if (!use_samples) {
			const float old_conservation = 0.0f;
			mult(old_color, old_conservation);
			mult(result.color, 1.0f - old_conservation);
			add(old_color, result.color);
			render_image.setPixel(pixel.x, pixel.y, old_color);
		}
		else {
			colors[x][y].r += result.color.r;
			colors[x][y].g += result.color.g;
			colors[x][y].b += result.color.b;
			colors[x][y].update_count += 1.0;
		}
	}

	void samples_to_image()
	{
		for (int32_t x(0); x < render_size.x; ++x) {
			for (int32_t y(0); y < render_size.y; ++y) {
				const Sample& s = colors[x][y];
				const sf::Color color(s.r / s.update_count, s.g / s.update_count, s.b / s.update_count);
				render_image.setPixel(x, y, color);
			}
		}
	}

	void resetSamples()
	{
		for (int32_t x(0); x < render_size.x; ++x) {
			for (int32_t y(0); y < render_size.y; ++y) {
				Sample& s = colors[x][y];
				s.r = 0.0f;
				s.g = 0.0f;
				s.b = 0.0f;
				s.update_count = 0.0f;
			}
		}
	}

	ColorResult castRay(const glm::vec3& start, const glm::vec3& direction, float time, RayContext& context)
	{
		ColorResult result;
		if (context.bounds > max_bounds)
			return result;

		const HitPoint intersection = svo.castRay(start, direction, 512U);
		context.complexity += intersection.complexity;
		context.distance = intersection.distance;

		if (intersection.cell) {
			//result.color = sf::Color::Red;
			result.distance = intersection.distance;
			const Cell& cell = *(intersection.cell);
			const glm::vec3 hit_position = intersection.position + eps * intersection.normal;

			sf::Color hit_texture_color = sf::Color::Black;
			if (cell.type == Cell::Solid) {
				hit_texture_color = getTextureColorFromHitPoint(intersection);
				result.color = hit_texture_color;
				if (use_ao) {
					mult(result.color, getAmbientOcclusion(intersection));
				}
			}
			else if (cell.type == Cell::Mirror) {
				const float roughness = 0.04f;
				const glm::vec3 normal = glm::normalize(intersection.normal + glm::vec3(roughness * getRand(), 0.0f, roughness * getRand()));
				context.bounds++;
				result = castRay(hit_position, glm::reflect(direction, normal), time, context);
				mult(result.color, 0.8f);
			}

			const uint32_t shadow_sample = use_samples ? 4U : 1U;

			if (cell.texture != Cell::Red) {
				float light_intensity = 0.0f;
				for (uint32_t i(shadow_sample); i--;) {
					const glm::vec3 light_point = light_position + glm::vec3(getRand(-25.0f, 25.0f), getRand(-25.0f, 25.0f), 0.0f);
					const glm::vec3 point_to_light = glm::normalize(light_point - intersection.position);
					const HitPoint light_intersection = svo.castRay(hit_position, point_to_light, 128U);

					if (!light_intersection.cell) {
						light_intensity += std::max(0.0f, glm::dot(point_to_light, intersection.normal));
					}
				}

				mult(result.color, std::max(0.0f, (light_intensity) / float(shadow_sample)));
			}

			if (use_gi && context.gi_bounce) {
				--context.gi_bounce;
				GIContribution gic;
				getGlobalIllumination(intersection, context, gic);
				const sf::Color gi_color(gic.r, gic.g, gic.b);
				sf::Color obj_color = hit_texture_color;
				mult(obj_color, gi_color);
				add(result.color, obj_color);
			}
		}

		/*const int32_t c = context.complexity;
		sf::Color color(c, c, c);
		result.color = color;*/
		return result;
	}

	float getAmbientOcclusion(const HitPoint& point)
	{
		const uint32_t ray_count = 4U;
		const uint32_t max_iter = 16U;
		const glm::vec3 ao_start = point.position + point.normal * eps;
		float acc = 0.0f;
		for (uint32_t i(ray_count); i--;) {
			glm::vec3 noise_normal = glm::vec3();

			if (point.normal.x) {
				noise_normal = glm::vec3(0.0f, getRand(-1.0f, 1.0f), getRand(-1.0f, 1.0f));
			}
			else if (point.normal.y) {
				noise_normal = glm::vec3(getRand(-1.0f, 1.0f), 0.0f, getRand(-1.0f, 1.0f));
			}
			else if (point.normal.z) {
				noise_normal = glm::vec3(getRand(-1.0f, 1.0f), getRand(-1.0f, 1.0f), 0.0f);
			}

			HitPoint ao_point = svo.castRay(ao_start, glm::normalize(point.normal + noise_normal), max_iter);
			if (!ao_point.cell) {
				acc += 1.0f;
			}
		}

		return std::min(1.0f, acc / float(ray_count));
	}

	float getGodRaysIntensity(const glm::vec3& start, const glm::vec3& direction, const glm::vec3& stop, float step_size)
	{
		float light_intensity = 0.0f;
		float distance = 0.0f;
		glm::vec3 measure_start = start;
		while (measure_start.x > 0.0f && measure_start.y > 0.0f && measure_start.z > 0.0f &&
			   measure_start.x < 512.0f && measure_start.y < 512.0f && measure_start.z < 512.0f) {
			
			const glm::vec3 gr_direction = light_position - measure_start;
			const HitPoint hp = svo.castRay(measure_start, gr_direction, 512U);
			light_intensity += !hp.cell ? 0.1f * hp.complexity : 0.0f;

			distance += step_size;
			measure_start += direction * step_size;
		}

		return light_intensity;
	}

	void getGlobalIllumination(const HitPoint& point, RayContext& context, GIContribution& result)
	{
		const uint32_t ray_count = 16U;
		const uint32_t max_iter = 512U;
		const glm::vec3 gi_start = point.position + point.normal * eps;
		float acc = 0.0f;
		uint32_t valid_rays = ray_count;
		for (uint32_t i(ray_count); i--;) {
			glm::vec3 noise_normal = glm::vec3();

			noise_normal = glm::vec3(getRand(-1.0f, 1.0f), getRand(-1.0f, 1.0f), getRand(-1.0f, 1.0f));

			const glm::vec3 gi_vec = glm::normalize(point.normal + noise_normal);
			const float coef = 8.0f;
			if (glm::dot(point.normal, gi_vec) > 0.0f) {
				const ColorResult ray_result = castRay(gi_start, glm::normalize(point.normal + noise_normal), max_iter, context);

				result.r += coef * ray_result.color.r;
				result.g += coef * ray_result.color.g;
				result.b += coef * ray_result.color.b;
			} else {
				--valid_rays;
			}
		}

		result.n_points += ray_count;

		if (valid_rays) {
			result.r /= float(valid_rays);
			result.g /= float(valid_rays);
			result.b /= float(valid_rays);
		}
	}

	const sf::Image& getTextureFromNormal(const glm::vec3& normal)
	{
		if (normal.y) {
			return image_top;
		}

		return image_side;
	}

	const sf::Color getTextureColorFromHitPoint(const HitPoint& point)
	{
		if (point.cell->texture == Cell::Grass) {
			return getColorFromVoxelCoord(getTextureFromNormal(point.normal), point.voxel_coord);
		}
		else if (point.cell->texture == Cell::Red) {
			return sf::Color::Red;
		}
		else if (point.cell->texture == Cell::White) {
			return sf::Color::White;
		}
		else {
			return sf::Color::Magenta;
		}
	}

	const sf::Color getColorFromVoxelCoord(const sf::Image& image, glm::vec2 coords)
	{
		const sf::Vector2u tex_size = image.getSize();
		clamp(coords.x, 0.0f, 1.0f);
		clamp(coords.y, 0.0f, 1.0f);
		return image.getPixel(uint32_t(tex_size.x * coords.x), uint32_t(tex_size.y * coords.y));
	}


	const sf::Color getColorFromNormal(const glm::vec3& normal)
	{
		if (normal.x || normal.z) {
			return sf::Color(94, 61, 27);
		}
		return sf::Color(47, 89, 10);
	}

	std::vector<std::vector<Sample>> colors;

	sf::Image render_image;
	sf::Image image_side;
	sf::Image image_top;

	const SVO& svo;

	const sf::Vector2i render_size;

	glm::vec3 light_position;

	sf::Color sky_color = sf::Color(119, 199, 242);

	bool use_ao = false;
	bool use_gi = false;
	bool use_samples = false;
	bool use_god_rays = false;
	const uint32_t max_bounds = 4;
	//const sf::Color sky_color = sf::Color(166, 215, 255);


	const float focal_length = 1.0f;
	const float aperture = 0.001f;
};