#pragma once


#include <SFML/Graphics.hpp>
#include "volumetric.hpp"


struct RayCaster
{
	RayCaster(const Volumetric& volumetric_, sf::VertexArray& va_, const sf::Vector2i& render_size_)
		: volumetric(volumetric_)
		, va(va_)
		, render_size(render_size_)
	{}

	void setLightPosition(const glm::vec3& position)
	{
		light_position = position;
	}

	void cast_ray(const sf::Vector2i pixel, const glm::vec3& start, const glm::vec3& direction, float time)
	{
		const HitPoint intersection = volumetric.cast_ray(start, direction);
		const uint32_t x = pixel.x;
		const uint32_t y = pixel.y;
		va[x * render_size.y + y].position = sf::Vector2f(x, y);
		va[x * render_size.y + y].color = sf::Color(135, 181, 255);

		if (intersection.type == Cell::Solid) {
			va[x * render_size.y + y].color = getColorFromHitPoint(intersection);
		} else if (intersection.type == Cell::Mirror) {
			constexpr float water_freq = 4.0f;
			//glm::vec3 surface_deformation(0.0f, 0.0f, 0.0f);
			time *= 1.5f;
			glm::vec3 surface_deformation(sin(water_freq * intersection.position.x + time), cos(water_freq * intersection.position.x + time), 0.0f);
			const glm::vec3 normal = glm::normalize(intersection.normal + 0.02f * surface_deformation);
			const HitPoint reflection = volumetric.cast_ray(intersection.position, glm::reflect(direction, normal));

			const glm::vec3 light_hit = glm::normalize(intersection.position - light_position);
			const glm::vec3 light_reflection = glm::reflect(light_hit, normal);

			const bool facing_light = isFacingLight(intersection);
			const float reflection_coef = facing_light ? 0.5f : 0.1f;
			sf::Color color = getColorFromHitPoint(reflection, reflection_coef);

			if (facing_light) {
				const float specular_coef = std::pow(glm::dot(light_reflection, -direction), 256);
				color.r = std::min(255.0f, color.r + 255.0f * specular_coef);
				color.g = std::min(255.0f, color.g + 255.0f * specular_coef);
				color.b = std::min(255.0f, color.b + 255.0f * specular_coef);
			}

			va[x * render_size.y + y].color = color;
		}
	}

	const sf::Color getColorFromHitPoint(const HitPoint& hit_point, float color_factor = 1.0f)
	{
		sf::Color color = sf::Color(135, 181, 255);
		if (hit_point.type == Cell::Solid) {
			color = getColorFromNormal(hit_point.normal);

			const float light_intensity = getLightIntensity(hit_point);

			color.r *= light_intensity;
			color.g *= light_intensity;
			color.b *= light_intensity;
		}

		color.r *= color_factor;
		color.g *= color_factor;
		color.b *= color_factor;

		return color;
	}

	float getLightIntensity(const HitPoint& hit_point)
	{
		float light_intensity = 0.1f;

		if (isFacingLight(hit_point)) {
			const glm::vec3 hit_light = glm::normalize(light_position - hit_point.position);
			light_intensity = std::max(0.1f, glm::dot(hit_point.normal, hit_light));
		}

		return light_intensity;
	}

	bool isFacingLight(const HitPoint& hit_point)
	{
		constexpr float eps = 0.001f;
		const glm::vec3 hit_light = glm::normalize(light_position - hit_point.position);
		const HitPoint light_ray = volumetric.cast_ray(hit_point.position + eps * hit_point.normal, hit_light);

		return light_ray.type == Cell::Empty;
	}

	const sf::Color getColorFromNormal(const glm::vec3& normal)
	{
		sf::Color color;
		if (normal.x) {
			color = sf::Color::Red;
		}
		else if (normal.y) {
			color = sf::Color::Green;
		}
		else if (normal.z) {
			color = sf::Color::Blue;
		}

		return color;
	}


	const Volumetric& volumetric;
	
	const sf::Vector2i render_size;
	sf::VertexArray& va;
	
	glm::vec3 light_position;
};