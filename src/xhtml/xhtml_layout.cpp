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

#include "asserts.hpp"
#include "css_styles.hpp"
#include "xhtml_layout.hpp"
#include "xhtml_node.hpp"
#include "xhtml_render_ctx.hpp"

namespace xhtml
{
	using namespace css;

	namespace 
	{
		std::string display_string(CssDisplay disp) {
			switch(disp) {
				case CssDisplay::BLOCK:					return "block";
				case CssDisplay::INLINE:				return "inline";
				case CssDisplay::INLINE_BLOCK:			return "inline-block";
				case CssDisplay::LIST_ITEM:				return "list-item";
				case CssDisplay::TABLE:					return "table";
				case CssDisplay::INLINE_TABLE:			return "inline-table";
				case CssDisplay::TABLE_ROW_GROUP:		return "table-row-group";
				case CssDisplay::TABLE_HEADER_GROUP:	return "table-header-group";
				case CssDisplay::TABLE_FOOTER_GROUP:	return "table-footer-group";
				case CssDisplay::TABLE_ROW:				return "table-row";
				case CssDisplay::TABLE_COLUMN_GROUP:	return "table-column-group";
				case CssDisplay::TABLE_COLUMN:			return "table-column";
				case CssDisplay::TABLE_CELL:			return "table-cell";
				case CssDisplay::TABLE_CAPTION:			return "table-caption";
				case CssDisplay::NONE:					return "none";
				default: 
					ASSERT_LOG(false, "illegal display value: " << static_cast<int>(disp));
					break;
			}
			return "none";
		}
	}

	double convert_pt_to_pixels(double pt)
	{
		return pt / 72.0 * RenderContext::get().getDPI();
	}

	LayoutBox::LayoutBox(LayoutBoxPtr parent, NodePtr node, CssDisplay display, DisplayListPtr display_list)
		: node_(node),
		  display_(display),
		  dimensions_(),
		  display_list_(display_list),
		  children_()
	{
		ASSERT_LOG(display_ != CssDisplay::NONE, "Tried to create a layout node with a display of none.");
	}

	LayoutBoxPtr LayoutBox::create(NodePtr node, DisplayListPtr display_list, LayoutBoxPtr parent)
	{
		RenderContext::Manager ctx_manager(node->getProperties());
		CssDisplay display = RenderContext::get().getComputedValue(Property::DISPLAY).getValue<CssDisplay>();
		if(display != CssDisplay::NONE) {
			auto root = std::make_shared<LayoutBox>(parent, node, display, display_list);

			LayoutBoxPtr inline_container;
			for(auto& c : node->getChildren()) {
				RenderContext::Manager child_ctx_manager(node->getProperties());
				CssDisplay disp = RenderContext::get().getComputedValue(Property::DISPLAY).getValue<CssDisplay>();
				if(disp == CssDisplay::NONE) {
					// ignore child nodes with display none.
				} else if(disp == CssDisplay::INLINE && root->display_ == CssDisplay::BLOCK) {
					if(inline_container == nullptr) {
						inline_container = std::make_shared<LayoutBox>(root, nullptr, CssDisplay::BLOCK, display_list);
						root->children_.emplace_back(inline_container);
					}
					inline_container->children_.emplace_back(create(c, display_list, root));
				} else {
					inline_container.reset();
					root->children_.emplace_back(create(c, display_list, root));
				}
			}

			return root;
		}
		return nullptr;
	}

	void LayoutBox::layout(const Dimensions& containing, point& offset)
	{
		// XXX need to deal with non-static positioned elements
		auto node = node_.lock();
		if(node == nullptr) {
			// anonymous
			layoutInline(containing, offset);
			return;
		}
		std::unique_ptr<RenderContext::Manager> ctx_manager;
		if(node->id() == NodeId::ELEMENT) {
			// only instantiate on element nodes.
			ctx_manager.reset(new RenderContext::Manager(node->getProperties()));
		}

		switch(display_) {
			case CssDisplay::BLOCK:
				layoutBlock(containing);
				break;
			case CssDisplay::INLINE:
				layoutInline(containing, offset);
				break;
			case CssDisplay::INLINE_BLOCK:
			case CssDisplay::LIST_ITEM:
			case CssDisplay::TABLE:
			case CssDisplay::INLINE_TABLE:
			case CssDisplay::TABLE_ROW_GROUP:
			case CssDisplay::TABLE_HEADER_GROUP:
			case CssDisplay::TABLE_FOOTER_GROUP:
			case CssDisplay::TABLE_ROW:
			case CssDisplay::TABLE_COLUMN_GROUP:
			case CssDisplay::TABLE_COLUMN:
			case CssDisplay::TABLE_CELL:
			case CssDisplay::TABLE_CAPTION:
				ASSERT_LOG(false, "Implement display layout type: " << static_cast<int>(display_));
				break;
			case CssDisplay::NONE:
			default: 
				break;
		}
	}

	void LayoutBox::layoutBlock(const Dimensions& containing)
	{
		layoutBlockWidth(containing);
		layoutBlockPosition(containing);
		layoutBlockChildren();
		layoutBlockHeight(containing);
	}

