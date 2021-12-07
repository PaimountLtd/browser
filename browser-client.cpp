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

#include "browser-client.hpp"
#include "base64/base64.hpp"
#include "json11/json11.hpp"
#include <iostream>
#include <vector>

using namespace json11;

BrowserClient::~BrowserClient()
{
	std::lock_guard<std::mutex> lock_client(browser_mtx);
	if (OnAudioStreamStarted_pending && OnAudioStreamStarted_reactor)
		OnAudioStreamStarted_reactor->Finish(Status::OK);

	if (OnAudioStreamStopped_pending && OnAudioStreamStopped_reactor)
		OnAudioStreamStopped_reactor->Finish(Status::OK);

	if (OnAudioStreamPacket_requested && OnAudioStreamPacket_reactor)
		OnAudioStreamPacket_reactor->Finish(Status::OK);

	if (SignalBeginFrame_requested && SignalBeginFrame_reactor)
		SignalBeginFrame_reactor->Finish(Status::OK);
}

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler()
{
	return this;
}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler()
{
	return this;
}

CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler()
{
	return this;
}

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler()
{
	return this;
}

CefRefPtr<CefContextMenuHandler> BrowserClient::GetContextMenuHandler()
{
	return this;
}

#if CHROME_VERSION_BUILD >= 3683
CefRefPtr<CefAudioHandler> BrowserClient::GetAudioHandler()
{
	return reroute_audio ? this : nullptr;
}
#endif

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
				  const CefString &, const CefString &,
				  WindowOpenDisposition, bool,
				  const CefPopupFeatures &, CefWindowInfo &,
				  CefRefPtr<CefClient> &, CefBrowserSettings &,
#if CHROME_VERSION_BUILD >= 3770
				  CefRefPtr<CefDictionaryValue> &,
#endif
				  bool *)
{
	/* block popups */
	return true;
}

void BrowserClient::OnBeforeContextMenu(CefRefPtr<CefBrowser>,
					CefRefPtr<CefFrame>,
					CefRefPtr<CefContextMenuParams>,
					CefRefPtr<CefMenuModel> model)
{
	/* remove all context menu contributions */
	model->Clear();
}

