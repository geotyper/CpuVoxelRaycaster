#include <iostream>
#include <SFML/Graphics.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <sstream>
#include <fstream>

#include "svo.hpp"
#include "utils.hpp"
#include "swarm.hpp"
#include "FastNoise.h"
#include "raycaster.hpp"
#include "dynamic_blur.hpp"
#include "fly_controller.hpp"
#include "replay.hpp"


int32_t main()
{
	constexpr uint32_t win_width = 1920;
	constexpr uint32_t win_height = 1080;

	sf::RenderWindow window(sf::VideoMode(win_width, win_height), "Voxels", sf::Style::Default);
	window.setMouseCursorVisible(false);
	window.setFramerateLimit(60.0f);

	constexpr float render_scale = 1.0f;
	constexpr uint32_t RENDER_WIDTH  = win_width  * render_scale;
	constexpr uint32_t RENDER_HEIGHT = win_height * render_scale;
	sf::RenderTexture render_tex;
	sf::RenderTexture denoised_tex;
	sf::RenderTexture bloom_tex;
	render_tex.create(RENDER_WIDTH, RENDER_HEIGHT);
	denoised_tex.create(RENDER_WIDTH, RENDER_HEIGHT);
	bloom_tex.create(RENDER_WIDTH, RENDER_HEIGHT);
	render_tex.setSmooth(false);

	Blur blur(RENDER_WIDTH, RENDER_HEIGHT, 0.5f);

	float movement_speed = 1.0f;

	const float body_radius = 0.4f;
	
	const glm::vec3 camera_origin(float(RENDER_WIDTH) * 0.5f, float(RENDER_HEIGHT) * 0.5f, -0.85f * float(RENDER_WIDTH));
	
	constexpr int32_t grid_size_x = 256;
	constexpr int32_t grid_size_y = 192;
	constexpr int32_t grid_size_z = 256;
	using Grid = Grid3D<grid_size_x, grid_size_y, grid_size_z>;
	Grid* grid_raw = new Grid();
	Grid& grid = *grid_raw;

	Camera camera;
	camera.position = glm::vec3(10, 120, 10);
	camera.view_angle = glm::vec2(0.0f, 0.0f);

	FlyController controller;

	FastNoise myNoise; // Create a FastNoise object
	myNoise.SetNoiseType(FastNoise::SimplexFractal); // Set the desired noise type

	for (int x = 0; x < grid_size_x; x++) {
		for (int z = 0; z < grid_size_z; z++) {
			int max_height = grid_size_y;
			int height = 32.0f * myNoise.GetNoise(x, z);

			grid.setCell(Cell::Water, x, grid_size_y - 1, z);

			for (int y(1); y < std::min(max_height, height); ++y) {
				grid.setCell(Cell::Grass, x, grid_size_y - y - 1, z);
			}
		}
	}

	int sky_mirror_offset = 50;
	for (int x = sky_mirror_offset; x < grid_size_x - sky_mirror_offset; x++) {
		for (int z = sky_mirror_offset; z < grid_size_z - sky_mirror_offset; z++) {
			if (x == sky_mirror_offset || x == grid_size_x-sky_mirror_offset -1 || z == sky_mirror_offset || z == grid_size_z-sky_mirror_offset-1) {
				grid.setCell(Cell::Solid, x, 0, z);
			}
			else {
				grid.setCell(Cell::Mirror, x, 0, z);
			}
		}
	}

	for (int x = 100; x < 150; x++) {
		for (int y(10); y < 60; ++y) {
			if (x == 100 || x == 149 || y == 10 || y == 59) {
				grid.setCell(Cell::Solid, x, grid_size_y - y - 1, 50);
			}
			else {
				grid.setCell(Cell::Mirror, x, grid_size_y - y - 1, 50);
			}
		}
	}

	for (int x = 100; x < 150; x++) {
		for (int y(10); y < 60; ++y) {
			if (x == 100 || x == 149 || y == 10 || y == 59) {
				grid.setCell(Cell::Solid, x, grid_size_y - y - 1, 150);
			}
			else {
				grid.setCell(Cell::Mirror, x, grid_size_y - y - 1, 150);
			}
		}
	}

	for (int z = 50; z < 150; z++) {
		for (int y(10); y < 60; ++y) {
			if (z == 50 || z == 149 || y == 10 || y == 59) {
				grid.setCell(Cell::Solid, 24, grid_size_y - y - 1, z);
			}
			else {
				grid.setCell(Cell::Mirror, 24, grid_size_y - y - 1, z);
			}
		}
	}

	/*for (int z = 50; z < 150; z++) {
		for (int y(10); y < 60; ++y) {
			if (z == 50 || z == 149 || y == 10 || y == 59) {
				grid.setCell(Cell::Solid, 256-24, grid_size_y - y - 1, z);
			}
			else {
				grid.setCell(Cell::Mirror, 256 - 24, grid_size_y - y - 1, z);
			}
		}
	}*/

	/*for (int i(12000); i--;) {
		grid.setCell(Cell::Solid, rand()% grid_size_x, rand() % grid_size_y, rand() % grid_size_z);
	}*/

	sf::VertexArray screen_pixels(sf::Points, RENDER_WIDTH * RENDER_HEIGHT);

	RayCaster raycaser(grid, screen_pixels, sf::Vector2i(RENDER_WIDTH, RENDER_HEIGHT));

	swrm::Swarm swarm(16);

	sf::Mouse::setPosition(sf::Vector2i(win_width / 2, win_height / 2), window);

	float time = 0.0f;

	sf::Shader shader;
	sf::Shader shader_threshold;
	sf::Shader shader_denoise;
	shader.loadFromFile("C:/Users/jeant/Documents/Code/cpp/CpuVoxelRaycaster/res/median_3.frag", sf::Shader::Fragment);
	//shader_threshold.loadFromFile("C:/Users/jeant/Documents/Code/cpp/CpuVoxelRaycaster/res/threshold.frag", sf::Shader::Fragment);
	//shader_threshold.loadFromFile("C:/Users/jeant/Documents/Code/cpp/CpuVoxelRaycaster/res/denoiser.frag", sf::Shader::Fragment);

	bool mouse_control = true;
	bool use_denoise = true;
	bool mode_demo = false;

	int32_t checker_board_offset = 0;

	sf::Texture screen_capture;
	screen_capture.create(win_width, win_height);
	uint32_t frame_count = 0U;

	bool left(false), right(false), forward(false), backward(false);

	std::ofstream stream;
	stream.open("C:/Users/jeant/Desktop/recs/lol.txt");

	std::list<ReplayElements> replay;

	while (window.isOpen())
	{
		sf::Clock frame_clock;
		const sf::Vector2i mouse_pos = sf::Mouse::getPosition(window);

		if (mouse_control) {	
			sf::Mouse::setPosition(sf::Vector2i(win_width / 2, win_height / 2), window);
			const float mouse_sensitivity = 0.005f;
			controller.updateCameraView(mouse_sensitivity * glm::vec2(mouse_pos.x - win_width  * 0.5f, (win_height  * 0.5f) - mouse_pos.y), camera);
		}

		glm::vec3 camera_vec = glm::rotate(glm::vec3(0.0f, 0.0f, 1.0f), camera.view_angle.y, glm::vec3(1.0f, 0.0f, 0.0f));
		camera_vec = glm::rotate(camera_vec, camera.view_angle.x, glm::vec3(0.0f, 1.0f, 0.0f));

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
						replay = ReplayElements::loadFromFile("C:/Users/jeant/Desktop/recs/lol3.txt");
						time = 0.0f;
					}
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
				default:
					break;
				}
			}
		}

		if (forward) {
			move += camera_vec * movement_speed;
		}
		else if (backward) {
			move -= camera_vec * movement_speed;
		}

		if (left) {
			move += glm::vec3(-camera_vec.z, 0.0f, camera_vec.x) * movement_speed;
		}
		else if (right) {
			move -= glm::vec3(-camera_vec.z, 0.0f, camera_vec.x) * movement_speed;
		}

		if (!replay.empty()) {
			std::cout << replay.front().timestamp << std::endl;
			while (time > replay.front().timestamp) {
				const auto& last = replay.front();
				
				camera.position.x = last.x;
				camera.position.y = last.y;
				camera.position.z = last.z;

				camera.view_angle.x = last.view_x;
				camera.view_angle.y = last.view_y;

				replay.pop_front();
				if (replay.empty()) {
					break;
				}
			}
		}

		controller.move(move, camera, grid);
		
		const glm::vec3 light_position = glm::vec3(grid_size_x*0.5f, 0.0f, grid_size_z*0.5f) + glm::rotate(glm::vec3(0.0f, -500.0f, 1000.0f), 0.2f * time, glm::vec3(0.0f, 1.0f, 0.0f));
		raycaser.setLightPosition(light_position);

		checker_board_offset = 1 - checker_board_offset;

		/*if (mode_demo) {
			camera.position.x = (0.5f * grid_size_x - 10.0f) * cos(0.1f * time) + 0.5f * grid_size_x;
			camera.position.z = (0.5f * grid_size_z - 10.0f) * sin(0.1f * time) + 0.5f * grid_size_z;
			camera.position.y = 90.0 + 80.0f*sin(0.1f * time);

			camera.view_angle.x = -0.1f * time - PI * 0.5f;
			camera.view_angle.y = -PI*0.15f + PI*0.1f * sin(0.1f * time);
		}*/

		auto group = swarm.execute([&](uint32_t thread_id, uint32_t max_thread) {
			const uint32_t area_width = RENDER_WIDTH / 4;
			const uint32_t area_height = RENDER_HEIGHT / 4;
			const uint32_t start_x = thread_id % 4;
			const uint32_t start_y = thread_id / 4;

			for (int x(start_x * area_width); x < (start_x + 1) * area_width; ++x) {
				for (int y(start_y * area_height); y < (start_y + 1) * area_height; y++) {
					const glm::vec3 screen_position(x, y, 0.0f);
					glm::vec3 ray = glm::rotate(screen_position - camera_origin, camera.view_angle.y, glm::vec3(1.0f, 0.0f, 0.0f));
					ray = glm::rotate(ray, camera.view_angle.x, glm::vec3(0.0f, 1.0f, 0.0f));

					raycaser.render_ray(sf::Vector2i(x, y), camera.position, glm::normalize(ray), time);
				}
			}
		}, 16);

		group.waitExecutionDone();

		window.clear(sf::Color::Black);
		render_tex.draw(screen_pixels);
		render_tex.display();

		/*denoised_tex.draw(sf::Sprite(render_tex.getTexture()), &shader_denoise);
		denoised_tex.display();

		render_tex.draw(sf::Sprite(denoised_tex.getTexture()));
		render_tex.display();*/

		bloom_tex.draw(sf::Sprite(render_tex.getTexture()), &shader_threshold);
		bloom_tex.display();

		render_tex.draw(blur.apply(bloom_tex.getTexture(), 1), sf::BlendAdd);
		render_tex.display();
		
		sf::Sprite render_sprite(render_tex.getTexture());
		render_sprite.setScale(1.0f / render_scale, 1.0f / render_scale);

		if (use_denoise && !mode_demo) {
			window.draw(render_sprite, &shader);
		}
		else {
			window.draw(render_sprite);
		}
		window.display();

		if (mode_demo) {
			screen_capture.update(window);
			sf::Image image = screen_capture.copyToImage();

			std::stringstream ssx;
			ssx << "C:/Users/jeant/Desktop/voxels_dumps/img_" << frame_count++ << ".bmp";
			image.saveToFile(ssx.str());
		}

		stream << time << " " << camera.position.x << " " << camera.position.y << " " << camera.position.z << " " << camera.view_angle.x << " " << camera.view_angle.y << std::endl;

		if (!mode_demo) {
			time += frame_clock.getElapsedTime().asSeconds();
		}
		else {
			time += 0.016f;
		}
	}

	stream.close();
}