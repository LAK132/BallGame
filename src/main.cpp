#define APP_NAME "Ball Game"

#define LAK_BASIC_PROGRAM_IMGUI_IMPL
#include <lak/basic_program.inl>

#include <lak/file.hpp>

#include <lak/structure/obj.hpp>
#include <lak/structure/pnm.hpp>

#include <lak/opengl/mesh.hpp>
#include <lak/opengl/shader.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "space.hpp"

#include <cinttypes>

enum state_t
{
	LOADING,
	RUNNING,
	WIN,
	LOSS
};

state_t state = LOADING;

struct camera
{
	lak::shared_ptr<reference_frame> frame;
	glm::mat4 projection;
	glm::mat4 view;

	inline glm::mat4 &update_projection(const lak::window &w)
	{
		return projection = glm::perspective(glm::pi<float>() / 2.0f,
		                                     (float)w.drawable_size().x /
		                                       (float)w.drawable_size().y,
		                                     0.01f,
		                                     100.0f);
	}

	inline glm::mat4 &update_view()
	{
		const auto trans   = frame->get_transform();
		const auto pos     = glm::vec3(trans * glm::vec4(0, 0, 0, 1));
		const auto forward = glm::vec3(trans * glm::vec4(0, -1, 0, 1));
		const auto up      = glm::vec3(trans * glm::vec4(0, 0, 1, 0));
		return view        = glm::lookAt(pos, forward, glm::normalize(up));
	}

	inline glm::mat4 update_projview(const lak::window &w)
	{
		return update_projection(w) * update_view();
	}
};

struct light
{
	lak::shared_ptr<reference_frame> frame;
	glm::vec4 colour = {0.5f, 0.5f, 0.5f, 1.0f};
};

struct model
{
	lak::shared_ptr<reference_frame> frame;
	lak::shared_ptr<lak::opengl::static_object_part> mesh;

	void draw()
	{
		auto model_transform = frame->get_transform();
		mesh->shader()->assert_set_uniform("model",
		                                   lak::as_bytes(&model_transform));
		mesh->draw();
	}
};

struct vertex
{
	glm::vec4 pos;
	glm::vec4 col;
	glm::vec3 norm;
	glm::vec2 tex_coord;

	static lak::array<lak::opengl::vertex_attribute> attributes()
	{
		return lak::array<lak::opengl::vertex_attribute>{
		  {
		    .size       = 4,
		    .type       = GL_FLOAT,
		    .normalised = GL_FALSE,
		    .stride     = sizeof(vertex),
		    .offset     = offsetof(vertex, pos),
		    .divisor    = 0,
		  },
		  {
		    .size       = 4,
		    .type       = GL_FLOAT,
		    .normalised = GL_FALSE,
		    .stride     = sizeof(vertex),
		    .offset     = offsetof(vertex, col),
		    .divisor    = 0,
		  },
		  {
		    .size       = 3,
		    .type       = GL_FLOAT,
		    .normalised = GL_FALSE,
		    .stride     = sizeof(vertex),
		    .offset     = offsetof(vertex, norm),
		    .divisor    = 0,
		  },
		  {
		    .size       = 2,
		    .type       = GL_FLOAT,
		    .normalised = GL_FALSE,
		    .stride     = sizeof(vertex),
		    .offset     = offsetof(vertex, tex_coord),
		    .divisor    = 0,
		  }};
	}

	static lak::array<GLuint, 4U> attribute_indices(
	  const lak::opengl::program &shader,
	  const GLchar *pos_name,
	  const GLchar *col_name,
	  const GLchar *norm_name,
	  const GLchar *tex_coord_name)
	{
		return lak::array<GLuint, 4U>{
		  shader.assert_attrib_index(pos_name),
		  shader.assert_attrib_index(col_name),
		  shader.assert_attrib_index(norm_name),
		  shader.assert_attrib_index(tex_coord_name),
		};
	}
};

