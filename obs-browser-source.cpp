/******************************************************************************
 Copyright (C) 2014 by John R. Bradley <jrb@turrettech.com>
 Copyright (C) 2018 by Hugh Bailey ("Jim") <jim@obsproject.com>

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

#include "obs-browser-source.hpp"
#include "browser-client.hpp"
#include "browser-scheme.hpp"
#include "wide-string.hpp"
#include "json11/json11.hpp"
#include <util/threading.h>
#include <util/dstr.h>
#include <functional>
#include <thread>
#include <mutex>

#ifdef __linux__
#include "linux-keyboard-helpers.hpp"
#endif

#if defined(USE_UI_LOOP) && defined(WIN32)
#include <QEventLoop>
#include <QThread>
#elif defined(USE_UI_LOOP) && defined(__APPLE__)
#include "browser-mac.h"
#endif

using namespace std;
using namespace json11;

static mutex browser_list_mutex;
static BrowserSource *first_browser = nullptr;

static void SendBrowserVisibility(CefRefPtr<CefBrowser> browser, bool isVisible)
{
	if (!browser)
		return;

#if ENABLE_WASHIDDEN
	if (isVisible) {
		browser->GetHost()->WasHidden(false);
		browser->GetHost()->Invalidate(PET_VIEW);
	} else {
		browser->GetHost()->WasHidden(true);
	}
#endif

	CefRefPtr<CefProcessMessage> msg =
		CefProcessMessage::Create("Visibility");
	CefRefPtr<CefListValue> args = msg->GetArgumentList();
	args->SetBool(0, isVisible);
	SendBrowserProcessMessage(browser, PID_RENDERER, msg);
}

void DispatchJSEvent(std::string eventName, std::string jsonString,
		     BrowserSource *browser = nullptr);

BrowserSource::BrowserSource(obs_data_t *, obs_source_t *source_)
	: source(source_)
{
	/* defer update */
	obs_source_update(source, nullptr);

	lock_guard<mutex> lock(browser_list_mutex);
	p_prev_next = &first_browser;
	next = first_browser;
	if (first_browser)
		first_browser->p_prev_next = &next;
	first_browser = this;
}

BrowserSource::~BrowserSource()
{
	DestroyBrowser();
	DestroyTextures();

	lock_guard<mutex> lock(browser_list_mutex);
	if (next)
		next->p_prev_next = p_prev_next;
	*p_prev_next = next;
}

#if CHROME_VERSION_BUILD >= 3683
static speaker_layout GetSpeakerLayout(CefAudioHandler::ChannelLayout cefLayout)
{
	switch (cefLayout) {
	case CEF_CHANNEL_LAYOUT_MONO:
		return SPEAKERS_MONO; /**< Channels: MONO */
	case CEF_CHANNEL_LAYOUT_STEREO:
		return SPEAKERS_STEREO; /**< Channels: FL, FR */
	case CEF_CHANNEL_LAYOUT_2POINT1:
		return SPEAKERS_2POINT1; /**< Channels: FL, FR, LFE */
	case CEF_CHANNEL_LAYOUT_2_2:
	case CEF_CHANNEL_LAYOUT_QUAD:
	case CEF_CHANNEL_LAYOUT_4_0:
		return SPEAKERS_4POINT0; /**< Channels: FL, FR, FC, RC */
	case CEF_CHANNEL_LAYOUT_4_1:
		return SPEAKERS_4POINT1; /**< Channels: FL, FR, FC, LFE, RC */
	case CEF_CHANNEL_LAYOUT_5_1:
	case CEF_CHANNEL_LAYOUT_5_1_BACK:
		return SPEAKERS_5POINT1; /**< Channels: FL, FR, FC, LFE, RL, RR */
	case CEF_CHANNEL_LAYOUT_7_1:
	case CEF_CHANNEL_LAYOUT_7_1_WIDE_BACK:
	case CEF_CHANNEL_LAYOUT_7_1_WIDE:
		return SPEAKERS_7POINT1; /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
	default:
		return SPEAKERS_UNKNOWN;
	}
}
#endif

