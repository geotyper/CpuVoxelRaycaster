#include <iostream>
#include <SFML/Graphics.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <sstream>
#include <fstream>

#include "svo.hpp"
#include "grid_3d.hpp"
#include "utils.hpp"
#include "swarm.hpp"
#include "FastNoise.h"
#include "raycaster.hpp"
#include "dynamic_blur.hpp"
#include "fly_controller.hpp"
#include "replay.hpp"


int32_t main()
{
	constexpr uint32_t win_width = 1600;
	constexpr uint32_t win_height = 900;

	sf::RenderWindow window(sf::VideoMode(win_width, win_height), "Voxels", sf::Style::Default);
	window.setMouseCursorVisible(false);

	constexpr float render_scale = 1.0f;
	constexpr uint32_t RENDER_WIDTH = uint32_t(win_width  * render_scale);
	constexpr uint32_t RENDER_HEIGHT = uint32_t(win_height * render_scale);
	sf::RenderTexture render_tex;
	sf::RenderTexture denoised_tex;
	sf::RenderTexture bloom_tex;
	render_tex.create(RENDER_WIDTH, RENDER_HEIGHT);
	denoised_tex.create(RENDER_WIDTH, RENDER_HEIGHT);
	bloom_tex.create(RENDER_WIDTH, RENDER_HEIGHT);
	render_tex.setSmooth(false);

	Blur blur(RENDER_WIDTH, RENDER_HEIGHT, 0.5f);

	float movement_speed = 2.5f;

	const float body_radius = 0.4f;

	const glm::vec3 camera_origin(0.0f, 0.0f, 0.0f);

	constexpr int32_t size = 512;
	constexpr int32_t grid_size_x = size;
	constexpr int32_t grid_size_y = size / 2;
	constexpr int32_t grid_size_z = size;
	using Volume = SVO;
	Volume* volume_raw = new Volume();
	Volume& volume = *volume_raw;

	Camera camera;
	camera.position = glm::vec3(256, 10, 256);
	camera.view_angle = glm::vec2(0.0f);
	camera.fov = 1.0f;

	FlyController controller;

	FastNoise myNoise;
	myNoise.SetNoiseType(FastNoise::SimplexFractal);
	for (uint32_t x = 0; x < grid_size_x; x++) {
		for (uint32_t z = 0; z < grid_size_z; z++) {
			int32_t max_height = grid_size_y;
			float amp_x = x - grid_size_x * 0.5f;
			float amp_z = z - grid_size_z * 0.5f;
			float ratio = std::pow(1.0f - sqrt(amp_x * amp_x + amp_z * amp_z) / (10.0f * grid_size_x), 256.0f);
			int32_t height = int32_t(64.0f * myNoise.GetNoise(float(0.75f * x), float(0.75f * z)) + 32);

			volume.setCell(Cell::Mirror, Cell::None, x, grid_size_y - 1, z);
			//volume.setCell(Cell::Solid, Cell::Grass, x, 0, z);

			for (int y(1); y < std::min(max_height, height); ++y) {
				volume.setCell(Cell::Solid, Cell::Grass, x, grid_size_y - y - 1, z);
			}
		}
	}


	RayCaster raycaster(volume, sf::Vector2i(RENDER_WIDTH, RENDER_HEIGHT));

	const uint32_t thread_count = 16U;
	const uint32_t area_count = uint32_t(sqrt(thread_count));
	swrm::Swarm swarm(thread_count);

	sf::Mouse::setPosition(sf::Vector2i(win_width / 2, win_height / 2), window);

	float time = 0.0f;

	bool mouse_control = true;
	bool use_denoise = false;
	bool mode_demo = false;

	int32_t checker_board_offset = 0;
	uint32_t frame_count = 0U;

	bool left(false), right(false), forward(false), backward(false), up(false);

	while (window.isOpen())
	{
		sf::Clock frame_clock;
		const sf::Vector2i mouse_pos = sf::Mouse::getPosition(window);

		if (mouse_control) {
			sf::Mouse::setPosition(sf::Vector2i(win_width / 2, win_height / 2), window);
			const float mouse_sensitivity = 0.005f;
			controller.updateCameraView(mouse_sensitivity * glm::vec2(mouse_pos.x - win_width * 0.5f, (win_height  * 0.5f) - mouse_pos.y), camera);
		}

		glm::vec3 move = glm::vec3(0.0f);
		sf::Event event;
		while (window.pollEvent(event)) {
			if (event.type == sf::Event::Closed)
				window.close();

			if (event.type == sf::Event::KeyPressed) {
				switch (event.key.code) {
				case sf::Keyboard::Escape:
					window.close();
					break;
				case sf::Keyboard::Z:
					forward = true;
					break;
				case sf::Keyboard::S:
					backward = true;
					break;
				case sf::Keyboard::Q:
					left = true;
					break;
				case sf::Keyboard::D:
					right = true;
					break;
				case sf::Keyboard::O:
					raycaster.use_ao = !raycaster.use_ao;
					break;
				case sf::Keyboard::Space:
					up = true;
					break;
				case sf::Keyboard::A:
					use_denoise = !use_denoise;
					break;
				case sf::Keyboard::E:
					mouse_control = !mouse_control;
					window.setMouseCursorVisible(!mouse_control);
					break;
				case sf::Keyboard::F:
					mode_demo = !mode_demo;
					window.setMouseCursorVisible(!mode_demo && !mouse_control);
					if (mode_demo) {
						time = 0.0f;
					}
					break;
				case sf::Keyboard::Up:
					break;
				case sf::Keyboard::Down:
					break;
				case sf::Keyboard::Right:
					camera.aperture += 0.1f;
					break;
				case sf::Keyboard::Left:
					camera.aperture -= 0.1f;
					if (camera.aperture < 0.0f) {
						camera.aperture = 0.0f;
					}
					break;
				case sf::Keyboard::R:
					raycaster.use_samples = !raycaster.use_samples;
					if (raycaster.use_samples) {
						raycaster.resetSamples();
					}
					break;
				case sf::Keyboard::G:
					raycaster.use_gi = !raycaster.use_gi;
					break;
				case sf::Keyboard::H:
					raycaster.use_god_rays = !raycaster.use_god_rays;
					break;
				default:
					break;
				}
			}
			else if (event.type == sf::Event::KeyReleased) {
				switch (event.key.code) {
				case sf::Keyboard::Z:
					forward = false;
					break;
				case sf::Keyboard::S:
					backward = false;
					break;
				case sf::Keyboard::Q:
					left = false;
					break;
				case sf::Keyboard::D:
					right = false;
					break;
				case sf::Keyboard::Space:
					up = false;
					break;
				default:
					break;
				}
			}
		}

		if (forward) {
			move += camera.camera_vec * movement_speed;
		}
		else if (backward) {
			move -= camera.camera_vec * movement_speed;
		}

		if (left) {
			move += glm::vec3(-camera.camera_vec.z, 0.0f, camera.camera_vec.x) * movement_speed;
		}
		else if (right) {
			move -= glm::vec3(-camera.camera_vec.z, 0.0f, camera.camera_vec.x) * movement_speed;
		}

		if (up) {
			move += glm::vec3(0.0f, -1.0f, 0.0f) * movement_speed;
		}

		controller.move(move, camera);

		const glm::vec3 light_position = glm::vec3(100, 100, 30);
		raycaster.setLightPosition(light_position);

		sf::Clock render_clock;
		uint32_t ray_count = 0U;

		// Computing camera's focal length based on aimed point
		HitPoint closest_point = camera.getClosestPoint(volume);
		if (closest_point.cell) {
			camera.focal_length = closest_point.distance;
		}
		else {
			camera.focal_length = 100.0f;
		}

		// Computing some constants, could be done outside main loop
		const uint32_t area_width = RENDER_WIDTH / area_count;
		const uint32_t area_height = RENDER_HEIGHT / area_count;
		const float aspect_ratio = float(RENDER_WIDTH) / float(RENDER_HEIGHT);
		const uint32_t rays = raycaster.use_samples ? 32000U : 8000U;

		// Change checker board offset ot render the other pixels
		checker_board_offset = 1 - checker_board_offset;
		// The actual raycasting
		auto group = swarm.execute([&](uint32_t thread_id, uint32_t max_thread) {
			const uint32_t start_x = thread_id % 4;
			const uint32_t start_y = thread_id / 4;
			for (uint32_t x(start_x * area_width); x < (start_x + 1) * area_width; ++x) {
				for (uint32_t y(start_y * area_height + (x + checker_board_offset) % 2); y < (start_y + 1) * area_height; y += 2) {
					++ray_count;
					// Computing ray coordinates in 'lens' space ie in normalized screen space
					const float lens_x = float(x) / float(RENDER_HEIGHT) - aspect_ratio * 0.5f;
					const float lens_y = float(y) / float(RENDER_HEIGHT) - 0.5f;
					// Get ray to cast with stochastic blur baked into it
					const CameraRay camera_ray = camera.getRay(glm::vec2(lens_x, lens_y));

					raycaster.renderRay(sf::Vector2i(x, y), camera.position + camera_ray.world_rand_offset, camera_ray.ray, time);
				}
			}
		});

		// Wait for threads to terminate
		group.waitExecutionDone();

		if (raycaster.use_samples) {
			raycaster.samples_to_image();
		}

		// Add some persistence to reduce the noise
		const float old_value_conservation = raycaster.use_samples ? 0.0f : 0.75f;
		sf::RectangleShape cache1(sf::Vector2f(RENDER_WIDTH, RENDER_HEIGHT));
		cache1.setFillColor(sf::Color(255 * old_value_conservation, 255 * old_value_conservation, 255 * old_value_conservation));
		sf::RectangleShape cache2(sf::Vector2f(win_width, win_height));
		const float c2 = 255 * (1.0f - old_value_conservation);
		cache2.setFillColor(sf::Color(c2, c2, c2));
		// Draw image to final render texture
		sf::Texture texture;
		texture.loadFromImage(raycaster.render_image);
		render_tex.draw(sf::Sprite(texture));
		render_tex.draw(cache2, sf::BlendMultiply);
		render_tex.display();
		// Not really denoised but OK
		denoised_tex.draw(cache1, sf::BlendMultiply);
		sf::Sprite render_sprite(render_tex.getTexture());
		denoised_tex.draw(render_sprite, sf::BlendAdd);
		denoised_tex.display();
		// Scale and render
		sf::Sprite final_sprite(denoised_tex.getTexture());
		final_sprite.setScale(1.0f / render_scale, 1.0f / render_scale);
		window.draw(final_sprite);
		window.display();

		time += frame_clock.getElapsedTime().asSeconds();
	}
}