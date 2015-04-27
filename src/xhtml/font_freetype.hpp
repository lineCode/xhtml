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

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "geometry.hpp"
#include "Color.hpp"
#include "Texture.hpp"

namespace KRE
{
	typedef std::map<std::string, std::string> font_path_cache;	

	struct FontError2 : public std::runtime_error
	{
		FontError2(const std::string& str) : std::runtime_error(str) {}
	};

	class FontHandle
	{
	public:
		FontHandle(const std::string& fnt_name, float size, const Color& color);
		~FontHandle();
		float getFontSize();
		float getFontXHeight();
		const std::string& getFontName();
		const std::string& getFontFamily();
		void renderText();
		void getFontMetrics();
		rectf getBoundingBox(const std::string& text);
		void getGlyphPath(const std::string& text, std::vector<geometry::Point<double>>* path);
		double calculateCharAdvance(char32_t cp);
	private:
		class Impl;
		std::unique_ptr<Impl> impl_;
	};
	typedef std::shared_ptr<FontHandle> FontHandlePtr;

	class FontDriver
	{
	public:
		static FontHandlePtr getFontHandle(const std::vector<std::string>& font_list, float size, const Color& color=Color::colorWhite());
		static void setAvailableFonts(const font_path_cache& font_map);
		//static TexturePtr renderText(const std::string& text, ...);
	private:
		FontDriver();
	};
}
