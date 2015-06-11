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

#include "xhtml_block_box.hpp"

namespace xhtml
{
	class RootBox : public BlockBox
	{
	public:
		explicit RootBox(BoxPtr parent, NodePtr node);
		std::string toString() const override;

		void addFloatBox(LayoutEngine& eng, BoxPtr box, css::CssFloat cfloat, FixedPoint y);
		void addFixed(BoxPtr fixed);
		
		void layoutFixed(LayoutEngine& eng, const Dimensions& containing);

		const std::vector<BoxPtr>& getFixed() const { return fixed_boxes_; }
		const std::vector<BoxPtr>& getLeftFloats() const { return left_floats_; }
		const std::vector<BoxPtr>& getRightFloats() const { return right_floats_; }
	private:
		virtual void handleLayout(LayoutEngine& eng, const Dimensions& containing) override;
		void handleEndRender(DisplayListPtr display_list, const point& offset) const override;

		std::vector<BoxPtr> fixed_boxes_;
		std::vector<BoxPtr> left_floats_;
		std::vector<BoxPtr> right_floats_;
	};
}