lak::shared_ptr<lak::opengl::static_object_part> make_mesh(
  lak::span<const vertex> vertices,
  GLenum draw_mode,
  lak::opengl::shared_program shader,
  lak::shared_ptr<lak::opengl::texture> albedo)
{
	auto buffer = lak::opengl::shared_vertex_buffer::make(
	  lak::opengl::vertex_buffer::create());
	buffer->bind()
	  .set_data(vertices, vertices.size(), draw_mode)
	  .set_vertex_attributes(vertex::attributes());
	return lak::shared_ptr<lak::opengl::static_object_part>::make(
	  lak::opengl::static_object_part::create(
	    buffer,
	    shader,
	    vertex::attribute_indices(
	      *shader, "vPosition", "vColor", "vNormal", "vTexCoord"),
	    {{albedo, shader->assert_uniform_location("albedo")}}));
}

lak::array<vertex> load_model_file(const lak::fs::path &path)
{
	auto model_file = lak::read_file(path).EXPECT("failed to open ", path);
	lak::binary_reader strm{model_file};
	auto obj = strm.read<lak::obj::obj>().EXPECT("failed to read obj ", path);
	lak::array<vertex> result;
	for (const auto &face : obj.faces)
	{
		face.visit_fan(
		  obj.vertex_coords,
		  obj.texture_coords,
		  obj.vertex_normals,
		  obj.face_coords,
		  [&](const lak::obj::vertex_coord &v,
		      const lak::obj::texture_coord *vt,
		      const lak::obj::vertex_normal *vn)
		  {
			  result.push_back(vertex{
			    .pos  = glm::vec4{float(v.x), float(v.z), float(v.y), float(v.w)},
			    .col  = glm::vec4{1.0, 1.0, 1.0, 1.0},
			    .norm = vn ? glm::vec3{float(vn->x), float(vn->z), float(vn->y)}
			               : glm::vec3{0.0},
			    // lak::image is top-left origin, opengl is bottom-left
			    .tex_coord =
			      vt ? glm::vec2{float(vt->u), -float(vt->v)} : glm::vec2{0.0},
			  });
		  });
	}
	return result;
}

lak::image3_t load_texture3_file(const lak::fs::path &path)
{
	auto tex_file = lak::read_file(path).EXPECT("failed to open ", path);
	lak::binary_reader strm{tex_file};
	auto pnm = strm.read<lak::pnm::pnm>().EXPECT("failed to read pnm ", path);
	return static_cast<lak::image3_t>(pnm);
}


lak::opengl::texture load_opengl_texture(const lak::image3_t &img)
{
	lak::opengl::texture tex(GL_TEXTURE_2D);
	tex.bind()
	  .apply(GL_TEXTURE_WRAP_S, GL_REPEAT)
	  .apply(GL_TEXTURE_WRAP_T, GL_REPEAT)
	  .apply(GL_TEXTURE_MIN_FILTER, GL_LINEAR)
	  .apply(GL_TEXTURE_MAG_FILTER, GL_NEAREST)
	  .build(0,
	         GL_RGB,
	         (lak::vec2<GLsizei>)img.size(),
	         0,
	         GL_RGB,
	         GL_UNSIGNED_BYTE,
	         img.data());
	return tex;
}


struct scene
{
	lak::shared_ptr<lak::opengl::program> shader;
	lak::shared_ptr<reference_frame> world;
	lak::shared_ptr<reference_frame> player;
	lak::shared_ptr<reference_frame> cameraBoom;
	::camera camera;
	lak::array<light> lights;
	lak::array<model> blocks;
	lak::array<model> coins;
	lak::array<model> coins_reset;
	model ball;
};

struct user_data
{
	scene scene;
};

user_data ud;

lak::optional<int> basic_program_init(int argc, char **argv)
{
	LAK_UNUSED(argc);
	LAK_UNUSED(argv);

	basic_window_target_framerate                = 60;
	basic_window_opengl_settings.major           = 3;
	basic_window_opengl_settings.minor           = 2;
	basic_window_opengl_settings.double_buffered = true;
	basic_window_clear_colour = {0.0f, 0.3125f, 0.3125f, 1.0f};

	auto gr = basic_create_window().UNWRAP().graphics();
	ASSERT_EQUAL(gr, lak::graphics_mode::OpenGL);

	return lak::nullopt;
}

