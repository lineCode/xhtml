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

#include "CameraObject.hpp"
#include "RenderTarget.hpp"
#include "Shaders.hpp"
#include "WindowManager.hpp"

#include "solid_renderable.hpp"

#include "xhtml_absolute_box.hpp"
#include "xhtml_block_box.hpp"
#include "xhtml_box.hpp"
#include "xhtml_inline_element_box.hpp"
#include "xhtml_layout_engine.hpp"
#include "xhtml_line_box.hpp"
#include "xhtml_root_box.hpp"

namespace xhtml
{
	using namespace css;

	namespace
	{
		std::string fp_to_str(const FixedPoint& fp)
		{
			std::ostringstream ss;
			ss << (static_cast<float>(fp)/LayoutEngine::getFixedPointScaleFloat());
			return ss.str();
		}
	}

	std::ostream& operator<<(std::ostream& os, const Rect& r)
	{
		os << "(" << fp_to_str(r.x) << ", " << fp_to_str(r.y) << ", " << fp_to_str(r.width) << ", " << fp_to_str(r.height) << ")";
		return os;
	}

	Box::Box(BoxId id, BoxPtr parent, StyleNodePtr node)
		: id_(id),
		  node_(node),
		  parent_(parent),
		  dimensions_(),
		  boxes_(),
		  absolute_boxes_(),
		  background_info_(node),
		  border_info_(node),
		  offset_(),
		  line_height_(0),
		  end_of_line_(false),
		  is_replaceable_(false)
	{
		if(getNode() != nullptr && getNode()->id() == NodeId::ELEMENT) {
			is_replaceable_ = getNode()->isReplaced();
		}

		init();
	}

	void Box::init()
	{
		// skip for line/text
		if(id_ == BoxId::LINE) {
			return;
		}

		auto& lh = node_->getLineHeight();
		if(lh != nullptr) {
			if(lh->isPercent() || lh->isNumber()) {
				line_height_ = static_cast<FixedPoint>(lh->compute() * node_->getFont()->getFontSize() * 96.0/72.0);
			} else {
				line_height_ = lh->compute();
			}
		}
	}

	RootBoxPtr Box::createLayout(StyleNodePtr node, int containing_width, int containing_height)
	{
		LayoutEngine e;
		// search for the body element then render that content.
		node->preOrderTraversal([&e, containing_width, containing_height](StyleNodePtr node){
			auto n = node->getNode();
			if(n->id() == NodeId::ELEMENT && n->hasTag(ElementId::HTML)) {
				e.layoutRoot(node, nullptr, point(containing_width * LayoutEngine::getFixedPointScale(), containing_height * LayoutEngine::getFixedPointScale()));
				return false;
			}
			return true;
		});
		node->getNode()->layoutComplete();
		return e.getRoot();
	}

	bool Box::ancestralTraverse(std::function<bool(const ConstBoxPtr&)> fn) const
	{
		if(fn(shared_from_this())) {
			return true;
		}
		auto parent = getParent();
		if(parent != nullptr) {
			return parent->ancestralTraverse(fn);
		}
		return false;
	}

	void Box::preOrderTraversal(std::function<void(BoxPtr, int)> fn, int nesting)
	{
		fn(shared_from_this(), nesting);
		// floats, absolutes
		for(auto& child : boxes_) {
			child->preOrderTraversal(fn, nesting+1);
		}
		for(auto& abs : absolute_boxes_) {
			abs->preOrderTraversal(fn, nesting+1);
		}
	}

	void Box::addAbsoluteElement(LayoutEngine& eng, const Dimensions& containing, BoxPtr abs_box)
	{
		absolute_boxes_.emplace_back(abs_box);
		abs_box->layout(eng, containing);
	}

	bool Box::hasChildBlockBox() const
	{
		for(auto& child : boxes_) {
			if(child->isBlockBox()) {
				return true;
			}
		}
		return false;
	}