bool BrowserClient::OnProcessMessageReceived(
	CefRefPtr<CefBrowser> browser,
#if CHROME_VERSION_BUILD >= 3770
	CefRefPtr<CefFrame>,
#endif
	CefProcessId, CefRefPtr<CefProcessMessage> message)
{
 	const std::string &name = message->GetName();
 	Json json;

// 	if (!bs) {
// 		return false;
// 	}

// #if BROWSER_FRONTEND_API_SUPPORT_ENABLED
// 	if (name == "getCurrentScene") {
// 		OBSSource current_scene = obs_frontend_get_current_scene();
// 		obs_source_release(current_scene);

// 		if (!current_scene)
// 			return false;

// 		const char *name = obs_source_get_name(current_scene);
// 		if (!name)
// 			return false;

// 		json = Json::object{
// 			{"name", name},
// 			{"width", (int)obs_source_get_width(current_scene)},
// 			{"height", (int)obs_source_get_height(current_scene)}};

// 	}
// 	else if (name == "getStatus") {
// 		json = Json::object {
// 			{"recording", obs_frontend_recording_active()},
// 			{"streaming", obs_frontend_streaming_active()},
// 			{"recordingPaused", obs_frontend_recording_paused()},
// 			{"replaybuffer", obs_frontend_replay_buffer_active()},
// 			{"virtualcam", obs_frontend_virtualcam_active()}};

// 	} else if (name == "saveReplayBuffer") {
// 		obs_frontend_replay_buffer_save();
// 	} else {
// 		return false;
// 	}
// #endif

 	CefRefPtr<CefProcessMessage> msg =
 		CefProcessMessage::Create("executeCallback");

 	CefRefPtr<CefListValue> args = msg->GetArgumentList();
 	args->SetInt(0, message->GetArgumentList()->GetInt(0));
 	args->SetString(1, json.dump());

 	SendBrowserProcessMessage(browser, PID_RENDERER, msg);

	return true;
}
#if CHROME_VERSION_BUILD >= 3578
void BrowserClient::GetViewRect(
#else
bool BrowserClient::GetViewRect(
#endif
	CefRefPtr<CefBrowser>, CefRect &rect)
{
	if (!cefBrowser) {
#if CHROME_VERSION_BUILD >= 3578
		rect.Set(0, 0, 16, 16);
		return;
#else
		return false;
#endif
	}

	rect.Set(0, 0, width < 1 ? 1 : width,
		 height < 1 ? 1 : height);
#if CHROME_VERSION_BUILD >= 3578
	return;
#else
	return true;
#endif
}

void BrowserClient::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
			    const RectList &, const void *buffer, int width,
			    int height)
{
	if (type != PET_VIEW) {
		return;
	}

// #ifdef SHARED_TEXTURE_SUPPORT_ENABLED
// 	if (sharing_available) {
// 		return;
// 	}
// #endif

// 	if (!bs) {
// 		return;
// 	}

// 	if (bs->width != width || bs->height != height) {
// 		obs_enter_graphics();
// 		bs->DestroyTextures();
// 		obs_leave_graphics();
// 	}

// 	if (!bs->texture && width && height) {
// 		obs_enter_graphics();
// 		bs->texture = gs_texture_create(width, height, GS_BGRA, 1,
// 						(const uint8_t **)&buffer,
// 						GS_DYNAMIC);
// 		bs->width = width;
// 		bs->height = height;
// 		obs_leave_graphics();
// 	} else {
// 		obs_enter_graphics();
// 		gs_texture_set_image(bs->texture, (const uint8_t *)buffer,
// 				     width * 4, false);
// 		obs_leave_graphics();
// 	}
}

// #ifdef SHARED_TEXTURE_SUPPORT_ENABLED
void BrowserClient::OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType,
				       const RectList &, void *shared_handle)
{
	std::lock_guard<std::mutex> lock_client(browser_mtx);
	if (SignalBeginFrame_requested) {
		SignalBeginFrame_reply->set_shared_handle((int64_t) shared_handle);

		SignalBeginFrame_requested = false;
		SignalBeginFrame_reactor->Finish(Status::OK);
	}

	// if (shared_handle != last_handle) 
	// 	last_handle = shared_handle;

	// 	if (!bs) {
// 		return;
// 	}

// 	if (shared_handle != last_handle) {
// 		obs_enter_graphics();
// #if USE_TEXTURE_COPY
// 		gs_texture_destroy(texture);
// 		texture = nullptr;
// #endif
// 		gs_texture_destroy(bs->texture);
// 		bs->texture = nullptr;

// #if USE_TEXTURE_COPY
// 		texture = gs_texture_open_shared(
// 			(uint32_t)(uintptr_t)shared_handle);

// 		uint32_t cx = gs_texture_get_width(texture);
// 		uint32_t cy = gs_texture_get_height(texture);
// 		gs_color_format format = gs_texture_get_color_format(texture);

// 		bs->texture = gs_texture_create(cx, cy, format, 1, nullptr, 0);
// #else
// 		bs->texture = gs_texture_open_shared(
// 			(uint32_t)(uintptr_t)shared_handle);
// #endif
// 		obs_leave_graphics();

// 		last_handle = shared_handle;
// 	}

// #if USE_TEXTURE_COPY
// 	if (texture && bs->texture) {
// 		obs_enter_graphics();
// 		gs_copy_texture(bs->texture, texture);
// 		obs_leave_graphics();
// 	}
// #endif
}
// #endif