	void LayoutBox::layoutBlockWidth(const Dimensions& containing)
	{
		auto node = node_.lock();
		// boxes without nodes are anonymous and thus dimensionless.
		if(node != nullptr) {
			RenderContext& ctx = RenderContext::get();
			double containing_width = containing.content_.w();

			// XXX If the value is a percentable it needs to be relative to the containing_width.
			auto css_width = ctx.getComputedValue(Property::WIDTH).getValue<Width>();
			double width = css_width.evaluate(ctx).getValue<Length>().compute(containing_width);

			dimensions_.border_.left = ctx.getComputedValue(Property::BORDER_LEFT_WIDTH).getValue<Length>().compute();
			dimensions_.border_.right = ctx.getComputedValue(Property::BORDER_RIGHT_WIDTH).getValue<Length>().compute();

			dimensions_.padding_.left = ctx.getComputedValue(Property::PADDING_LEFT).getValue<Length>().compute(containing_width);
			dimensions_.padding_.right = ctx.getComputedValue(Property::PADDING_RIGHT).getValue<Length>().compute(containing_width);

			auto css_margin_left = ctx.getComputedValue(Property::MARGIN_LEFT).getValue<Width>();
			auto css_margin_right = ctx.getComputedValue(Property::MARGIN_RIGHT).getValue<Width>();
			dimensions_.margin_.left = css_margin_left.evaluate(ctx).getValue<Length>().compute(containing_width);
			dimensions_.margin_.right = css_margin_right.evaluate(ctx).getValue<Length>().compute(containing_width);

			double total = dimensions_.border_.left + dimensions_.border_.right
				+ dimensions_.padding_.left + dimensions_.padding_.right
				+ dimensions_.margin_.left + dimensions_.margin_.right
				+ width;
			
			if(!css_width.isAuto() && total > containing.content_.w()) {
				if(css_margin_left.isAuto()) {
					dimensions_.margin_.left = 0;
				}
				if(css_margin_right.isAuto()) {
					dimensions_.margin_.right = 0;
				}
			}

			// If negative is overflow.
			double underflow = containing.content_.w() - total;

			if(css_width.isAuto()) {
				if(css_margin_left.isAuto()) {
					dimensions_.margin_.left = 0;
				}
				if(css_margin_right.isAuto()) {
					dimensions_.margin_.right = 0;
				}
				if(underflow >= 0) {
					width = underflow;
				} else {
					width = 0;
					dimensions_.margin_.right = css_margin_right.evaluate(ctx).getValue<Length>().compute(containing_width) + underflow;
				}
				dimensions_.content_.set_w(width);
			} else if(!css_margin_left.isAuto() && !css_margin_right.isAuto()) {
				dimensions_.margin_.right = dimensions_.margin_.right + underflow;
			} else if(!css_margin_left.isAuto() && css_margin_right.isAuto()) {
				dimensions_.margin_.right = underflow;
			} else if(css_margin_left.isAuto() && !css_margin_right.isAuto()) {
				dimensions_.margin_.left = underflow;
			} else if(css_margin_left.isAuto() && css_margin_right.isAuto()) {
				dimensions_.margin_.left = underflow/2.0;
				dimensions_.margin_.right = underflow/2.0;
			} 
		} else {
			dimensions_.content_.set_w(containing.content_.w());
		}
	}

	void LayoutBox::layoutBlockPosition(const Dimensions& containing)
	{
		auto node = node_.lock();
		if(node != nullptr) {
			RenderContext& ctx = RenderContext::get();
			double containing_height = containing.content_.h();
			
			dimensions_.border_.top = ctx.getComputedValue(Property::BORDER_TOP_WIDTH).getValue<Length>().compute();
			dimensions_.border_.bottom = ctx.getComputedValue(Property::BORDER_BOTTOM_WIDTH).getValue<Length>().compute();

			dimensions_.padding_.top = ctx.getComputedValue(Property::PADDING_TOP).getValue<Length>().compute(containing_height);
			dimensions_.padding_.bottom = ctx.getComputedValue(Property::PADDING_BOTTOM).getValue<Length>().compute(containing_height);

			dimensions_.margin_.top = ctx.getComputedValue(Property::MARGIN_TOP).getValue<Width>().evaluate(ctx).getValue<Length>().compute(containing_height);
			dimensions_.margin_.bottom = ctx.getComputedValue(Property::MARGIN_BOTTOM).getValue<Width>().evaluate(ctx).getValue<Length>().compute(containing_height);

			dimensions_.content_.set_x(containing.content_.x() + dimensions_.margin_.left + dimensions_.padding_.left + dimensions_.border_.left);
			dimensions_.content_.set_y(containing.content_.y2() + dimensions_.margin_.top + dimensions_.padding_.top + dimensions_.border_.top);
		} else {
			dimensions_.content_.set_xy(containing.content_.x(), containing.content_.y2());
		}
	}

