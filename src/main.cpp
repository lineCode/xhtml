/*
	Copyright (C) 2003-2013 by Kristina Simpson <sweet.kristas@gmail.com>
	
	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	   1. The origin of this software must not be misrepresented; you must not
	   claim that you wrote the original software. If you use this software
	   in a product, an acknowledgment in the product documentation would be
	   appreciated but is not required.

	   2. Altered source versions must be plainly marked as such, and must not be
	   misrepresented as being the original software.

	   3. This notice may not be removed or altered from any source
	   distribution.
*/

#include <clocale>
#include <locale>

#include "asserts.hpp"
#include "filesystem.hpp"
#include "Blittable.hpp"
#include "CameraObject.hpp"
#include "Canvas.hpp"
#include "ClipScope.hpp"
#include "Font.hpp"
#include "RenderManager.hpp"
#include "RenderTarget.hpp"
#include "SceneGraph.hpp"
#include "SceneNode.hpp"
#include "SceneTree.hpp"
#include "SDLWrapper.hpp"
#include "SurfaceBlur.hpp"
#include "WindowManager.hpp"
#include "profile_timer.hpp"
#include "variant_utils.hpp"
#include "unit_test.hpp"

#include "css_parser.hpp"
#include "FontDriver.hpp"
#include "scrollable.hpp"
#include "xhtml.hpp"
#include "xhtml_layout_engine.hpp"
#include "xhtml_root_box.hpp"
#include "xhtml_style_tree.hpp"
#include "xhtml_node.hpp"
#include "xhtml_render_ctx.hpp"

#include "SurfaceScale.hpp"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace
{
	static int display_tree_parse = false;
	static int layout_cycle_test = false;
}

void check_layout(int width, int height, xhtml::StyleNodePtr& style_tree, xhtml::DocumentPtr doc, KRE::SceneTreePtr& scene_tree, KRE::SceneGraphPtr graph)
{
	xhtml::RenderContextManager rcm;
	// layout has to happen after initialisation of graphics
	if(doc->needsLayout()) {
		LOG_DEBUG("Triggered layout!");

		//display_list->clear();

		// XXX should we should have a re-process styles flag here.

		{
		profile::manager pman("apply styles");
		doc->processStyleRules();
		}

		{
			profile::manager pman("create style tree");
			if(style_tree == nullptr) {
				style_tree = xhtml::StyleNode::createStyleTree(doc);
				scene_tree = style_tree->getSceneTree();
			} else {
				style_tree->updateStyles();
			}
		}

		xhtml::RootBoxPtr layout = nullptr;
		{
		profile::manager pman("layout");
		layout = xhtml::Box::createLayout(style_tree, width, height);
		}
		{
		profile::manager pman_render("render");
		scene_tree->clear();
		layout->render(point());
		
		}

		if(display_tree_parse) {
			layout->preOrderTraversal([](xhtml::BoxPtr box, int nesting) {
				std::stringstream ss;
				ss << std::string(nesting * 2, ' ') << box->toString();
				LOG_DEBUG(ss.str());
			}, 0);
		}
	}
}

xhtml::DocumentPtr load_xhtml(const std::string& ua_ss, const std::string& test_doc)
{
	auto user_agent_style_sheet = std::make_shared<css::StyleSheet>();
	css::Parser::parse(user_agent_style_sheet, sys::read_file(ua_ss));

	auto doc = xhtml::Document::create(user_agent_style_sheet);
	auto doc_frag = xhtml::parse_from_file(test_doc, doc);
	doc->addChild(doc_frag, doc);
	//doc->normalize();
	doc->processStyles();
	// whitespace can only be processed after applying styles.
	doc->processWhitespace();

	/*doc->preOrderTraversal([](xhtml::NodePtr n) {
		LOG_DEBUG(n->toString());
		return true;
	});*/

	// XXX - open question. Should we generate another tree for handling mouse events.

	return doc;
}