	void Box::layout(LayoutEngine& eng, const Dimensions& containing)
	{
		std::unique_ptr<LayoutEngine::FloatContextManager> fcm;
		if(getParent() && getParent()->isFloat()) {
			fcm.reset(new LayoutEngine::FloatContextManager(eng, FloatList()));
		}
		point cursor;
		// If we have a clear flag set, then move the cursor in the layout engine to clear appropriate floats.
		if(node_ != nullptr) {
			eng.moveCursorToClearFloats(node_->getClear(), cursor);
		}

		NodePtr node = getNode();

		std::unique_ptr<RenderContext::Manager> ctx_manager;
		if(node != nullptr) {
			ctx_manager.reset(new RenderContext::Manager(node->getProperties()));
		}

		handlePreChildLayout(eng, containing);

		LineBoxPtr open = std::make_shared<LineBox>(shared_from_this(), cursor);

		if(node_ != nullptr) {
			const std::vector<StyleNodePtr>& node_children = node_->getChildren();
			if(!node_children.empty()) {
				boxes_ = eng.layoutChildren(node_children, shared_from_this(), open);
			}
			if(open != nullptr && !open->boxes_.empty()) {
				boxes_.emplace_back(open);
			}
		}

		// xxx offs
		offset_ = (getParent() != nullptr ? getParent()->getOffset() : point()) + point(dimensions_.content_.x, dimensions_.content_.y);

		for(auto& child : boxes_) {
			if(child->isFloat()) {
				child->layout(eng, dimensions_);
				eng.addFloat(child);
			}
		}

		handlePreChildLayout2(eng, containing);

		for(auto& child : boxes_) {
			if(!child->isFloat()) {
				child->layout(eng, dimensions_);
				handlePostChildLayout(eng, child);
			}
		}
		
		handleLayout(eng, containing);
		//layoutAbsolute(eng, containing);

		for(auto& child : boxes_) {
			child->postParentLayout(eng, dimensions_);
		}

		// need to call this after doing layout, since we need to now what the computed padding/border values are.
		border_info_.init(dimensions_);
		background_info_.init(dimensions_);
	}

	void Box::calculateVertMPB(FixedPoint containing_height)
	{
		if(border_info_.isValid(Side::TOP)) {
			setBorderTop(getStyleNode()->getBorderWidths()[0]->compute());
		}
		if(border_info_.isValid(Side::BOTTOM)) {
			setBorderBottom(getStyleNode()->getBorderWidths()[2]->compute());
		}

		setPaddingTop(getStyleNode()->getPadding()[0]->compute(containing_height));
		setPaddingBottom(getStyleNode()->getPadding()[2]->compute(containing_height));

		setMarginTop(getStyleNode()->getMargin()[0]->getLength().compute(containing_height));
		setMarginBottom(getStyleNode()->getMargin()[2]->getLength().compute(containing_height));
	}

	void Box::calculateHorzMPB(FixedPoint containing_width)
	{		
		if(border_info_.isValid(Side::LEFT)) {
			setBorderLeft(getStyleNode()->getBorderWidths()[1]->compute());
		}
		if(border_info_.isValid(Side::RIGHT)) {
			setBorderRight(getStyleNode()->getBorderWidths()[3]->compute());
		}

		setPaddingLeft(getStyleNode()->getPadding()[1]->compute(containing_width));
		setPaddingRight(getStyleNode()->getPadding()[3]->compute(containing_width));

		if(!getStyleNode()->getMargin()[1]->isAuto()) {
			setMarginLeft(getStyleNode()->getMargin()[1]->getLength().compute(containing_width));
		}
		if(!getStyleNode()->getMargin()[3]->isAuto()) {
			setMarginRight(getStyleNode()->getMargin()[3]->getLength().compute(containing_width));
		}
	}