#if CHROME_VERSION_BUILD < 4103 && CHROME_VERSION_BUILD >= 3683
void BrowserSource::OnAudioStreamStarted(int id,
					 int channel_layout,
					 int sample_rate)
{
	this->id = id;
	AudioStream &stream = audio_streams[id];
	if (!stream.source) {
		stream.source = obs_source_create_private("audio_line", nullptr,
							  nullptr);
		obs_source_release(stream.source);

		obs_source_add_active_child(source, stream.source);

		std::lock_guard<std::mutex> lock(audio_sources_mutex);
		audio_sources.push_back(stream.source);
	}

	stream.speakers = GetSpeakerLayout((CefAudioHandler::ChannelLayout)channel_layout);
	stream.channels = get_audio_channels(stream.speakers);
	stream.sample_rate = sample_rate;
}

void BrowserSource::OnAudioStreamPacket(
	::google::protobuf::RepeatedPtrField<string>* data,
	int32_t frames, int64_t pts)
{
	AudioStream &stream = audio_streams[this->id];
	struct obs_source_audio audio = {};

	std::vector<std::string> buffers(MAX_AV_PLANES);
	for (int i = 0; i < data->size(); i++) {
		buffers[i] = data->at(i);
		if (buffers[i].empty()) {
			continue;
		}
		
		audio.data[i] = (const uint8_t *)buffers[i].data();
	}

	audio.samples_per_sec = stream.sample_rate;
	audio.frames = frames;
	audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	audio.speakers = stream.speakers;
	audio.timestamp = (uint64_t)pts * 1000000LLU;

	obs_source_output_audio(stream.source, &audio);
}

void BrowserSource::OnAudioStreamStopped(int id)
{
	auto pair = audio_streams.find(id);
	if (pair == audio_streams.end()) {
		return;
	}

	AudioStream &stream = pair->second;
	{
		std::lock_guard<std::mutex> lock(audio_sources_mutex);
		for (size_t i = 0; i < audio_sources.size(); i++) {
			obs_source_t *source = audio_sources[i];
			if (source == stream.source) {
				audio_sources.erase(
					audio_sources.begin() + i);
				break;
			}
		}
	}
	audio_streams.erase(pair);
}
#endif

#if CHROME_VERSION_BUILD >= 4103
void BrowserSource::OnAudioStreamStarted(int id,
					 int channel_layout,
					 int sample_rate)
{

}

void BrowserSource::OnAudioStreamPacket(
	::google::protobuf::RepeatedPtrField<string>* data,
	int32_t frames, int64_t pts)
{

}

void BrowserSource::OnAudioStreamStopped(int id)
{

}
#endif


bool BrowserSource::CreateBrowser()
{
	bc->CreateBrowserSource(
		(uint64_t) &source, hwaccel, reroute_audio, 
		width, height, fps, fps_custom,
		obs_get_active_fps(), url, css
	);

	if (reroute_audio)
		bc->OnAudioStreamStarted(this);

	return true;
}

void BrowserSource::DestroyBrowser(bool async)
{
	bc->DestroyBrowserSource((uint64_t) &source, async);
}
#if CHROME_VERSION_BUILD < 4103 && CHROME_VERSION_BUILD >= 3683
void BrowserSource::ClearAudioStreams()
{
// #ifdef WIN32
// 	QueueCEFTask([this]() {
// #elif defined(USE_UI_LOOP) && defined(__APPLE__)
// 	ExecuteTask([this]() {
// #endif
// 		audio_streams.clear();
// 		std::lock_guard<std::mutex> lock(audio_sources_mutex);
// 		audio_sources.clear();
// 	});
}
#endif
void BrowserSource::SendMouseClick(const struct obs_mouse_event *event,
				   int32_t type, bool mouse_up,
				   uint32_t click_count)
{
	bc->SendMouseClick(
		(uint64_t) &source, event->modifiers, event->x,
		event->y, type, mouse_up, click_count);
}

void BrowserSource::SendMouseMove(const struct obs_mouse_event *event,
				  bool mouse_leave)
{
	bc->SendMouseMove(
		(uint64_t) &source, event->modifiers, event->x,
		event->y, mouse_leave);
}

void BrowserSource::SendMouseWheel(const struct obs_mouse_event *event,
				   int x_delta, int y_delta)
{
	bc->SendMouseWheel(
		(uint64_t) &source, event->modifiers, event->x, event->y,
		x_delta, y_delta);
}

