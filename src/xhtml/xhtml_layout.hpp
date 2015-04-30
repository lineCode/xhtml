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

#include "geometry.hpp"

#include "css_styles.hpp"
#include "display_list.hpp"
#include "xhtml.hpp"
#include "variant_object.hpp"

namespace xhtml
{
	class LayoutBox;
	typedef std::shared_ptr<LayoutBox> LayoutBoxPtr;

	struct EdgeSize
	{
		EdgeSize() : left(0), top(0), right(0), bottom(0) {}
		EdgeSize(double l, double t, double r, double b) : left(l), top(t), right(r), bottom(b) {}
		double left;
		double top;
		double right;
		double bottom;
	};

	struct Dimensions
	{
		geometry::Rect<double> content_;
		EdgeSize padding_;
		EdgeSize border_;
		EdgeSize margin_;
	};

	class LayoutBox : public std::enable_shared_from_this<LayoutBox>
	{
	public:
		LayoutBox(LayoutBoxPtr parent, NodePtr node, css::CssDisplay display, DisplayListPtr display_list);
		static LayoutBoxPtr create(NodePtr node, DisplayListPtr display_list, LayoutBoxPtr parent=nullptr);
		void layout(const Dimensions& containing, point& offset);
		
		void layoutBlock(const Dimensions& containing);
		void layoutBlockWidth(const Dimensions& containing);
		void layoutBlockPosition(const Dimensions& containing);
		void layoutBlockChildren();
		void layoutBlockHeight(const Dimensions& containing);

		void layoutInline(const Dimensions& containing, point& offset);
		void layoutInlineWidth(const Dimensions& containing, point& offset);

		void preOrderTraversal(std::function<void(LayoutBoxPtr, int)> fn, int nesting);
		std::string toString() const;
		const geometry::Rect<double>& getContentDimensions() const { return dimensions_.content_; }
	private:
		WeakNodePtr node_;
		css::CssDisplay display_;
		Dimensions dimensions_;
		DisplayListPtr display_list_;
		std::vector<LayoutBoxPtr> children_;
	};

	double convert_pt_to_pixels(double pt);
}