bool game_running = true;
bool basic_program_loop(uint64_t counter_delta)
{
	LAK_UNUSED(counter_delta);
	return game_running && !basic_window_instances.empty();
}

int basic_program_quit() { return EXIT_SUCCESS; }

std::atomic_bool assets_loaded = false;

lak::image3_t ball_texture;
lak::array<vertex> ball_vertices;
lak::image3_t cube_texture;
lak::array<vertex> cube_vertices;
lak::image3_t coin_texture;
lak::array<vertex> coin_vertices;
lak::image3_t map_texture;

void load_assets()
{
	lak::fs::path assets_dir = "assets";

	ball_texture  = load_texture3_file(assets_dir / "ball.ppm");
	ball_vertices = load_model_file(assets_dir / "ball.obj");

	cube_texture  = load_texture3_file(assets_dir / "cube.ppm");
	cube_vertices = load_model_file(assets_dir / "cube.obj");

	coin_texture  = load_texture3_file(assets_dir / "coin.ppm");
	coin_vertices = load_model_file(assets_dir / "coin.obj");

	map_texture = load_texture3_file(assets_dir / "map.ppm");

	assets_loaded = true;
}

lak::optional<std::thread> asset_loader;

bool init_game_state()
{
	if (assets_loaded)
	{
		if (asset_loader)
		{
			asset_loader->join();
			asset_loader.reset();
		}

		{
			using namespace lak::opengl::literals;
			auto vshader = R"(
#version 330 core

in vec4 vPosition;
in vec4 vColor;
in vec3 vNormal;
in vec2 vTexCoord;

uniform mat4 projview;
uniform mat4 invprojview;
uniform mat4 model;

out vec4 fColor;
out vec3 fNormal;
out vec2 fTexCoord;
out vec3 fPosition;
out vec3 fEye;

const vec4 WUP = vec4(0.0, 0.0, 0.0, 1.0);

void main()
{
	vec4 vertpos = model * vPosition; // object -> world space

	fTexCoord = vTexCoord;
	// fColor = vPosition;
	fColor = vColor;
	fEye = vec3(WUP * invprojview); // screen -> camera -> world space
	fNormal = mat3(model) * vNormal; // object -> world space (no translation/scale)
	fPosition = vertpos.xyz;

	gl_Position = projview * vertpos; // world -> camera -> screen space
})"_vertex_shader.UNWRAP();

			auto fshader = R"(
#version 330 core

smooth in vec4 fColor;
smooth in vec3 fEye;
smooth in vec3 fNormal;
smooth in vec3 fPosition;
smooth in vec2 fTexCoord;

uniform sampler2D albedo;

uniform vec4 ambient;
uniform vec4 diffuse;
uniform vec4 specular;
uniform float shininess;

#define MAX_LIGHTS 6
struct light {
	vec3 position;
	vec4 color;
};
uniform int lightCount;
uniform light lights[MAX_LIGHTS];

out vec4 pColor;

void main()
{
	vec3 normal = normalize(fNormal);
	vec3 viewDir = normalize(fEye - fPosition);
	vec4 texColor = texture(albedo, fTexCoord); // * fColor;

	pColor = ambient * mix(fColor, texColor, texColor.w);

	for(int i = 0; i < lightCount && i < MAX_LIGHTS; i++)
	{
		vec3 lightDir = normalize(lights[i].position - fPosition);
		float dNL = max(dot(normal, lightDir), 0.0f);
		vec4 color = diffuse;
		color = diffuse * texColor;
		vec4 lambert = color * lights[i].color * dNL;

		vec3 halfVec = normalize(lightDir + viewDir);
		float dNH = max(dot(normal, halfVec), 0.0f);
		vec4 phong = specular * lights[i].color * pow(dNH, shininess);
		pColor += lambert + phong;
	}
})"_fragment_shader.UNWRAP();

			ud.scene.shader =
			  lak::opengl::program::create_shared(vshader, fshader).UNWRAP();
			ud.scene.shader->use().UNWRAP();
		}

		ud.scene.world      = lak::shared_ptr<reference_frame>::make();
		ud.scene.player     = ud.scene.world->add_child();
		ud.scene.cameraBoom = ud.scene.player->add_child();

		ud.scene.camera = camera{.frame = ud.scene.cameraBoom->add_child()};

		ud.scene.cameraBoom->rotation.value.x      = 0.58f;
		ud.scene.camera.frame->translation.value.y = 2.2f;
		ud.scene.camera.frame->translation.value.z = 0.7f;

		{
			auto albedo = lak::shared_ptr<lak::opengl::texture>::make(
			  load_opengl_texture(ball_texture));

			ud.scene.ball = model{
			  .frame = ud.scene.player->add_child(),
			  .mesh =
			    make_mesh(ball_vertices, GL_TRIANGLES, ud.scene.shader, albedo),
			};
		}

		{
			auto albedo = lak::shared_ptr<lak::opengl::texture>::make(
			  load_opengl_texture(cube_texture));

			auto obj_part =
			  make_mesh(cube_vertices, GL_TRIANGLES, ud.scene.shader, albedo);

			ud.scene.blocks.clear();
			ud.scene.blocks.reserve(map_texture.size().x * map_texture.size().y);
			for (size_t x = 0; x < map_texture.size().x; x++)
			{
				for (size_t y = 0; y < map_texture.size().y; y++)
				{
					if (map_texture[{x, y}].r > 0)
					{
						auto &block = ud.scene.blocks.push_back(model{
						  .frame = lak::shared_ptr<reference_frame>::make(),
						  .mesh  = obj_part,
						});

						block.frame->translation.value = {x * 2.0f, y * -2.0f, -2.0f};
					}
				}
			}
		}

		{
			auto albedo = lak::shared_ptr<lak::opengl::texture>::make(
			  load_opengl_texture(coin_texture));

			auto obj_part =
			  make_mesh(coin_vertices, GL_TRIANGLES, ud.scene.shader, albedo);

			ud.scene.coins.clear();
			ud.scene.coins.reserve(map_texture.size().x * map_texture.size().y);
			for (size_t x = 0; x < map_texture.size().x; x++)
			{
				for (size_t y = 0; y < map_texture.size().y; y++)
				{
					if (map_texture[{x, y}].g > 0)
					{
						auto &coin = ud.scene.coins.push_back(model{
						  .frame = lak::shared_ptr<reference_frame>::make(),
						  .mesh  = obj_part,
						});

						coin.frame->translation.value   = {x * 2.0f, y * -2.0f, 0.0f};
						coin.frame->rotation.velocity.z = 1.0f;
					}
				}
			}
			ud.scene.coins_reset = ud.scene.coins;
		}

		{
			ud.scene.lights.clear();
			for (size_t x = 0; x < map_texture.size().x; x++)
			{
				for (size_t y = 0; y < map_texture.size().y; y++)
				{
					if (map_texture[{x, y}].b > 0)
					{
						auto &li = ud.scene.lights.push_back(light{
						  .frame  = lak::shared_ptr<reference_frame>::make(),
						  .colour = {0.5f, 0.5f, 0.5f, 1.0f},
						});

						li.frame->translation.value = {x * 2.0f, y * -2.0f, 2.0f};
					}
				}
			}

			size_t lightCount = std::min<size_t>(6U, ud.scene.lights.size());
			ud.scene.shader->assert_set_uniform("lightCount",
			                                    lak::as_bytes(&lightCount));

			for (size_t i = 0; i < lightCount; i++)
			{
				auto lightname = "lights["_str + (char)('0' + i) + "]"_str;
				ud.scene.shader->assert_set_uniform(
				  (lightname + ".position").c_str(),
				  lak::as_bytes(&ud.scene.lights[i].frame->translation.value));
				ud.scene.shader->assert_set_uniform(
				  (lightname + ".color").c_str(),
				  lak::as_bytes(&ud.scene.lights[i].colour));
			}

			auto temp_vec = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
			ud.scene.shader->assert_set_uniform("ambient", lak::as_bytes(&temp_vec));
			temp_vec = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
			ud.scene.shader->assert_set_uniform("diffuse", lak::as_bytes(&temp_vec));
			temp_vec = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
			ud.scene.shader->assert_set_uniform("specular",
			                                    lak::as_bytes(&temp_vec));
			float shininess = 100.0f;
			ud.scene.shader->assert_set_uniform("shininess",
			                                    lak::as_bytes(&shininess));
			auto temp_mat = glm::mat4(1.0f);
			ud.scene.shader->assert_set_uniform("model", lak::as_bytes(&temp_mat));
		}

		return true;
	}

	if (!asset_loader)
	{
		asset_loader = std::thread(load_assets);
	}

	return false;
}