void BrowserSource::SendFocus(bool focus)
{
	bc->SendFocus(
		(uint64_t) &source, focus);
}

void BrowserSource::SendKeyClick(const struct obs_key_event *event, bool key_up)
{
	std::string text = event->text;
	uint32_t native_scancode = 0;
#ifdef __linux__
	uint32_t native_vkey = KeyboardCodeFromXKeysym(event->native_vkey);
	uint32_t modifiers = event->native_modifiers;
#elif defined(_WIN32)
	uint32_t native_vkey = event->native_vkey;
	uint32_t modifiers = event->modifiers;
#else
	uint32_t native_vkey = event->native_vkey;
	native_scancode = event->native_scancode;
	uint32_t modifiers = event->native_modifiers;
#endif

	bc->SendKeyClick(
		(uint64_t) &source, text, native_vkey,
		modifiers, native_scancode, key_up);
}

void BrowserSource::SetShowing(bool showing)
{
	is_showing = showing;

	if (shutdown_on_invisible) {
		if (showing) {
			Update();
		} else {
			DestroyBrowser(true);
		}
	} else {
		bc->SetShowing((uint64_t) &source, showing);

#if defined(_WIN32) && defined(SHARED_TEXTURE_SUPPORT_ENABLED)
		if (showing && !fps_custom) {
			reset_frame = false;
		}
#endif
	}
}

void BrowserSource::SetActive(bool active)
{
	bc->SetActive((uint64_t) &source, active);
}

void BrowserSource::Refresh()
{
	bc->Refresh((uint64_t) &source);
}
#ifdef SHARED_TEXTURE_SUPPORT_ENABLED
#ifdef _WIN32

void BrowserSource::RenderSharedTexture(void* shared_handle)
{
	if (shared_handle && this->last_handle != shared_handle) {
		obs_enter_graphics();

		gs_texture_destroy(this->texture);

		this->texture = gs_texture_open_shared(
			(uint32_t)(uintptr_t)shared_handle);

		obs_leave_graphics();
		last_handle = shared_handle;
	}
}

inline void BrowserSource::SignalBeginFrame()
{
	if (reset_frame) {
		if (hwaccel)
			bc->SignalBeginFrame(this);
		else
			bc->RequestPaint(this);
		reset_frame = false;
	} else {
		blog(LOG_INFO, "frame not ready to be rendered");
	}
}
#endif
#endif

void BrowserSource::RenderFrame(int width, int height,
	::google::protobuf::RepeatedPtrField<string>* buffer)
{
	if (!width || !height)
		return;

	if (this->width != width || this->height != height) {
		obs_enter_graphics();
		DestroyTextures();
		obs_leave_graphics();
	}

	const uint8_t *data = (const uint8_t *)buffer->begin()->data();
	if (!this->texture && width && height) {
		obs_enter_graphics();
		this->texture = gs_texture_create(width, height, GS_BGRA, 1,
						(const uint8_t **)&data,
						GS_DYNAMIC);
		this->width = width;
		this->height = height;
		obs_leave_graphics();
	} else {
		obs_enter_graphics();
		gs_texture_set_image(this->texture, data,
				     width * 4, false);
		obs_leave_graphics();
	}
}