KRE::SceneObjectPtr test_filter_shader(const std::string& filename)
{
	using namespace KRE;
	auto wnd = WindowManager::getMainWindow();

	const int gaussian_radius = 7;
	const float sigma = 3.0f;
	const std::vector<float> gaussian = generate_gaussian(sigma, gaussian_radius);

	auto bt = std::make_shared<Blittable>(Texture::createTexture(filename));
	const int img_width  = bt->getTexture()->width();
	const int img_height = bt->getTexture()->height();
	bt->setCamera(Camera::createInstance("ortho7", 0, img_width, 0, img_height));
	auto blur7_shader = ShaderProgram::getProgram("blur7")->clone();
	const int blur7_two = blur7_shader->getUniform("texel_height_offset");
	const int blur7_tho = blur7_shader->getUniform("texel_height_offset");
	const int u_gaussian7 = blur7_shader->getUniform("gaussian");
	blur7_shader->setUniformDrawFunction([blur7_two, blur7_tho, u_gaussian7, gaussian, img_height](ShaderProgramPtr shader) {
		shader->setUniformValue(blur7_two, 0.0f);
		shader->setUniformValue(blur7_tho, 1.0f / (static_cast<float>(img_height)-1.0f));
		shader->setUniformValue(u_gaussian7,  gaussian.data());
	});
	bt->setShader(blur7_shader);
	auto rt = RenderTarget::create(img_width, img_height);
	{
		RenderTarget::RenderScope rs(rt, rect(0, 0, img_width, img_height));
		bt->preRender(wnd);
		wnd->render(bt.get());
	}	

	rt->setCentre(Blittable::Centre::MIDDLE);
	rt->setDrawRect(rect(0, 0, img_width * 2, img_height * 2));
	rt->setPosition(wnd->width()/2, wnd->height()/2);
	auto filter_shader = ShaderProgram::getProgram("filter_shader")->clone();
	const int u_blur = filter_shader->getUniform("u_blur");
	const int u_sepia = filter_shader->getUniform("u_sepia");
	const int u_brightness = filter_shader->getUniform("u_brightness");
	const int u_contrast = filter_shader->getUniform("u_contrast");
	const int u_grayscale = filter_shader->getUniform("u_grayscale");
	const int u_hue_rotate = filter_shader->getUniform("u_hue_rotate");
	const int u_invert = filter_shader->getUniform("u_invert");
	const int u_opacity = filter_shader->getUniform("u_opacity");
	const int u_saturate = filter_shader->getUniform("u_saturate");
	const int blur_two = filter_shader->getUniform("texel_width_offset");
	const int blur_tho = filter_shader->getUniform("texel_height_offset");
	const int u_gaussian = filter_shader->getUniform("gaussian");
	filter_shader->setUniformDrawFunction([u_blur, u_sepia, u_brightness, u_contrast, 
		u_grayscale, u_hue_rotate, u_invert, u_opacity, 
		u_saturate, blur_two, blur_tho, 
		u_gaussian, gaussian, img_width](ShaderProgramPtr shader) {		
		shader->setUniformValue(u_blur, 1);
		shader->setUniformValue(blur_two, 1.0f / (static_cast<float>(img_width) - 1.0f));
		shader->setUniformValue(blur_tho, 0.0f);
		shader->setUniformValue(u_gaussian,  gaussian.data());

		/*shader->setUniformValue(u_sepia, 0.0f);
		shader->setUniformValue(u_brightness, 1.0f);
		shader->setUniformValue(u_contrast, 1.0f);
		shader->setUniformValue(u_grayscale, 0.0f);
		shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);		// angle in radians
		shader->setUniformValue(u_invert, 0.0f);
		shader->setUniformValue(u_opacity, 1.0f);
		shader->setUniformValue(u_saturate, 1.0f);*/
		shader->setUniformValue(u_sepia, 1.0f);
		shader->setUniformValue(u_brightness, 0.5f);
		shader->setUniformValue(u_contrast, 2.0f);
		shader->setUniformValue(u_grayscale, 1.0f);
		shader->setUniformValue(u_hue_rotate, 90.0f / 180.0f * 3.141592653f);		// angle in radians
		shader->setUniformValue(u_invert, 1.0f);
		shader->setUniformValue(u_opacity, 0.5f);
		shader->setUniformValue(u_saturate, 2.0f);
	});
	rt->setShader(filter_shader);

	return rt;
}

