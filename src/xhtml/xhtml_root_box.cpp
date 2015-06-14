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

#include "xhtml_root_box.hpp"
#include "xhtml_layout_engine.hpp"

namespace xhtml
{
	using namespace css;

	RootBox::RootBox(BoxPtr parent, NodePtr node)
		: BlockBox(parent, node),
		  fixed_boxes_(),
		  left_floats_(),
		  right_floats_()
	{
	}

	std::string RootBox::toString() const 
	{
		std::ostringstream ss;
		ss << "RootBox: " << getDimensions().content_ << "\n";

		int nesting = 10;
		for(auto& lf : getLeftFloats()) {
			ss << std::string((nesting+1) * 2, ' ') << "Floating " << lf->toString() << "\n";
		}
		for(auto& rf : getRightFloats()) {
			ss << std::string((nesting+1) * 2, ' ') << "Floating " << rf->toString() << "\n";
		}

		for(auto& f : fixed_boxes_) {
			ss << std::string((nesting+1) * 2, ' ') << " FixedBox: " << f->toString() << "\n";
		}
		return ss.str();
	}

	void RootBox::handleLayout(LayoutEngine& eng, const Dimensions& containing) 
	{
		//BlockBox::handleLayout(eng, containing);

		calculateHorzMPB(containing.content_.width);
		calculateVertMPB(containing.content_.height);

		setContentX(getMBPLeft());
		setContentY(getMBPTop());

		setContentWidth(containing.content_.width - getMBPWidth());
		setContentHeight(containing.content_.height - getMBPHeight());

		layoutFixed(eng, containing);
	}

	void RootBox::handleEndRender(DisplayListPtr display_list, const point& offset) const
	{
		// render float boxes.
		/*for(auto& lf : left_floats_) {
			lf->render(display_list, offset);
		}
		for(auto& rf : right_floats_) {
			rf->render(display_list, offset);
		}*/
		// render fixed boxes.
		for(auto& fix : fixed_boxes_) {
			fix->render(display_list, point(0, 0));
		}
	}

	void RootBox::addFixed(BoxPtr fixed)
	{
		fixed_boxes_.emplace_back(fixed);
	}

	void RootBox::layoutFixed(LayoutEngine& eng, const Dimensions& containing)
	{
		for(auto& fix : fixed_boxes_) {
			fix->layout(eng, eng.getDimensions());
		}
	}

	void RootBox::addFloatBox(LayoutEngine& eng, BoxPtr box, css::CssFloat cfloat)
	{
		/*box->layout(eng, box->getParent()->getDimensions());

		const FixedPoint lh = getLineHeight();
		const FixedPoint box_w = box->getDimensions().content_.width;

		FixedPoint new_x = cfloat == CssFloat::LEFT ? eng.getXAtPosition(y + eng.getOffset().y) + x : eng.getX2AtPosition(y + eng.getOffset().y);
		FixedPoint w = eng.getWidthAtPosition(y + eng.getOffset().y, getDimensions().content_.width);
		bool placed = false;
		while(!placed) {
			if(w >= box_w) {
				box->setContentX(new_x - (cfloat == CssFloat::LEFT ? x : box_w));
				box->setContentY(y);
				placed = true;
			} else {
				y += lh;
				new_x = cfloat == CssFloat::LEFT ? eng.getXAtPosition(y + eng.getOffset().y) + x : eng.getX2AtPosition(y + eng.getOffset().y);
				w = eng.getWidthAtPosition(y + eng.getOffset().y, getDimensions().content_.width);
			}
		}*/

		if(cfloat == CssFloat::LEFT) {
			left_floats_.emplace_back(box);
		} else {
			right_floats_.emplace_back(box);
		}
	}

}