void basic_window_init(lak::window &window) { LAK_UNUSED(window); }

void basic_window_handle_event(lak::window *window, lak::event &event)
{
	switch (event.type)
	{
		case lak::event_type::window_closed:
			break;

		case lak::event_type::close_window:
			ASSERT(!!window);
			basic_destroy_window(*window);
			break;

		case lak::event_type::quit_program:
			game_running = false;
			break;

		case lak::event_type::key_down:
			switch (event.key().scancode)
			{
				case 79: // right
					ud.scene.player->rotation.velocity.z = -2;
					break;
				case 80: // left
					ud.scene.player->rotation.velocity.z = 2;
					break;
				case 81: // down
					ud.scene.ball.frame->rotation.acceleration.x = -3;
					break;
				case 82: // up
					ud.scene.ball.frame->rotation.acceleration.x = 3;
					break;
			}
			break;

		case lak::event_type::key_up:
			switch (event.key().scancode)
			{
				case 79: // right
				case 80: // left
					ud.scene.player->rotation.velocity.z = 0;
					break;
				case 81: // down
				case 82: // up
					ud.scene.ball.frame->rotation.acceleration.x = 0;
					break;
			}
			break;

		default:
			break;
	}
}

void basic_window_loop(lak::window &window, uint64_t counter_delta)
{
	const float frame_time = (float)counter_delta / lak::performance_frequency();

	ImGuiIO &io = ImGui::GetIO();

	bool mainOpen = true;

	if (state == state_t::RUNNING)
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	else
		ImGui::SetNextWindowPos(
		  ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
		  ImGuiCond_Always,
		  ImVec2(0.5f, 0.5f));
	ImGui::Begin(APP_NAME,
	             &mainOpen,
	             ImGuiWindowFlags_AlwaysAutoResize |
	               ImGuiWindowFlags_NoScrollbar |
	               ImGuiWindowFlags_NoSavedSettings |
	               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);

	switch (state)
	{
		case state_t::LOADING:
		{
			ImGui::Text("Loading...");
			if (init_game_state()) state = state_t::RUNNING;
			ImGui::End();
			return;
		}
		break;

		case state_t::RUNNING:
		{
			auto &scene = ud.scene;

			scene.player->translation.velocity.x =
			  std::sin(scene.player->rotation.value.z) *
			  scene.ball.frame->rotation.velocity.x;

			scene.player->translation.velocity.y =
			  -std::cos(scene.player->rotation.value.z) *
			  scene.ball.frame->rotation.velocity.x;

			scene.world->update(frame_time);
			scene.player->update(frame_time);
			scene.cameraBoom->update(frame_time);
			scene.camera.frame->update(frame_time);

			for (auto &light : scene.lights) light.frame->update(frame_time);
			for (auto &block : scene.blocks) block.frame->update(frame_time);
			for (auto &coin : scene.coins) coin.frame->update(frame_time);

			scene.ball.frame->update(frame_time);

			auto player_world_pos = scene.player->total_translation();
			for (auto it = scene.coins.begin(); it != scene.coins.end();)
			{
				glm::vec3 dist = it->frame->total_translation() - player_world_pos;
				float dst      = std::sqrt((dist.x * dist.x) + (dist.y * dist.y));

				if (dst < 1.0f)
					it = scene.coins.erase(it);
				else
					++it;
			}
			if (ud.scene.coins.empty()) state = WIN;

			bool onTrack = false;
			for (auto &block : scene.blocks)
			{
				glm::vec3 dist = block.frame->total_translation() - player_world_pos;
				onTrack |= std::abs(dist.x) <= 1.0f && std::abs(dist.y) <= 1.0f;
			}
			if (!onTrack) state = LOSS;

			ImGui::Text("Score");
			ImGui::Text("%zu/%zu",
			            ud.scene.coins_reset.size() - ud.scene.coins.size(),
			            ud.scene.coins_reset.size());
		}
		break;

		case state_t::WIN:
		{
			auto &scene = ud.scene;

			ImGui::Text("YOUR'RE WINNER !");
			if (ImGui::Button("restart"))
			{
				scene.player->translation.value     = glm::vec3(0);
				scene.player->translation.velocity  = glm::vec3(0);
				scene.player->rotation.value        = glm::vec3(0);
				scene.player->rotation.velocity     = glm::vec3(0);
				scene.ball.frame->rotation.value    = glm::vec3(0);
				scene.ball.frame->rotation.velocity = glm::vec3(0);
				scene.coins                         = scene.coins_reset;
				state                               = RUNNING;
			}
		}
		break;

		case state_t::LOSS:
		{
			auto &scene = ud.scene;

			ImGui::Text("you fell off :(");
			if (ImGui::Button("try again"))
			{
				scene.player->translation.value     = glm::vec3(0);
				scene.player->translation.velocity  = glm::vec3(0);
				scene.player->rotation.value        = glm::vec3(0);
				scene.player->rotation.velocity     = glm::vec3(0);
				scene.ball.frame->rotation.value    = glm::vec3(0);
				scene.ball.frame->rotation.velocity = glm::vec3(0);
				scene.coins                         = scene.coins_reset;
				state                               = RUNNING;
			}
		}
		break;
	}

	{
		auto projview    = ud.scene.camera.update_projview(window);
		auto invprojview = glm::transpose(glm::inverse(projview));
		ud.scene.shader->assert_set_uniform("projview", lak::as_bytes(&projview));
		ud.scene.shader->assert_set_uniform("invprojview",
		                                    lak::as_bytes(&invprojview));
	}

	lak::opengl::enable_if(GL_BLEND, true).UNWRAP();
	lak::opengl::call_checked(glBlendEquationSeparate, GL_FUNC_ADD, GL_FUNC_ADD)
	  .UNWRAP();
	lak::opengl::call_checked(glBlendFuncSeparate,
	                          GL_SRC_ALPHA,
	                          GL_ONE_MINUS_SRC_ALPHA,
	                          GL_SRC_ALPHA,
	                          GL_ONE_MINUS_SRC_ALPHA)
	  .UNWRAP();

	lak::opengl::enable_if(GL_DEPTH_TEST, true).UNWRAP();
	lak::opengl::call_checked(glDepthFunc, GL_LESS).UNWRAP();
	lak::opengl::call_checked(glDepthRange, GLdouble(0.0), GLdouble(1.0))
	  .UNWRAP();

	lak::opengl::enable_if(GL_CULL_FACE, false).UNWRAP();

	lak::opengl::enable_if(GL_SCISSOR_TEST, false).UNWRAP();

	lak::opengl::call_checked(glViewport,
	                          static_cast<GLint>(0),
	                          static_cast<GLint>(0),
	                          static_cast<GLsizei>(window.drawable_size().x),
	                          static_cast<GLsizei>(window.drawable_size().y))
	  .UNWRAP();

	ud.scene.ball.draw();
	for (auto &it : ud.scene.blocks) it.draw();
	for (auto &it : ud.scene.coins) it.draw();

	ImGui::End();
}

void basic_window_quit(lak::window &window)
{
	LAK_UNUSED(window);
	ud = {};
}