#ifdef _MSC_VER
std::string wide_string_to_utf8(const std::wstring& ws)
{
	std::string res;

	int ret = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), ws.size(), nullptr, 0, nullptr, nullptr);
	if(ret > 0) {
		std::vector<char> str;
		str.resize(ret);
		ret = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), ws.size(), str.data(), str.size(), nullptr, nullptr);
		res = std::string(str.begin(), str.end());
	}

	return res;
}
#endif

void read_system_fonts(sys::file_path_map* res)
{
#if defined(_MSC_VER)
	// HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders\Fonts

	// should enum the key for the data type here, then run a query with a null buffer for the size

	HKEY font_key;
	LONG err = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", 0, KEY_READ, &font_key);
	if(err == ERROR_SUCCESS) {
		std::vector<wchar_t> data;
		DWORD data_size = 0;

		err = RegQueryValueExW(font_key, L"Fonts", 0, 0, nullptr, &data_size);
		if(err != ERROR_SUCCESS) {
			return;
		}
		data.resize(data_size);

		err = RegQueryValueExW(font_key, L"Fonts", 0, 0, reinterpret_cast<LPBYTE>(data.data()), &data_size);
		if(err == ERROR_SUCCESS) {
			if(data[data_size-2] == 0 && data[data_size-1] == 0) {
				// was stored with null terminator
				data_size -= 2;
			}
			data_size >>= 1;
			std::wstring base_font_dir(data.begin(), data.begin()+data_size);
			std::wstring wstr = base_font_dir + L"\\*.?tf";
			WIN32_FIND_DATA find_data;
			HANDLE hFind = FindFirstFileW(wstr.data(), &find_data);
			(*res)[wide_string_to_utf8(std::wstring(find_data.cFileName))] = wide_string_to_utf8(base_font_dir + L"\\" + std::wstring(find_data.cFileName));
			if(hFind != INVALID_HANDLE_VALUE) {			
				while(FindNextFileW(hFind, &find_data)) {
					(*res)[wide_string_to_utf8(std::wstring(find_data.cFileName))] = wide_string_to_utf8(base_font_dir + L"\\" + std::wstring(find_data.cFileName));
				}
			}
			FindClose(hFind);
		} else {
			LOG_WARN("Unable to read \"Fonts\" sub-key");
		}
		RegCloseKey(font_key);
	} else {
		LOG_WARN("Unable to read the shell folders registry key");
		// could try %windir%\fonts as a backup
	}
#elif defined(linux) || defined(__linux__)
	// use maybe XListFonts or fontconfig
#else
#endif
}

