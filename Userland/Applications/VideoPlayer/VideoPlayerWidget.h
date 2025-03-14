/*
 * Copyright (c) 2022, Gregory Bertilson <zaggy1024@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/FixedArray.h>
#include <AK/NonnullRefPtr.h>
#include <LibGUI/Forward.h>
#include <LibGUI/Widget.h>
#include <LibGfx/Forward.h>
#include <LibVideo/DecoderError.h>
#include <LibVideo/PlaybackManager.h>

#include "VideoFrameWidget.h"

namespace VideoPlayer {

class VideoPlayerWidget final : public GUI::Widget {
    C_OBJECT(VideoPlayerWidget)

public:
    void open_file(StringView filename);
    void resume_playback();
    void pause_playback();
    void toggle_pause();

    void update_title();

    void initialize_menubar(GUI::Window&);

private:
    VideoPlayerWidget(GUI::Window&);

    void update_play_pause_icon();
    void on_decoding_error(Video::DecoderError);
    void display_next_frame();

    void cycle_sizing_modes();

    void event(Core::Event&) override;

    GUI::Window& m_window;

    String m_path;

    RefPtr<VideoFrameWidget> m_video_display;
    RefPtr<GUI::HorizontalSlider> m_seek_slider;

    RefPtr<GUI::Toolbar> m_toolbar;

    RefPtr<Gfx::Bitmap> m_play_icon;
    RefPtr<Gfx::Bitmap> m_pause_icon;

    RefPtr<GUI::Action> m_play_pause_action;
    RefPtr<GUI::Label> m_timestamp_label;
    RefPtr<GUI::Action> m_cycle_sizing_modes_action;
    RefPtr<GUI::HorizontalSlider> m_volume_slider;

    RefPtr<Video::PlaybackManager> m_playback_manager;
};

}