	void LayoutBox::layoutBlockChildren()
	{		
		for(auto& child : children_) {
			child->layout(dimensions_, point());
			dimensions_.content_.set_h(dimensions_.content_.h()
				+ child->dimensions_.content_.h() 
				+ child->dimensions_.margin_.top + child->dimensions_.margin_.bottom
				+ child->dimensions_.padding_.top + child->dimensions_.padding_.bottom
				+ child->dimensions_.border_.top + child->dimensions_.border_.bottom);
		}
	}

	void LayoutBox::layoutBlockHeight(const Dimensions& containing)
	{
		auto node = node_.lock();
		if(node != nullptr) {
			RenderContext& ctx = RenderContext::get();
			// a set height value overrides the calculated value.
			auto css_h = ctx.getComputedValue(Property::HEIGHT).getValue<Width>();
			if(!css_h.isAuto()) {
				dimensions_.content_.set_h(css_h.evaluate(ctx).getValue<Length>().compute(containing.content_.h()));
			}
		}		
	}

	void LayoutBox::layoutInline(const Dimensions& containing, point& offset)
	{
		layoutInlineWidth(containing, offset);
		for(auto& c : children_) {
			c->layout(containing, offset);
		}
	}

	void LayoutBox::layoutInlineWidth(const Dimensions& containing, point& offset)
	{
		KRE::FontRenderablePtr fontr = nullptr;
		auto node = node_.lock();
		if(node != nullptr && node->id() == NodeId::TEXT) {
			long font_coord_factor = RenderContext::get().getFontHandle()->getScaleFactor();
			auto lines = node->generateLines(offset.x/font_coord_factor, containing.content_.w());

			// Generate a renderable object from the lines.
			// XXX move this to its own function.
			std::vector<geometry::Point<long>> path;
			std::string text;

			// XXX need to apply margins/padding/border.

			// XXX we need to store in RenderContext a variable specifying the max-line-height of the last line
			// to account for the case that we have mixed fonts on one line.
			// XXX also need to handle the case that we have an element with a different line height on the same line
			// oh, I suppose that is the same case I just mentioned. mea-culpa.
			// XXX I think this still needs to be passed up to a higher layer and concatenate more text flows together
			// which would allow better handling of justified text when you have elements or breaks on a line near the
			// end of a run. 
			// e.g. <span>Some text to render <em>NeedThisRenderedToday!</em> more text.</span>
			// If the screen was like this |----------------------------|
			// then the text might render  |Some text to render         |
			//                             |NeedThisRenderedToday! more |
			//                             |text.                       |
			//                             |----------------------------|
			// Where it breaks the em element on to another line, but doesn't justify the first line correctly to account 
			// for it. Yes it's a contrived example, but I saw it happen in practice. The other example is if it did
			// |----------------------------|
			// | This is a msg. me this  one| 
			// |----------------------------|
			// i.e. <span>This is a msg.<em>me</em> This one</span>
			// What happens is that the justify algoritm only applies to the text after the em tag finishes,
			// which can look kind of silly if there are only a few words being justified, when the entire 
			// rest of the line could be used.


			// since the Renderable returned from createRenderableFromPath is relative to the font baseline
			// we offset by the line-height to start.
			auto& ctx = RenderContext::get();
			auto lh = ctx.getComputedValue(Property::LINE_HEIGHT).getValue<Length>();
			double line_height = lh.compute();
			if(lh.isPercent() || lh.isNumber()) {
				line_height *= ctx.getComputedValue(Property::FONT_SIZE).getValue<double>();
			}
			LOG_DEBUG("line-height: " << line_height);
			if(offset.y == 0) {
				offset.y = static_cast<long>(line_height * font_coord_factor);
			}
			auto last_line = lines.lines.end()-1;
			for(auto line_it = lines.lines.begin(); line_it != lines.lines.end(); ++line_it) {
				auto& line = *line_it;
				for(auto& word : line) {
					for(auto it = word.advance.begin(); it != word.advance.end()-1; ++it) {
						path.emplace_back(it->x + offset.x, it->y + offset.y);
					}
					offset.x += word.advance.back().x + lines.space_advance;
					text += word.word;
				}
				// We exclude the last line from generating a newline.
				if(line_it != last_line) {
					offset.y += static_cast<long>(line_height * font_coord_factor);
					offset.x = 0;
				}
			}

			auto fh = ctx.getFontHandle();
			fontr = fh->createRenderableFromPath(fontr, text, path);
			fontr->setColor(ctx.getComputedValue(Property::COLOR).getValue<KRE::Color>());
			display_list_->addRenderable(fontr);
		}
	}

	void LayoutBox::preOrderTraversal(std::function<void(LayoutBoxPtr, int)> fn, int nesting)
	{
		fn(shared_from_this(), nesting);
		for(auto& child : children_) {
			child->preOrderTraversal(fn, nesting+1);
		}
	}

	std::string LayoutBox::toString() const
	{
		std::stringstream ss;
		auto node = node_.lock();
		//ss << "Box(" << (node ? display_string(display_) : "anonymous") << ", content: " << dimensions_.content_ << ")";
		ss << "Box(" << (node ? display_string(display_) : "anonymous") << (node ? ", " + node->toString() : "") << ")";
		return ss.str();
	}
}