int main(int argc, char* argv[])
{
	std::vector<std::string> args;
	for(int i = 1; i < argc; ++i) {
		if(argv[i] == std::string("--display-tree")) {
			display_tree_parse = true;
		} else if(argv[i] == std::string("--layout-cycle")) {
			layout_cycle_test = true;
		} else {
			args.emplace_back(argv[i]);
		}
	}
	if(args.empty()) {
		std::cout << "Usage: xhtml <filename>\n";
		return 0;
	}

	int width = 1024;
	int height = 768;

	using namespace KRE;
	SDL::SDL_ptr manager(new SDL::SDL());
	SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
	//SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG);

	if(!test::run_tests()) {
		// Just exit if some tests failed.
		exit(1);
	}

#if defined(__linux__)
	const std::string data_path = "data/";
#else
	const std::string data_path = "../data/";
#endif
	const std::string test_doc = data_path + args[0];
	//const std::string test_doc = data_path + "storyboard.xhtml";
	const std::string ua_ss = data_path + "user_agent.css";

	sys::file_path_map font_files;
	sys::get_unique_files(data_path + "fonts/", font_files);
	read_system_fonts(&font_files);
	KRE::FontDriver::setAvailableFonts(font_files);
	KRE::FontDriver::setFontProvider("stb");

#if 0
	auto test_surf = KRE::Surface::create("../data/httt_story1.jpg");
	auto outputs0 = KRE::scale::nearest_neighbour(test_surf, 60);
	outputs0->savePng("test-nearest.png");

	auto outputs1 = KRE::scale::bilinear(test_surf, 60);
	outputs1->savePng("test-linear.png");

	auto outputs2 = KRE::scale::bicubic(test_surf, 60);
	outputs2->savePng("test-bilinear.png");
	return 0;
#endif

#if 1
	WindowManager wm("SDL");

	variant_builder hints;
	hints.add("renderer", "opengl");
	hints.add("dpi_aware", true);
	hints.add("use_vsync", true);
	hints.add("resizeable", true);

	LOG_DEBUG("Creating window of size: " << width << "x" << height);
	auto main_wnd = wm.createWindow(width, height, hints.build());
	main_wnd->enableVsync(true);
	const float aspect_ratio = static_cast<float>(width) / height;

#if defined(__linux__)
	LOG_DEBUG("setting image file filter to 'images/'");
	Surface::setFileFilter(FileFilterType::LOAD, [](const std::string& fname) { return "images/" + fname; });
	Surface::setFileFilter(FileFilterType::SAVE, [](const std::string& fname) { return "images/" + fname; });
#else
	LOG_DEBUG("setting image file filter to '../images/'");
	Surface::setFileFilter(FileFilterType::LOAD, [](const std::string& fname) { return "../images/" + fname; });
	Surface::setFileFilter(FileFilterType::SAVE, [](const std::string& fname) { return "../images/" + fname; });
#endif
	Font::setAvailableFonts(font_files);

	SceneGraphPtr scene = SceneGraph::create("main");
	SceneNodePtr root = scene->getRootNode();
	root->setNodeName("root_node");

	DisplayDevice::getCurrent()->setDefaultCamera(std::make_shared<Camera>("ortho1", 0, width, 0, height));

	auto rman = std::make_shared<RenderManager>();
	auto rq = rman->addQueue(0, "opaques");

	xhtml::DocumentPtr doc = load_xhtml(ua_ss, test_doc);
	xhtml::StyleNodePtr style_tree = nullptr;
	KRE::SceneTreePtr scene_tree = nullptr;
	check_layout(width, height, style_tree, doc, scene_tree, scene);

	while(layout_cycle_test) {
		doc->triggerLayout();
		/* For cycle testing of ui_test2.xhtml
		if(layout_cycle_test) {
			bool toggle = false;
			auto div_content1 = doc->getElementById("content1");
			if(div_content1 != nullptr) {
				if(toggle) {
					div_content1->addAttribute(xhtml::Attribute::create("style", "display: none", doc));
				} else  {
					div_content1->addAttribute(xhtml::Attribute::create("style", "display: block", doc));
				}
			}
		}*/
		check_layout(width, height, style_tree, doc, scene_tree, scene);
	}

	auto canvas = Canvas::getInstance();

	//auto txt = Font::getInstance()->renderText("Thequickbrownfoxjumpsoverthelazydog.", Color::colorWhite(), static_cast<int>(24.0*96.0/72.0), true, "FreeSerif.ttf");

	SurfacePtr surf = nullptr;
	surf = Surface::create("summer.png");
	//std::vector<unsigned char> pixels;
	//pixels.resize(512 * 512);
	{
		//profile::manager pman("fill rect");
		//for(int y = 32; y < (512-32); ++y) {
		//	std::fill(&pixels[y * 512 + 32], &pixels[y * 512 + 512-32], 255);
		//}
	}
	//pixels_alpha_blur(pixels.data(), 512, 512, 512, 64.0f);
	//surf = Surface::create(512, 512, 8, 512, 0, 0, 0, 0xff, pixels.data());
	//surface_alpha_blur(surf, 6.0f);
	//auto bt = std::make_shared<Blittable>(Texture::createTexture(surf));
	//bt->setShader(ShaderProgram::getProgram("font_shader"));
	//bt->setColor(Color::colorBlue());
	//bt->setCentre(Blittable::Centre::MIDDLE);
	//bt->setPosition(width/2, 5 * height / 6);
	//bt = test_filter_shader("test_npc.png");

	SceneObjectPtr bt = nullptr;

	auto scrollbar = std::make_shared<scrollable::Scrollbar>(scrollable::Scrollbar::Direction::VERTICAL, [](int y_offs){
		std::cerr << "scrollbar change to: " << y_offs << "\n";
	}, rect(width-10, 0, 10, height));

	SDL_Event e;
	bool done = false;
	Uint32 last_tick_time = SDL_GetTicks();
	while(!done) {
		while(SDL_PollEvent(&e)) {
			// XXX we need to add some keyboard/mouse callback handling here for "doc".
			if(e.type == SDL_KEYUP && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
				done = true;
			} else if(e.type == SDL_KEYDOWN) {
				LOG_DEBUG("KEY PRESSED: " << SDL_GetKeyName(e.key.keysym.sym) << " : " << e.key.keysym.sym << " : " << e.key.keysym.scancode);
			} else if(e.type == SDL_QUIT) {
				done = true;
			} else if(e.type == SDL_MOUSEMOTION) {
				doc->handleMouseMotion(false, e.motion.x, e.motion.y);
			} else if(e.type == SDL_MOUSEBUTTONDOWN) {
				doc->handleMouseButtonDown(false, e.button.x, e.button.y, e.button.button);
			} else if(e.type == SDL_MOUSEBUTTONUP) {
				doc->handleMouseButtonUp(false, e.button.x, e.button.y, e.button.button);
			} else if(e.type == SDL_WINDOWEVENT) {
				const SDL_WindowEvent& wnd = e.window;
				if(wnd.event == SDL_WINDOWEVENT_RESIZED) {
					doc->triggerLayout();
					width = wnd.data1;
					height = wnd.data2;
					main_wnd->notifyNewWindowSize(width, height);
					DisplayDevice::getCurrent()->setDefaultCamera(std::make_shared<Camera>("ortho1", 0, width, 0, height));
				}
			}
		}

		//main_wnd->setClearColor(KRE::Color::colorWhite());
		main_wnd->clear(ClearFlags::ALL);

		check_layout(width, height, style_tree, doc, scene_tree, scene);

		// Called once a cycle before rendering.
		Uint32 current_tick_time = SDL_GetTicks();
		float dt = (current_tick_time - last_tick_time) / 1000.0f;
		if(style_tree != nullptr) {
			style_tree->process(dt);
		}
		scene->process(dt);
		last_tick_time = current_tick_time;

		//scene->renderScene(rman);
		//rman->render(main_wnd);

		if(scene_tree != nullptr) {
			//ClipScope::Manager clipper(rect(width/4, height/4, width/2, height/2));
			ClipScope::Manager clipper(rect(0, 0, width, height));
			scene_tree->preRender(main_wnd);
			scene_tree->render(main_wnd);
		}

		if(bt) {
			bt->preRender(main_wnd);
			main_wnd->render(bt.get());
		}

		if(scrollbar) {
			scrollbar->preRender(main_wnd);
			main_wnd->render(scrollbar.get());
		}

		main_wnd->swap();
	}
#endif

	return 0;
}