//#if CHROME_VERSION_BUILD >= 3683
//static speaker_layout GetSpeakerLayout(CefAudioHandler::ChannelLayout cefLayout)
//{
//	switch (cefLayout) {
//	case CEF_CHANNEL_LAYOUT_MONO:
//		return SPEAKERS_MONO; /**< Channels: MONO */
//	case CEF_CHANNEL_LAYOUT_STEREO:
//		return SPEAKERS_STEREO; /**< Channels: FL, FR */
//	case CEF_CHANNEL_LAYOUT_2POINT1:
//		return SPEAKERS_2POINT1; /**< Channels: FL, FR, LFE */
//	case CEF_CHANNEL_LAYOUT_2_2:
//	case CEF_CHANNEL_LAYOUT_QUAD:
//	case CEF_CHANNEL_LAYOUT_4_0:
//		return SPEAKERS_4POINT0; /**< Channels: FL, FR, FC, RC */
//	case CEF_CHANNEL_LAYOUT_4_1:
//		return SPEAKERS_4POINT1; /**< Channels: FL, FR, FC, LFE, RC */
//	case CEF_CHANNEL_LAYOUT_5_1:
//	case CEF_CHANNEL_LAYOUT_5_1_BACK:
//		return SPEAKERS_5POINT1; /**< Channels: FL, FR, FC, LFE, RL, RR */
//	case CEF_CHANNEL_LAYOUT_7_1:
//	case CEF_CHANNEL_LAYOUT_7_1_WIDE_BACK:
//	case CEF_CHANNEL_LAYOUT_7_1_WIDE:
//		return SPEAKERS_7POINT1; /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
//	default:
//		return SPEAKERS_UNKNOWN;
//	}
//}
//#endif

#if CHROME_VERSION_BUILD >= 4103
void BrowserClient::OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
					 const CefAudioParameters &params_,
					 int channels_)
{
	UNUSED_PARAMETER(browser);
	channels = channels_;
	channel_layout = (ChannelLayout)params_.channel_layout;
	sample_rate = params_.sample_rate;
	frames_per_buffer = params_.frames_per_buffer;
}

void BrowserClient::OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
					const float **data, int frames,
					int64_t pts)
{
	// UNUSED_PARAMETER(browser);
	// if (!bs) {
	// 	return;
	// }
	// struct obs_source_audio audio = {};
	// const uint8_t **pcm = (const uint8_t **)data;
	// speaker_layout speakers = GetSpeakerLayout(channel_layout);
	// int speaker_count = get_audio_channels(speakers);
	// for (int i = 0; i < speaker_count; i++)
	// 	audio.data[i] = pcm[i];
	// audio.samples_per_sec = sample_rate;
	// audio.frames = frames;
	// audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	// audio.speakers = speakers;
	// audio.timestamp = (uint64_t)pts * 1000000LLU;
	// obs_source_output_audio(bs->source, &audio);
}

void BrowserClient::OnAudioStreamStopped(CefRefPtr<CefBrowser> browser)
{
	UNUSED_PARAMETER(browser);
	if (!bs) {
		return;
	}
}

void BrowserClient::OnAudioStreamError(CefRefPtr<CefBrowser> browser,
				       const CefString &message)
{
	UNUSED_PARAMETER(browser);
	UNUSED_PARAMETER(message);
	if (!bs) {
		return;
	}
}

static CefAudioHandler::ChannelLayout Convert2CEFSpeakerLayout(int channels)
{
	switch (channels) {
	case 1:
		return CEF_CHANNEL_LAYOUT_MONO;
	case 2:
		return CEF_CHANNEL_LAYOUT_STEREO;
	case 3:
		return CEF_CHANNEL_LAYOUT_2_1;
	case 4:
		return CEF_CHANNEL_LAYOUT_4_0;
	case 5:
		return CEF_CHANNEL_LAYOUT_4_1;
	case 6:
		return CEF_CHANNEL_LAYOUT_5_1;
	case 8:
		return CEF_CHANNEL_LAYOUT_7_1;
	default:
		return CEF_CHANNEL_LAYOUT_UNSUPPORTED;
	}
}

