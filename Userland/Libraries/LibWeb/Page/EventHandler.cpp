/*
 * Copyright (c) 2020-2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Max Wipfli <mail@maxwipfli.ch>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGUI/Event.h>
#include <LibWeb/DOM/Range.h>
#include <LibWeb/DOM/Text.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/Focus.h>
#include <LibWeb/HTML/HTMLAnchorElement.h>
#include <LibWeb/HTML/HTMLIFrameElement.h>
#include <LibWeb/HTML/HTMLImageElement.h>
#include <LibWeb/Layout/InitialContainingBlock.h>
#include <LibWeb/Page/EventHandler.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/Painting/PaintableBox.h>
#include <LibWeb/UIEvents/EventNames.h>
#include <LibWeb/UIEvents/KeyboardEvent.h>
#include <LibWeb/UIEvents/MouseEvent.h>
#include <LibWeb/UIEvents/WheelEvent.h>

namespace Web {

static JS::GCPtr<DOM::Node> dom_node_for_event_dispatch(Painting::Paintable const& paintable)
{
    if (auto node = paintable.mouse_event_target())
        return node;
    if (auto node = paintable.dom_node())
        return node;
    if (auto* layout_parent = paintable.layout_node().parent())
        return layout_parent->dom_node();
    return nullptr;
}

static bool parent_element_for_event_dispatch(Painting::Paintable const& paintable, JS::GCPtr<DOM::Node>& node, Layout::Node const*& layout_node)
{
    layout_node = &paintable.layout_node();
    while (layout_node && node && !node->is_element() && layout_node->parent()) {
        layout_node = layout_node->parent();
        if (layout_node->is_anonymous())
            continue;
        node = layout_node->dom_node();
    }
    return node && layout_node;
}

static Gfx::StandardCursor cursor_css_to_gfx(Optional<CSS::Cursor> cursor)
{
    if (!cursor.has_value()) {
        return Gfx::StandardCursor::None;
    }
    switch (cursor.value()) {
    case CSS::Cursor::Crosshair:
    case CSS::Cursor::Cell:
        return Gfx::StandardCursor::Crosshair;
    case CSS::Cursor::Grab:
    case CSS::Cursor::Grabbing:
        return Gfx::StandardCursor::Drag;
    case CSS::Cursor::Pointer:
        return Gfx::StandardCursor::Hand;
    case CSS::Cursor::Help:
        return Gfx::StandardCursor::Help;
    case CSS::Cursor::None:
        return Gfx::StandardCursor::Hidden;
    case CSS::Cursor::Text:
    case CSS::Cursor::VerticalText:
        return Gfx::StandardCursor::IBeam;
    case CSS::Cursor::Move:
    case CSS::Cursor::AllScroll:
        return Gfx::StandardCursor::Move;
    case CSS::Cursor::Progress:
    case CSS::Cursor::Wait:
        return Gfx::StandardCursor::Wait;
    case CSS::Cursor::ColResize:
        return Gfx::StandardCursor::ResizeColumn;
    case CSS::Cursor::EResize:
    case CSS::Cursor::WResize:
    case CSS::Cursor::EwResize:
        return Gfx::StandardCursor::ResizeHorizontal;
    case CSS::Cursor::RowResize:
        return Gfx::StandardCursor::ResizeRow;
    case CSS::Cursor::NResize:
    case CSS::Cursor::SResize:
    case CSS::Cursor::NsResize:
        return Gfx::StandardCursor::ResizeVertical;
    case CSS::Cursor::NeResize:
    case CSS::Cursor::SwResize:
    case CSS::Cursor::NeswResize:
        return Gfx::StandardCursor::ResizeDiagonalBLTR;
    case CSS::Cursor::NwResize:
    case CSS::Cursor::SeResize:
    case CSS::Cursor::NwseResize:
        return Gfx::StandardCursor::ResizeDiagonalTLBR;
    case CSS::Cursor::ZoomIn:
    case CSS::Cursor::ZoomOut:
        return Gfx::StandardCursor::Zoom;
    default:
        return Gfx::StandardCursor::None;
    }
}

static Gfx::IntPoint compute_mouse_event_offset(Gfx::IntPoint const& position, Layout::Node const& layout_node)
{
    auto top_left_of_layout_node = layout_node.box_type_agnostic_position();
    return {
        position.x() - static_cast<int>(top_left_of_layout_node.x()),
        position.y() - static_cast<int>(top_left_of_layout_node.y())
    };
}

EventHandler::EventHandler(Badge<HTML::BrowsingContext>, HTML::BrowsingContext& browsing_context)
    : m_browsing_context(browsing_context)
    , m_edit_event_handler(make<EditEventHandler>(browsing_context))
{
}

EventHandler::~EventHandler() = default;

Layout::InitialContainingBlock const* EventHandler::layout_root() const
{
    if (!m_browsing_context.active_document())
        return nullptr;
    return m_browsing_context.active_document()->layout_node();
}

Layout::InitialContainingBlock* EventHandler::layout_root()
{
    if (!m_browsing_context.active_document())
        return nullptr;
    return m_browsing_context.active_document()->layout_node();
}

Painting::PaintableBox* EventHandler::paint_root()
{
    if (!m_browsing_context.active_document())
        return nullptr;
    return const_cast<Painting::PaintableBox*>(m_browsing_context.active_document()->paint_box());
}

Painting::PaintableBox const* EventHandler::paint_root() const
{
    if (!m_browsing_context.active_document())
        return nullptr;
    return const_cast<Painting::PaintableBox*>(m_browsing_context.active_document()->paint_box());
}

bool EventHandler::handle_mousewheel(Gfx::IntPoint const& position, unsigned button, unsigned buttons, unsigned int modifiers, int wheel_delta_x, int wheel_delta_y)
{
    if (m_browsing_context.active_document())
        m_browsing_context.active_document()->update_layout();

    if (!paint_root())
        return false;

    if (modifiers & KeyModifier::Mod_Shift)
        swap(wheel_delta_x, wheel_delta_y);

    bool handled_event = false;

    RefPtr<Painting::Paintable> paintable;
    if (m_mouse_event_tracking_layout_node) {
        paintable = m_mouse_event_tracking_layout_node->paintable();
    } else {
        if (auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::Exact); result.has_value())
            paintable = result->paintable;
    }

    if (paintable) {
        paintable->handle_mousewheel({}, position, buttons, modifiers, wheel_delta_x, wheel_delta_y);

        auto node = dom_node_for_event_dispatch(*paintable);

        if (node) {
            // FIXME: Support wheel events in nested browsing contexts.
            if (is<HTML::HTMLIFrameElement>(*node)) {
                return false;
            }

            // Search for the first parent of the hit target that's an element.
            Layout::Node const* layout_node;
            if (!parent_element_for_event_dispatch(*paintable, node, layout_node))
                return false;

            auto offset = compute_mouse_event_offset(position, *layout_node);
            if (node->dispatch_event(*UIEvents::WheelEvent::create_from_platform_event(node->realm(), UIEvents::EventNames::wheel, offset.x(), offset.y(), position.x(), position.y(), wheel_delta_x, wheel_delta_y, buttons, button))) {
                if (auto* page = m_browsing_context.page()) {
                    page->client().page_did_request_scroll(wheel_delta_x * 20, wheel_delta_y * 20);
                }
            }

            handled_event = true;
        }
    }

    return handled_event;
}

bool EventHandler::handle_mouseup(Gfx::IntPoint const& position, unsigned button, unsigned buttons, unsigned modifiers)
{
    if (m_browsing_context.active_document())
        m_browsing_context.active_document()->update_layout();

    if (!paint_root())
        return false;

    bool handled_event = false;

    RefPtr<Painting::Paintable> paintable;
    if (m_mouse_event_tracking_layout_node) {
        paintable = m_mouse_event_tracking_layout_node->paintable();
    } else {
        if (auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::Exact); result.has_value())
            paintable = result->paintable;
    }

    if (paintable && paintable->wants_mouse_events()) {
        if (paintable->handle_mouseup({}, position, button, modifiers) == Painting::Paintable::DispatchEventOfSameName::No)
            return false;

        // Things may have changed as a consequence of Layout::Node::handle_mouseup(). Hit test again.
        if (!paint_root())
            return true;
        if (auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::Exact); result.has_value())
            paintable = result->paintable;
    }

    if (paintable) {
        auto node = dom_node_for_event_dispatch(*paintable);

        if (node) {
            if (is<HTML::HTMLIFrameElement>(*node)) {
                if (auto* nested_browsing_context = static_cast<HTML::HTMLIFrameElement&>(*node).nested_browsing_context())
                    return nested_browsing_context->event_handler().handle_mouseup(position.translated(compute_mouse_event_offset({}, paintable->layout_node())), button, buttons, modifiers);
                return false;
            }

            // Search for the first parent of the hit target that's an element.
            // "The click event type MUST be dispatched on the topmost event target indicated by the pointer." (https://www.w3.org/TR/uievents/#event-type-click)
            // "The topmost event target MUST be the element highest in the rendering order which is capable of being an event target." (https://www.w3.org/TR/uievents/#topmost-event-target)
            Layout::Node const* layout_node;
            if (!parent_element_for_event_dispatch(*paintable, node, layout_node)) {
                // FIXME: This is pretty ugly but we need to bail out here.
                goto after_node_use;
            }

            auto offset = compute_mouse_event_offset(position, *layout_node);
            node->dispatch_event(*UIEvents::MouseEvent::create_from_platform_event(node->realm(), UIEvents::EventNames::mouseup, offset.x(), offset.y(), position.x(), position.y(), buttons, button));
            handled_event = true;

            bool run_activation_behavior = true;
            if (node.ptr() == m_mousedown_target && button == GUI::MouseButton::Primary) {
                run_activation_behavior = node->dispatch_event(*UIEvents::MouseEvent::create_from_platform_event(node->realm(), UIEvents::EventNames::click, offset.x(), offset.y(), position.x(), position.y(), button));
            }

            if (run_activation_behavior) {
                // FIXME: This is ad-hoc and incorrect. The reason this exists is
                //        because we are missing browsing context navigation:
                //
                //        https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate
                //
                //        Additionally, we currently cannot spawn a new top-level
                //        browsing context for new tab operations, because the new
                //        top-level browsing context would be in another process. To
                //        fix this, there needs to be some way to be able to
                //        communicate with browsing contexts in remote WebContent
                //        processes, and then step 8 of this algorithm needs to be
                //        implemented in BrowsingContext::choose_a_browsing_context:
                //
                //        https://html.spec.whatwg.org/multipage/browsers.html#the-rules-for-choosing-a-browsing-context-given-a-browsing-context-name
                if (JS::GCPtr<HTML::HTMLAnchorElement> link = node->enclosing_link_element()) {
                    JS::NonnullGCPtr<DOM::Document> document = *m_browsing_context.active_document();
                    auto href = link->href();
                    auto url = document->parse_url(href);
                    dbgln("Web::EventHandler: Clicking on a link to {}", url);
                    if (button == GUI::MouseButton::Primary) {
                        if (href.starts_with("javascript:"sv)) {
                            document->run_javascript(href.substring_view(11, href.length() - 11));
                        } else if (!url.fragment().is_null() && url.equals(document->url(), AK::URL::ExcludeFragment::Yes)) {
                            m_browsing_context.scroll_to_anchor(url.fragment());
                        } else {
                            if (m_browsing_context.is_top_level()) {
                                if (auto* page = m_browsing_context.page())
                                    page->client().page_did_click_link(url, link->target(), modifiers);
                            }
                        }
                    } else if (button == GUI::MouseButton::Middle) {
                        if (auto* page = m_browsing_context.page())
                            page->client().page_did_middle_click_link(url, link->target(), modifiers);
                    } else if (button == GUI::MouseButton::Secondary) {
                        if (auto* page = m_browsing_context.page())
                            page->client().page_did_request_link_context_menu(m_browsing_context.to_top_level_position(position), url, link->target(), modifiers);
                    }
                } else if (button == GUI::MouseButton::Secondary) {
                    if (is<HTML::HTMLImageElement>(*node)) {
                        auto& image_element = verify_cast<HTML::HTMLImageElement>(*node);
                        auto image_url = image_element.document().parse_url(image_element.src());
                        if (auto* page = m_browsing_context.page())
                            page->client().page_did_request_image_context_menu(m_browsing_context.to_top_level_position(position), image_url, "", modifiers, image_element.bitmap());
                    } else if (auto* page = m_browsing_context.page()) {
                        page->client().page_did_request_context_menu(m_browsing_context.to_top_level_position(position));
                    }
                }
            }
        }
    }

after_node_use:
    if (button == GUI::MouseButton::Primary)
        m_in_mouse_selection = false;
    return handled_event;
}

bool EventHandler::handle_mousedown(Gfx::IntPoint const& position, unsigned button, unsigned buttons, unsigned modifiers)
{
    if (m_browsing_context.active_document())
        m_browsing_context.active_document()->update_layout();

    if (!paint_root())
        return false;

    JS::NonnullGCPtr<DOM::Document> document = *m_browsing_context.active_document();
    JS::GCPtr<DOM::Node> node;

    {
        RefPtr<Painting::Paintable> paintable;
        if (m_mouse_event_tracking_layout_node) {
            paintable = m_mouse_event_tracking_layout_node->paintable();
        } else {
            auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::Exact);
            if (!result.has_value())
                return false;
            paintable = result->paintable;
        }

        auto pointer_events = paintable->computed_values().pointer_events();
        // FIXME: Handle other values for pointer-events.
        VERIFY(pointer_events != CSS::PointerEvents::None);

        node = dom_node_for_event_dispatch(*paintable);
        document->set_hovered_node(node);

        if (paintable->wants_mouse_events()) {
            if (paintable->handle_mousedown({}, position, button, modifiers) == Painting::Paintable::DispatchEventOfSameName::No)
                return false;
        }

        if (!node)
            return false;

        if (is<HTML::HTMLIFrameElement>(*node)) {
            if (auto* nested_browsing_context = static_cast<HTML::HTMLIFrameElement&>(*node).nested_browsing_context())
                return nested_browsing_context->event_handler().handle_mousedown(position.translated(compute_mouse_event_offset({}, paintable->layout_node())), button, buttons, modifiers);
            return false;
        }

        if (auto* page = m_browsing_context.page())
            page->set_focused_browsing_context({}, m_browsing_context);

        // Search for the first parent of the hit target that's an element.
        // "The click event type MUST be dispatched on the topmost event target indicated by the pointer." (https://www.w3.org/TR/uievents/#event-type-click)
        // "The topmost event target MUST be the element highest in the rendering order which is capable of being an event target." (https://www.w3.org/TR/uievents/#topmost-event-target)
        Layout::Node const* layout_node;
        if (!parent_element_for_event_dispatch(*paintable, node, layout_node))
            return false;

        m_mousedown_target = node.ptr();
        auto offset = compute_mouse_event_offset(position, *layout_node);
        node->dispatch_event(*UIEvents::MouseEvent::create_from_platform_event(node->realm(), UIEvents::EventNames::mousedown, offset.x(), offset.y(), position.x(), position.y(), buttons, button));
    }

    // NOTE: Dispatching an event may have disturbed the world.
    if (!paint_root() || paint_root() != node->document().paint_box())
        return true;

    if (button == GUI::MouseButton::Primary) {
        if (auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::TextCursor); result.has_value()) {
            auto paintable = result->paintable;
            if (paintable->dom_node()) {
                // See if we want to focus something.
                bool did_focus_something = false;
                for (auto candidate = node; candidate; candidate = candidate->parent()) {
                    if (candidate->is_focusable()) {
                        // When a user activates a click focusable focusable area, the user agent must run the focusing steps on the focusable area with focus trigger set to "click".
                        // Spec Note: Note that focusing is not an activation behavior, i.e. calling the click() method on an element or dispatching a synthetic click event on it won't cause the element to get focused.
                        HTML::run_focusing_steps(candidate.ptr(), nullptr, "click"sv);
                        did_focus_something = true;
                        break;
                    }
                }

                // If we didn't focus anything, place the document text cursor at the mouse position.
                // FIXME: This is all rather strange. Find a better solution.
                if (!did_focus_something) {
                    m_browsing_context.set_cursor_position(DOM::Position(*paintable->dom_node(), result->index_in_node));
                    layout_root()->set_selection({ { paintable->layout_node(), result->index_in_node }, {} });
                    m_in_mouse_selection = true;
                }
            }
        }
    }
    return true;
}

bool EventHandler::handle_mousemove(Gfx::IntPoint const& position, unsigned buttons, unsigned modifiers)
{
    if (m_browsing_context.active_document())
        m_browsing_context.active_document()->update_layout();

    if (!paint_root())
        return false;

    auto& document = *m_browsing_context.active_document();

    bool hovered_node_changed = false;
    bool is_hovering_link = false;
    Gfx::StandardCursor hovered_node_cursor = Gfx::StandardCursor::None;

    RefPtr<Painting::Paintable> paintable;
    Optional<int> start_index;
    if (m_mouse_event_tracking_layout_node) {
        paintable = m_mouse_event_tracking_layout_node->paintable();
    } else {
        if (auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::Exact); result.has_value()) {
            paintable = result->paintable;
            start_index = result->index_in_node;
        }
    }

    const HTML::HTMLAnchorElement* hovered_link_element = nullptr;
    if (paintable) {
        if (paintable->wants_mouse_events()) {
            document.set_hovered_node(paintable->dom_node());
            if (paintable->handle_mousemove({}, position, buttons, modifiers) == Painting::Paintable::DispatchEventOfSameName::No)
                return false;

            // FIXME: It feels a bit aggressive to always update the cursor like this.
            if (auto* page = m_browsing_context.page())
                page->client().page_did_request_cursor_change(Gfx::StandardCursor::None);
        }

        auto node = dom_node_for_event_dispatch(*paintable);

        if (node && is<HTML::HTMLIFrameElement>(*node)) {
            if (auto* nested_browsing_context = static_cast<HTML::HTMLIFrameElement&>(*node).nested_browsing_context())
                return nested_browsing_context->event_handler().handle_mousemove(position.translated(compute_mouse_event_offset({}, paintable->layout_node())), buttons, modifiers);
            return false;
        }

        auto pointer_events = paintable->computed_values().pointer_events();
        // FIXME: Handle other values for pointer-events.
        VERIFY(pointer_events != CSS::PointerEvents::None);

        // Search for the first parent of the hit target that's an element.
        // "The click event type MUST be dispatched on the topmost event target indicated by the pointer." (https://www.w3.org/TR/uievents/#event-type-click)
        // "The topmost event target MUST be the element highest in the rendering order which is capable of being an event target." (https://www.w3.org/TR/uievents/#topmost-event-target)
        Layout::Node const* layout_node;
        bool found_parent_element = parent_element_for_event_dispatch(*paintable, node, layout_node);
        hovered_node_changed = node.ptr() != document.hovered_node();
        document.set_hovered_node(node);
        if (found_parent_element) {
            hovered_link_element = node->enclosing_link_element();
            if (hovered_link_element)
                is_hovering_link = true;

            if (node->is_text()) {
                auto cursor = paintable->computed_values().cursor();
                if (cursor == CSS::Cursor::Auto)
                    hovered_node_cursor = Gfx::StandardCursor::IBeam;
                else
                    hovered_node_cursor = cursor_css_to_gfx(cursor);
            } else if (node->is_element()) {
                auto cursor = paintable->computed_values().cursor();
                if (cursor == CSS::Cursor::Auto)
                    hovered_node_cursor = Gfx::StandardCursor::Arrow;
                else
                    hovered_node_cursor = cursor_css_to_gfx(cursor);
            }

            auto offset = compute_mouse_event_offset(position, *layout_node);
            node->dispatch_event(*UIEvents::MouseEvent::create_from_platform_event(node->realm(), UIEvents::EventNames::mousemove, offset.x(), offset.y(), position.x(), position.y(), buttons));
            // NOTE: Dispatching an event may have disturbed the world.
            if (!paint_root() || paint_root() != node->document().paint_box())
                return true;
        }
        if (m_in_mouse_selection) {
            auto hit = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::TextCursor);
            if (start_index.has_value() && hit.has_value() && hit->dom_node()) {
                m_browsing_context.set_cursor_position(DOM::Position(*hit->dom_node(), *start_index));
                layout_root()->set_selection_end({ hit->paintable->layout_node(), hit->index_in_node });
                m_browsing_context.set_needs_display();
            }
            if (auto* page = m_browsing_context.page())
                page->client().page_did_change_selection();
        }
    }

    if (auto* page = m_browsing_context.page()) {
        page->client().page_did_request_cursor_change(hovered_node_cursor);

        if (hovered_node_changed) {
            JS::GCPtr<HTML::HTMLElement> hovered_html_element = document.hovered_node() ? document.hovered_node()->enclosing_html_element_with_attribute(HTML::AttributeNames::title) : nullptr;
            if (hovered_html_element && !hovered_html_element->title().is_null()) {
                page->client().page_did_enter_tooltip_area(m_browsing_context.to_top_level_position(position), hovered_html_element->title());
            } else {
                page->client().page_did_leave_tooltip_area();
            }
            if (is_hovering_link)
                page->client().page_did_hover_link(document.parse_url(hovered_link_element->href()));
            else
                page->client().page_did_unhover_link();
        }
    }
    return true;
}

bool EventHandler::handle_doubleclick(Gfx::IntPoint const& position, unsigned button, unsigned buttons, unsigned modifiers)
{
    if (m_browsing_context.active_document())
        m_browsing_context.active_document()->update_layout();

    if (!paint_root())
        return false;

    RefPtr<Painting::Paintable> paintable;
    if (m_mouse_event_tracking_layout_node) {
        paintable = m_mouse_event_tracking_layout_node->paintable();
    } else {
        auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::Exact);
        if (!result.has_value())
            return false;
        paintable = result->paintable;
    }

    auto pointer_events = paintable->computed_values().pointer_events();
    // FIXME: Handle other values for pointer-events.
    if (pointer_events == CSS::PointerEvents::None)
        return false;

    auto node = dom_node_for_event_dispatch(*paintable);

    if (paintable->wants_mouse_events()) {
        // FIXME: Handle double clicks.
    }

    if (!node)
        return false;

    if (is<HTML::HTMLIFrameElement>(*node)) {
        if (auto* nested_browsing_context = static_cast<HTML::HTMLIFrameElement&>(*node).nested_browsing_context())
            return nested_browsing_context->event_handler().handle_doubleclick(position.translated(compute_mouse_event_offset({}, paintable->layout_node())), button, buttons, modifiers);
        return false;
    }

    // Search for the first parent of the hit target that's an element.
    // "The topmost event target MUST be the element highest in the rendering order which is capable of being an event target." (https://www.w3.org/TR/uievents/#topmost-event-target)
    Layout::Node const* layout_node;
    if (!parent_element_for_event_dispatch(*paintable, node, layout_node))
        return false;

    auto offset = compute_mouse_event_offset(position, *layout_node);
    node->dispatch_event(*UIEvents::MouseEvent::create_from_platform_event(node->realm(), UIEvents::EventNames::dblclick, offset.x(), offset.y(), position.x(), position.y(), buttons, button));

    // NOTE: Dispatching an event may have disturbed the world.
    if (!paint_root() || paint_root() != node->document().paint_box())
        return true;

    if (button == GUI::MouseButton::Primary) {
        if (auto result = paint_root()->hit_test(position.to_type<float>(), Painting::HitTestType::TextCursor); result.has_value()) {
            auto paintable = result->paintable;
            if (!paintable->dom_node())
                return true;

            auto const& layout_node = paintable->layout_node();
            if (!layout_node.is_text_node())
                return true;
            auto const& text_for_rendering = verify_cast<Layout::TextNode>(layout_node).text_for_rendering();

            int first_word_break_before = [&] {
                // Start from one before the index position to prevent selecting only spaces between words, caused by the addition below.
                // This also helps us dealing with cases where index is equal to the string length.
                for (int i = result->index_in_node - 1; i >= 0; --i) {
                    if (is_ascii_space(text_for_rendering[i])) {
                        // Don't include the space in the selection
                        return i + 1;
                    }
                }
                return 0;
            }();

            int first_word_break_after = [&] {
                for (size_t i = result->index_in_node; i < text_for_rendering.length(); ++i) {
                    if (is_ascii_space(text_for_rendering[i]))
                        return i;
                }
                return text_for_rendering.length();
            }();

            m_browsing_context.set_cursor_position(DOM::Position(*paintable->dom_node(), first_word_break_after));
            layout_root()->set_selection({ { paintable->layout_node(), first_word_break_before }, { paintable->layout_node(), first_word_break_after } });
        }
    }

    return true;
}

bool EventHandler::focus_next_element()
{
    if (!m_browsing_context.active_document())
        return false;
    auto* element = m_browsing_context.active_document()->focused_element();
    if (!element) {
        element = m_browsing_context.active_document()->first_child_of_type<DOM::Element>();
        if (element && element->is_focusable()) {
            m_browsing_context.active_document()->set_focused_element(element);
            return true;
        }
    }

    for (element = element->next_element_in_pre_order(); element && !element->is_focusable(); element = element->next_element_in_pre_order())
        ;

    m_browsing_context.active_document()->set_focused_element(element);
    return element;
}

bool EventHandler::focus_previous_element()
{
    if (!m_browsing_context.active_document())
        return false;
    auto* element = m_browsing_context.active_document()->focused_element();
    if (!element) {
        element = m_browsing_context.active_document()->last_child_of_type<DOM::Element>();
        if (element && element->is_focusable()) {
            m_browsing_context.active_document()->set_focused_element(element);
            return true;
        }
    }

    for (element = element->previous_element_in_pre_order(); element && !element->is_focusable(); element = element->previous_element_in_pre_order())
        ;

    m_browsing_context.active_document()->set_focused_element(element);
    return element;
}

constexpr bool should_ignore_keydown_event(u32 code_point)
{
    // FIXME: There are probably also keys with non-zero code points that should be filtered out.
    return code_point == 0 || code_point == 27;
}

bool EventHandler::fire_keyboard_event(FlyString const& event_name, HTML::BrowsingContext& browsing_context, KeyCode key, unsigned int modifiers, u32 code_point)
{
    JS::NonnullGCPtr<DOM::Document> document = *browsing_context.active_document();
    if (!document)
        return false;

    if (JS::GCPtr<DOM::Element> focused_element = document->focused_element()) {
        if (is<HTML::BrowsingContextContainer>(*focused_element)) {
            auto& browsing_context_container = verify_cast<HTML::BrowsingContextContainer>(*focused_element);
            if (browsing_context_container.nested_browsing_context())
                return fire_keyboard_event(event_name, *browsing_context_container.nested_browsing_context(), key, modifiers, code_point);
        }

        auto event = UIEvents::KeyboardEvent::create_from_platform_event(document->realm(), event_name, key, modifiers, code_point);
        return focused_element->dispatch_event(*event);
    }

    // FIXME: De-duplicate this. This is just to prevent wasting a KeyboardEvent alloction when recursing into an (i)frame.
    auto event = UIEvents::KeyboardEvent::create_from_platform_event(document->realm(), event_name, key, modifiers, code_point);

    if (JS::GCPtr<HTML::HTMLElement> body = document->body())
        return body->dispatch_event(*event);

    return document->root().dispatch_event(*event);
}

bool EventHandler::handle_keydown(KeyCode key, unsigned modifiers, u32 code_point)
{
    if (!m_browsing_context.active_document())
        return false;

    JS::NonnullGCPtr<DOM::Document> document = *m_browsing_context.active_document();
    if (!document->layout_node())
        return false;

    JS::NonnullGCPtr<Layout::InitialContainingBlock> layout_root = *document->layout_node();

    if (key == KeyCode::Key_Tab) {
        if (modifiers & KeyModifier::Mod_Shift)
            return focus_previous_element();
        return focus_next_element();
    }

    if (layout_root->selection().is_valid()) {
        auto range = layout_root->selection().to_dom_range()->normalized();
        if (range->start_container()->is_editable()) {
            layout_root->set_selection({});

            // FIXME: This doesn't work for some reason?
            m_browsing_context.set_cursor_position({ *range->start_container(), range->start_offset() });

            if (key == KeyCode::Key_Backspace || key == KeyCode::Key_Delete) {
                m_edit_event_handler->handle_delete(*range);
                return true;
            }
            if (!should_ignore_keydown_event(code_point)) {
                m_edit_event_handler->handle_delete(*range);
                m_edit_event_handler->handle_insert(m_browsing_context.cursor_position(), code_point);
                m_browsing_context.increment_cursor_position_offset();
                return true;
            }
        }
    }

    if (m_browsing_context.cursor_position().is_valid() && m_browsing_context.cursor_position().node()->is_editable()) {
        if (key == KeyCode::Key_Backspace) {
            if (!m_browsing_context.decrement_cursor_position_offset()) {
                // FIXME: Move to the previous node and delete the last character there.
                return true;
            }

            m_edit_event_handler->handle_delete_character_after(m_browsing_context.cursor_position());
            return true;
        }
        if (key == KeyCode::Key_Delete) {
            if (m_browsing_context.cursor_position().offset_is_at_end_of_node()) {
                // FIXME: Move to the next node and delete the first character there.
                return true;
            }
            m_edit_event_handler->handle_delete_character_after(m_browsing_context.cursor_position());
            return true;
        }
        if (key == KeyCode::Key_Right) {
            if (!m_browsing_context.increment_cursor_position_offset()) {
                // FIXME: Move to the next node.
            }
            return true;
        }
        if (key == KeyCode::Key_Left) {
            if (!m_browsing_context.decrement_cursor_position_offset()) {
                // FIXME: Move to the previous node.
            }
            return true;
        }
        if (key == KeyCode::Key_Home) {
            auto& node = *static_cast<DOM::Text*>(const_cast<DOM::Node*>(m_browsing_context.cursor_position().node()));
            m_browsing_context.set_cursor_position(DOM::Position { node, 0 });
            return true;
        }
        if (key == KeyCode::Key_End) {
            auto& node = *static_cast<DOM::Text*>(const_cast<DOM::Node*>(m_browsing_context.cursor_position().node()));
            m_browsing_context.set_cursor_position(DOM::Position { node, (unsigned)node.data().length() });
            return true;
        }
        if (!should_ignore_keydown_event(code_point)) {
            m_edit_event_handler->handle_insert(m_browsing_context.cursor_position(), code_point);
            m_browsing_context.increment_cursor_position_offset();
            return true;
        }

        // NOTE: Because modifier keys should be ignored, we need to return true.
        return true;
    }

    bool continue_ = fire_keyboard_event(UIEvents::EventNames::keydown, m_browsing_context, key, modifiers, code_point);
    if (!continue_)
        return false;

    // FIXME: Work out and implement the difference between this and keydown.
    return fire_keyboard_event(UIEvents::EventNames::keypress, m_browsing_context, key, modifiers, code_point);
}

bool EventHandler::handle_keyup(KeyCode key, unsigned modifiers, u32 code_point)
{
    return fire_keyboard_event(UIEvents::EventNames::keyup, m_browsing_context, key, modifiers, code_point);
}

void EventHandler::set_mouse_event_tracking_layout_node(Layout::Node* layout_node)
{
    m_mouse_event_tracking_layout_node = layout_node;
}

}