void BrowserSource::Update(obs_data_t *settings)
{
	if (settings) {
		bool n_is_local;
		bool n_is_media_flag;
		int n_width;
		int n_height;
		bool n_fps_custom;
		int n_fps;
		bool n_shutdown;
		bool n_restart;
		bool n_reroute;
		std::string n_url;
		std::string n_css;

		n_is_media_flag = obs_data_get_bool(settings, "is_media_flag");
		n_is_local = obs_data_get_bool(settings, "is_local_file");
		n_width = (int)obs_data_get_int(settings, "width");
		n_height = (int)obs_data_get_int(settings, "height");
		n_fps_custom = obs_data_get_bool(settings, "fps_custom");
		n_fps = (int)obs_data_get_int(settings, "fps");
		n_shutdown = obs_data_get_bool(settings, "shutdown");
		n_restart = obs_data_get_bool(settings, "restart_when_active");
		n_css = obs_data_get_string(settings, "css");
		n_url = obs_data_get_string(settings,
					    n_is_local ? "local_file" : "url");
		n_reroute = obs_data_get_bool(settings, "reroute_audio");

		if (n_is_local && !n_url.empty()) {
			n_url = CefURIEncode(n_url, false);

#ifdef _WIN32
			size_t slash = n_url.find("%2F");
			size_t colon = n_url.find("%3A");

			if (slash != std::string::npos &&
			    colon != std::string::npos && colon < slash)
				n_url.replace(colon, 3, ":");
#endif

			while (n_url.find("%5C") != std::string::npos)
				n_url.replace(n_url.find("%5C"), 3, "/");

			while (n_url.find("%2F") != std::string::npos)
				n_url.replace(n_url.find("%2F"), 3, "/");

#if !ENABLE_LOCAL_FILE_URL_SCHEME
			/* http://absolute/ based mapping for older CEF */
			n_url = "http://absolute/" + n_url;
#elif defined(_WIN32)
			/* Widows-style local file URL:
			 * file:///C:/file/path.webm */
			n_url = "file:///" + n_url;
#else
			/* UNIX-style local file URL:
			 * file:///home/user/file.webm */
			n_url = "file://" + n_url;
#endif
		}

#if ENABLE_LOCAL_FILE_URL_SCHEME
		if (astrcmpi_n(n_url.c_str(), "http://absolute/", 16) == 0) {
			/* Replace http://absolute/ URLs with file://
			 * URLs if file:// URLs are enabled */
			n_url = "file:///" + n_url.substr(16);
			n_is_local = true;
		}
#endif
		if (n_is_local == is_local && n_width == width &&
		    n_height == height && n_fps_custom == fps_custom &&
		    n_fps == fps && n_shutdown == shutdown_on_invisible &&
		    n_restart == restart && n_css == css && n_url == url &&
		    n_reroute == reroute_audio &&
			n_is_media_flag == is_media_flag) {
			return;
		}

		is_media_flag = n_is_media_flag;
		is_local = n_is_local;
		width = n_width;
		height = n_height;
		fps = n_fps;
		fps_custom = n_fps_custom;
		shutdown_on_invisible = n_shutdown;
		reroute_audio = n_reroute;
		restart = n_restart;
		css = n_css;
		url = n_url;

		obs_source_set_audio_active(source, reroute_audio);
	}

	DestroyBrowser(true);
	DestroyTextures();
#if CHROME_VERSION_BUILD < 4103 && CHROME_VERSION_BUILD >= 3683
	ClearAudioStreams();
#endif
	if (!shutdown_on_invisible || obs_source_showing(source))
		create_browser = true;

	first_update = false;
}

void BrowserSource::Tick()
{
	if (create_browser && CreateBrowser())
		create_browser = false;
#if defined(_WIN32) && defined(SHARED_TEXTURE_SUPPORT_ENABLED)
	if (!fps_custom)
		reset_frame = true;
#endif
}

extern void ProcessCef();

void BrowserSource::Render()
{
	bool flip = false;
#ifdef SHARED_TEXTURE_SUPPORT_ENABLED
	flip = hwaccel;
#endif

	if (texture) {
#ifdef __APPLE__
		gs_effect_t *effect =
			obs_get_base_effect((hwaccel) ? OBS_EFFECT_DEFAULT_RECT
						      : OBS_EFFECT_DEFAULT);
#else
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
#endif

		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(true);

		gs_eparam_t *const image =
			gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, texture);

		const uint32_t flip_flag = flip ? GS_FLIP_V : 0;

		while (gs_effect_loop(effect,
				      "DrawSrgbDecompressPremultiplied"))
			gs_draw_sprite(texture, flip_flag, 0, 0);

		gs_enable_framebuffer_srgb(previous);
	}

#if defined(_WIN32) && defined(SHARED_TEXTURE_SUPPORT_ENABLED)
	SignalBeginFrame();
#endif
}

static void ExecuteOnBrowser(BrowserFunc func, BrowserSource *bs)
{
	lock_guard<mutex> lock(browser_list_mutex);

	if (bs) {
		BrowserSource *bsw = reinterpret_cast<BrowserSource *>(bs);
		bsw->ExecuteOnBrowser(func, true);
	}
}