	void Box::render(const point& offset) const
	{
		point offs/* = offset*/;
		if(id() != BoxId::TEXT) {
			offs += point(dimensions_.content_.x, dimensions_.content_.y);
		}

		if(node_ != nullptr && node_->getPosition() == Position::RELATIVE_POS) {
			if(getStyleNode()->getLeft()->isAuto()) {
				if(!getStyleNode()->getRight()->isAuto()) {
					offs.x -= getStyleNode()->getRight()->getLength().compute(getParent()->getWidth());
				}
				// the other case here evaluates as no-change.
			} else {
				if(getStyleNode()->getRight()->isAuto()) {
					offs.x += getStyleNode()->getLeft()->getLength().compute(getParent()->getWidth());
				} else {
					// over-constrained.
					if(getStyleNode()->getDirection() == Direction::LTR) {
						// left wins
						offs.x += getStyleNode()->getLeft()->getLength().compute(getParent()->getWidth());
					} else {
						// right wins
						offs.x -= getStyleNode()->getRight()->getLength().compute(getParent()->getWidth());
					}
				}
			}

			if(getStyleNode()->getTop()->isAuto()) {
				if(!getStyleNode()->getBottom()->isAuto()) {
					offs.y -= getStyleNode()->getBottom()->getLength().compute(getParent()->getHeight());
				}
				// the other case here evaluates as no-change.
			} else {
				// Either bottom is auto in which case top wins or over-constrained in which case top wins.
				offs.y += getStyleNode()->getTop()->getLength().compute(getParent()->getHeight());
			}
		}

		KRE::SceneTreePtr scene_tree = nullptr;
		if(node_ != nullptr) {
			scene_tree = node_->getSceneTree();
			scene_tree->setPosition(offs.x / LayoutEngine::getFixedPointScaleFloat(), offs.y / LayoutEngine::getFixedPointScaleFloat());

			// XXX needs a modifer for transform origin.
			auto transform = node_->getTransform();
			if(!transform->getTransforms().empty()) {
				// XX active rect needs to be modifed by these 
				// or alternatively rotate the mouse position based on this transform. which is only one 
				// vertex to transform.
				const float tw = (getWidth() + getMBPWidth()) / LayoutEngine::getFixedPointScaleFloat();
				const float th = (getHeight() + getMBPHeight()) / LayoutEngine::getFixedPointScaleFloat();
				glm::mat4 m1 = glm::translate(glm::mat4(1.0f), glm::vec3(-tw/2.0f, -th/2.0f, 0.0f));
				glm::mat4 m2 = glm::translate(glm::mat4(1.0f), glm::vec3(tw/2.0f, th/2.0f, 0.0f));
				auto node = getNode();
				scene_tree->setOnPreRenderFunction([m1, m2, transform, node](KRE::SceneTree* st) {
					glm::mat4 combined_matrix = m2 * transform->getComputedMatrix() * m1;
					st->setModelMatrix(combined_matrix);
					if(node != nullptr) {
						node->setModelMatrix(glm::inverse(combined_matrix));
					}
				});
				
			}
		}

		if(scene_tree != nullptr) {
			// XXX find a way to ameliorate this
			if(id() == BoxId::BLOCK) {
				scene_tree->clearObjects();
			}
			scene_tree->clearRenderTargets();

			handleRenderBackground(scene_tree, offs);
			handleRenderBorder(scene_tree, offs);
			handleRender(scene_tree, offs);
			handleRenderFilters(scene_tree, offs);
		}
		for(auto& child : getChildren()) {
			if(!child->isFloat()) {
				child->render(offs);
			}
		}
		for(auto& child : getChildren()) {
			if(child->isFloat()) {
				child->render(offs);
			}
		}
		for(auto& ab : absolute_boxes_) {
			ab->render(point(0, 0));
		}
		if(scene_tree != nullptr) {
			handleEndRender(scene_tree, offs);
		}

		// set the active rect on any parent node.
		auto node = getNode();
		if(node != nullptr) {
			auto& dims = getDimensions();
			offs += offset;
			const int x = (offs.x - dims.padding_.left - dims.border_.left) / LayoutEngine::getFixedPointScale();
			const int y = (offs.y - dims.padding_.top - dims.border_.top) / LayoutEngine::getFixedPointScale();
			const int w = (dims.content_.width + dims.padding_.left + dims.padding_.right + dims.border_.left + dims.border_.right) / LayoutEngine::getFixedPointScale();
			const int h = (dims.content_.height + dims.padding_.top + dims.padding_.bottom + dims.border_.top + dims.border_.bottom) / LayoutEngine::getFixedPointScale();
			node->setActiveRect(rect(x, y, w, h));
		}
	}

	void Box::handleRenderBackground(const KRE::SceneTreePtr& scene_tree, const point& offset) const
	{
		auto& dims = getDimensions();
		NodePtr node = getNode();
		if(node != nullptr && node->hasTag(ElementId::BODY)) {
			//dims = getRootDimensions();
		}
		background_info_.render(scene_tree, dims);
	}