bool BrowserClient::GetAudioParameters(CefRefPtr<CefBrowser> browser,
				       CefAudioParameters &params)
{
	// UNUSED_PARAMETER(browser);
	// int channels = (int)audio_output_get_channels(obs_get_audio());
	// params.channel_layout = Convert2CEFSpeakerLayout(channels);
	// params.sample_rate = (int)audio_output_get_sample_rate(obs_get_audio());
	// params.frames_per_buffer = kFramesPerBuffer;
	return true;
}
#elif CHROME_VERSION_BUILD >= 3683 && CHROME_VERSION_BUILD < 4103
void BrowserClient::OnAudioStreamStarted(CefRefPtr<CefBrowser> browser, int id,
					 int, ChannelLayout channel_layout,
					 int sample_rate, int)
{
	std::lock_guard<std::mutex> lock_client(browser_mtx);
	if (!OnAudioStreamStarted_reactor || !OnAudioStreamStarted_reply)
		return;

	OnAudioStreamStarted_reply->set_id((int32_t)id);
	OnAudioStreamStarted_reply->set_channel_layout((int32_t)channel_layout);
	OnAudioStreamStarted_reply->set_sample_rate((int32_t)sample_rate);
	OnAudioStreamStarted_reactor->Finish(Status::OK);
	OnAudioStreamStarted_pending = false;
}

void BrowserClient::OnAudioStreamPacket(CefRefPtr<CefBrowser> browser, int id,
					const float **data, int frames,
					int64_t pts)
{
	std::lock_guard<std::mutex> lock_client(browser_mtx);
	if (OnAudioStreamPacket_requested) {
		const char **pcm = (const char **)data;
		size_t size = frames * sizeof(float);

		for (int i = 0; i < channels; i++) {
			// if (pcm[i][0] == 0) {
			// 	OnAudioStreamPacket_reply->add_data("");
			// 	continue;
			// }
			std::string my_buffer2 (pcm[i], size);
			OnAudioStreamPacket_reply->add_data(my_buffer2);
		}
		OnAudioStreamPacket_reply->set_frames(frames);
		OnAudioStreamPacket_reply->set_pts(pts);


		OnAudioStreamPacket_requested = false;
		OnAudioStreamPacket_reactor->Finish(Status::OK);
	} else {
		std::cout << "packet dropped" << std::endl;
	}
}

void BrowserClient::OnAudioStreamStopped(CefRefPtr<CefBrowser> browser, int id)
{
	if (!OnAudioStreamStopped_reactor || !OnAudioStreamStopped_reply)
		return;

	OnAudioStreamStopped_reply->set_id(id);
	OnAudioStreamStopped_reactor->Finish(Status::OK);
	OnAudioStreamStopped_pending = false;
}
#endif

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
			      int)
{
	if (frame->IsMain() && css.length()) {
		std::string uriEncodedCSS =
			CefURIEncode(css, false).ToString();

		std::string script;
		script += "const obsCSS = document.createElement('style');";
		script += "obsCSS.innerHTML = decodeURIComponent(\"" +
			  uriEncodedCSS + "\");";
		script += "document.querySelector('head').appendChild(obsCSS);";

		frame->ExecuteJavaScript(script, "", 0);
	}
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser>,
#if CHROME_VERSION_BUILD >= 3282
				     cef_log_severity_t level,
#endif
				     const CefString &message,
				     const CefString &source, int line)
{
	// int errorLevel = LOG_INFO;
	// switch (level) {
	// case LOGSEVERITY_ERROR:
	// 	errorLevel = LOG_WARNING;
	// 	break;
	// case LOGSEVERITY_FATAL:
	// 	errorLevel = LOG_ERROR;
	// 	break;
	// default:
	// 	return false;
	// }

	// blog(errorLevel, "obs-browser: %s (source: %s:%d)",
	//      message.ToString().c_str(), source.ToString().c_str(), line);
	return false;
}
