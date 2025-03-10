/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021-2022, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2022, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <LibIPC/ConnectionFromClient.h>
#include <LibJS/Forward.h>
#include <LibJS/Heap/Handle.h>
#include <LibWeb/CSS/PreferredColorScheme.h>
#include <LibWeb/Cookie/ParsedCookie.h>
#include <LibWeb/Forward.h>
#include <LibWeb/Loader/FileRequest.h>
#include <LibWeb/Platform/Timer.h>
#include <WebContent/Forward.h>
#include <WebContent/WebContentClientEndpoint.h>
#include <WebContent/WebContentConsoleClient.h>
#include <WebContent/WebContentServerEndpoint.h>

namespace WebContent {

class ConnectionFromClient final
    : public IPC::ConnectionFromClient<WebContentClientEndpoint, WebContentServerEndpoint> {
    C_OBJECT(ConnectionFromClient);

public:
    ~ConnectionFromClient() override = default;

    virtual void die() override;

    void initialize_js_console(Badge<PageHost>);

    void request_file(NonnullRefPtr<Web::FileRequest>&);

    Optional<int> fd() { return socket().fd(); }

private:
    explicit ConnectionFromClient(NonnullOwnPtr<Core::Stream::LocalSocket>);

    Web::Page& page();
    Web::Page const& page() const;

    virtual void connect_to_webdriver(String const& webdriver_ipc_path) override;
    virtual void update_system_theme(Core::AnonymousBuffer const&) override;
    virtual void update_system_fonts(String const&, String const&, String const&) override;
    virtual void update_screen_rects(Vector<Gfx::IntRect> const&, u32) override;
    virtual void load_url(URL const&) override;
    virtual void load_html(String const&, URL const&) override;
    virtual void paint(Gfx::IntRect const&, i32) override;
    virtual void set_viewport_rect(Gfx::IntRect const&) override;
    virtual void mouse_down(Gfx::IntPoint const&, unsigned, unsigned, unsigned) override;
    virtual void mouse_move(Gfx::IntPoint const&, unsigned, unsigned, unsigned) override;
    virtual void mouse_up(Gfx::IntPoint const&, unsigned, unsigned, unsigned) override;
    virtual void mouse_wheel(Gfx::IntPoint const&, unsigned, unsigned, unsigned, i32, i32) override;
    virtual void doubleclick(Gfx::IntPoint const&, unsigned, unsigned, unsigned) override;
    virtual void key_down(i32, unsigned, u32) override;
    virtual void key_up(i32, unsigned, u32) override;
    virtual void add_backing_store(i32, Gfx::ShareableBitmap const&) override;
    virtual void remove_backing_store(i32) override;
    virtual void debug_request(String const&, String const&) override;
    virtual void get_source() override;
    virtual Messages::WebContentServer::SerializeSourceResponse serialize_source() override;
    virtual void inspect_dom_tree() override;
    virtual Messages::WebContentServer::InspectDomNodeResponse inspect_dom_node(i32 node_id, Optional<Web::CSS::Selector::PseudoElement> const& pseudo_element) override;
    virtual Messages::WebContentServer::GetHoveredNodeIdResponse get_hovered_node_id() override;
    virtual Messages::WebContentServer::DumpLayoutTreeResponse dump_layout_tree() override;
    virtual void set_content_filters(Vector<String> const&) override;
    virtual void set_proxy_mappings(Vector<String> const&, HashMap<String, size_t> const&) override;
    virtual void set_preferred_color_scheme(Web::CSS::PreferredColorScheme const&) override;
    virtual void set_has_focus(bool) override;
    virtual void set_is_scripting_enabled(bool) override;
    virtual void set_window_position(Gfx::IntPoint const&) override;
    virtual void set_window_size(Gfx::IntSize const&) override;
    virtual void handle_file_return(i32 error, Optional<IPC::File> const& file, i32 request_id) override;
    virtual void set_system_visibility_state(bool visible) override;

    virtual void js_console_input(String const&) override;
    virtual void run_javascript(String const&) override;
    virtual void js_console_request_messages(i32) override;

    virtual Messages::WebContentServer::GetDocumentElementResponse get_document_element() override;
    virtual Messages::WebContentServer::QuerySelectorAllResponse query_selector_all(i32 start_node_id, String const& selector) override;
    virtual void scroll_element_into_view(i32 element_id) override;
    virtual Messages::WebContentServer::IsElementSelectedResponse is_element_selected(i32 element_id) override;
    virtual Messages::WebContentServer::GetElementAttributeResponse get_element_attribute(i32 element_id, String const& name) override;
    virtual Messages::WebContentServer::GetElementPropertyResponse get_element_property(i32 element_id, String const& name) override;
    virtual Messages::WebContentServer::GetActiveDocumentsTypeResponse get_active_documents_type() override;
    virtual Messages::WebContentServer::GetComputedValueForElementResponse get_computed_value_for_element(i32 element_id, String const& property_name) override;
    virtual Messages::WebContentServer::GetElementTextResponse get_element_text(i32 element_id) override;
    virtual Messages::WebContentServer::GetElementTagNameResponse get_element_tag_name(i32 element_id) override;
    virtual Messages::WebContentServer::GetElementRectResponse get_element_rect(i32 element_id) override;
    virtual Messages::WebContentServer::IsElementEnabledResponse is_element_enabled(i32 element_id) override;
    virtual Messages::WebContentServer::TakeElementScreenshotResponse take_element_screenshot(i32 element_id) override;
    virtual Messages::WebContentServer::TakeDocumentScreenshotResponse take_document_screenshot() override;

    virtual Messages::WebContentServer::GetLocalStorageEntriesResponse get_local_storage_entries() override;
    virtual Messages::WebContentServer::GetSessionStorageEntriesResponse get_session_storage_entries() override;

    virtual Messages::WebContentServer::GetSelectedTextResponse get_selected_text() override;
    virtual void select_all() override;

    virtual Messages::WebContentServer::WebdriverExecuteScriptResponse webdriver_execute_script(String const& body, Vector<String> const& json_arguments, Optional<u64> const& timeout, bool async) override;

    void flush_pending_paint_requests();

    NonnullOwnPtr<PageHost> m_page_host;
    struct PaintRequest {
        Gfx::IntRect content_rect;
        NonnullRefPtr<Gfx::Bitmap> bitmap;
        i32 bitmap_id { -1 };
    };
    Vector<PaintRequest> m_pending_paint_requests;
    RefPtr<Web::Platform::Timer> m_paint_flush_timer;

    HashMap<i32, NonnullRefPtr<Gfx::Bitmap>> m_backing_stores;

    WeakPtr<JS::Realm> m_realm;
    OwnPtr<WebContentConsoleClient> m_console_client;
    JS::Handle<JS::GlobalObject> m_console_global_object;

    HashMap<int, NonnullRefPtr<Web::FileRequest>> m_requested_files {};
    int last_id { 0 };
};

}