	void Box::handleRenderBorder(const KRE::SceneTreePtr& scene_tree, const point& offset) const
	{
		border_info_.render(scene_tree, getDimensions());
	}

	void Box::handleRenderFilters(const KRE::SceneTreePtr& scene_tree, const point& offset) const
	{
		using namespace KRE;

		auto node = getStyleNode();
		if(node == nullptr || node->getFilters() == nullptr) {
			return;
		}

		// if we have a drop-shadow filter, w/h need to be bigger.
		// with all the changes that implies.
		//const int w = (getMBPWidth() + getWidth()) / LayoutEngine::getFixedPointScale();
		//const int h = (getMBPHeight() + getHeight()) / LayoutEngine::getFixedPointScale();
		// XXX this is wrong. It needs to be the layout size.
		const int w = WindowManager::getMainWindow()->width();
		const int h = WindowManager::getMainWindow()->height();

		const int x = offset.x / LayoutEngine::getFixedPointScale();
		const int y = offset.y / LayoutEngine::getFixedPointScale();
		
		auto filters = node->getFilters()->getFilters();

		if(!filters.empty()) {
			// Need to render the scene at full-size into the render buffer.
			//auto camera = Camera::createInstance("SceneTree:Camera", 0, w, 0, h);
			//camera->setOrthoWindow(0, w, 0, h);
			//scene_tree->setCamera(camera);
		}

		for(auto& filter : filters) {
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

			switch(filter->id()) {
				case CssFilterId::BRIGHTNESS: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, filter->getComputedLength());
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::CONTRAST: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, filter->getComputedLength());
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::GRAYSCALE: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, filter->getComputedLength());
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::HUE_ROTATE: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, filter->getComputedAngle());
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::INVERT: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, filter->getComputedLength());
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::OPACITY: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, filter->getComputedLength());
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::SEPIA: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, filter->getComputedLength());
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::SATURATE: {
					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 0);
						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, filter->getComputedLength());
						//LOG_DEBUG("filter->getComputedLength(): " << filter->getComputedLength());
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::BLUR: {
					if(filter->getComputedLength() == 0) {
						continue;
					}
					const int kernel_radius = filter->getKernelRadius();
					const float sigma = filter->getComputedLength();

					auto blur7_shader = ShaderProgram::createGaussianShader(kernel_radius)->clone();
					const int blur7_two = blur7_shader->getUniform("texel_height_offset");
					const int blur7_tho = blur7_shader->getUniform("texel_height_offset");
					const int u_gaussian7 = blur7_shader->getUniform("gaussian");
					const int tex_overlayh = blur7_shader->getUniform("tex_overlay");
					blur7_shader->setUniformDrawFunction([tex_overlayh, filter, blur7_two, blur7_tho, u_gaussian7, h](ShaderProgramPtr shader) {
						shader->setUniformValue(blur7_two, 0.0f);
						shader->setUniformValue(blur7_tho, 1.0f / (static_cast<float>(h)-1.0f));
						auto gaussian = KRE::generate_gaussian(filter->getComputedLength(), filter->getKernelRadius());
						shader->setUniformValue(u_gaussian7,  gaussian.data());
						shader->setUniformValue(tex_overlayh, 0);
					});
					auto rt_hblur = RenderTarget::create(w, h);
					rt_hblur->setShader(blur7_shader);
					scene_tree->addRenderTarget(rt_hblur);

					auto rt = RenderTarget::create(w, h);
					rt->setShader(filter_shader);
					rt->setClearColor(Color(0,0,0,0));
					filter_shader->setUniformDrawFunction([filter, w, u_blur, u_sepia, u_brightness, u_contrast, 
						u_grayscale, u_hue_rotate, u_invert, u_opacity, 
						u_saturate, blur_two, blur_tho, u_gaussian](ShaderProgramPtr shader) {
						shader->setUniformValue(u_blur, 1);
						shader->setUniformValue(blur_two, 1.0f / (static_cast<float>(w) - 1.0f));
						shader->setUniformValue(blur_tho, 0.0f);
						auto gaussian = KRE::generate_gaussian(filter->getComputedLength(), filter->getKernelRadius());
						shader->setUniformValue(u_gaussian,  gaussian.data());

						shader->setUniformValue(u_sepia, 0.0f);
						shader->setUniformValue(u_brightness, 1.0f);
						shader->setUniformValue(u_contrast, 1.0f);
						shader->setUniformValue(u_grayscale, 0.0f);
						// angle in radians
						shader->setUniformValue(u_hue_rotate, 0.0f / 180.0f * 3.141592653f);
						shader->setUniformValue(u_invert, 0.0f);
						shader->setUniformValue(u_opacity, 1.0f);
						shader->setUniformValue(u_saturate, 1.0f);
					});
					scene_tree->addRenderTarget(rt);
					break;
				}
				case CssFilterId::DROP_SHADOW: {
					/*auto& shadow = filter->getShadow();
					if(shadow == nullptr || shadow->getBlur().compute() == 0) {
						continue;					
					}

					// 1) Render alpha map to texture.
					// 2) blur it
					auto rta = RenderTarget::create(w, h);
					auto ashader = ShaderProgram::getProgram("alphaizer");
					rta->setShader(ashader);
					scene_tree->addRenderTarget(rta);

					const int kernel_radius = filter->getKernelRadius();
					// XXX fix this - the gaussian should come from the BoxShadow structure in this case
					const float sigma = shadow->getBlur().compute() / LayoutEngine::getFixedPointScaleFloat();
					const std::shared_ptr<std::vector<float>> gaussian = filter->getGaussian(sigma);

					int bw = w + 4*kernel_radius;
					int bh = h + 4*kernel_radius;
					rta->setPosition(2 * kernel_radius, 2 * kernel_radius);
					auto blur_camera = Camera::createInstance("SceneTree:Camera", 0, bw, 0, bh);
					blur_camera->setOrthoWindow(0, bw, 0, bh);
					rta->setCamera(blur_camera);

					auto blur7_shader = ShaderProgram::createGaussianShader(kernel_radius)->clone();
					const int blur7_two = blur7_shader->getUniform("texel_width_offset");
					const int blur7_tho = blur7_shader->getUniform("texel_height_offset");
					const int u_gaussian7 = blur7_shader->getUniform("gaussian");
					const int tex_overlayh = blur7_shader->getUniform("tex_overlay");
					blur7_shader->setUniformDrawFunction([blur7_two, blur7_tho, u_gaussian7, gaussian, tex_overlayh, bh](ShaderProgramPtr shader) {
						shader->setUniformValue(blur7_two, 0.0f);
						shader->setUniformValue(blur7_tho, 2.0f / (static_cast<float>(bh - 1)));
						shader->setUniformValue(u_gaussian7,  gaussian->data());
						shader->setUniformValue(tex_overlayh, 0);
					});
					auto rt_hblur = RenderTarget::create(bw, bh, 2);
					rt_hblur->setShader(blur7_shader);
					scene_tree->addRenderTarget(rt_hblur);
					rt_hblur->setCamera(blur_camera);

					auto blur7w_shader = ShaderProgram::createGaussianShader(kernel_radius)->clone();
					const int blur7w_two = blur7w_shader->getUniform("texel_width_offset");
					const int blur7w_tho = blur7w_shader->getUniform("texel_height_offset");
					const int u_gaussian7w= blur7w_shader->getUniform("gaussian");
					const int tex_overlay = blur7w_shader->getUniform("tex_overlay");
					const int tm1 = blur7w_shader->getUniform("u_tex_map1");
					blur7w_shader->setUniformDrawFunction([blur7w_two, blur7w_tho, u_gaussian7w, gaussian, bw, tex_overlay, tm1](ShaderProgramPtr shader) {
						shader->setUniformValue(blur7w_two, 2.0f / (static_cast<float>(bw - 1)));
						shader->setUniformValue(blur7w_tho, 0.0f);
						shader->setUniformValue(u_gaussian7w,  gaussian->data());
						shader->setUniformValue(tex_overlay, 1);
						shader->setUniformValue(tm1, 1);
					});
					auto rt_wblur = RenderTarget::create(bw, bh);
					rt_wblur->setShader(blur7w_shader);
					rt_wblur->setColor(*shadow->getColor().compute());
					scene_tree->addRenderTarget(rt_wblur);*/
					break;
				}
				default: break;
			}
		}
	}
}
