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
	// Not implemented
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

	std::lock_guard<std::mutex> lock_client(browser_mtx);
	if (OnPaint_requested) {
		const uint8_t *frameData = (const uint8_t *)buffer;
		if (frameData[0]) {
			std::string data((const char *)buffer, width * height * 4);
			OnPaint_reply->add_data(data);
			OnPaint_reply->set_width(width);
			OnPaint_reply->set_height(height);
			OnPaint_requested = false;
			OnPaint_reactor->Finish(Status::OK);
		}

	} else {
		std::cout << "frame dropped" << std::endl;
	}
}

void BrowserClient::OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType,
				       const RectList &, void *shared_handle)
{
	std::lock_guard<std::mutex> lock_client(browser_mtx);
	if (SignalBeginFrame_requested) {
		SignalBeginFrame_reply->set_shared_handle((int64_t) shared_handle);

		SignalBeginFrame_requested = false;
		SignalBeginFrame_reactor->Finish(Status::OK);
	}
}

#if CHROME_VERSION_BUILD >= 4103
void BrowserClient::OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
					 const CefAudioParameters &params_,
					 int channels_)
{
	// UNUSED_PARAMETER(browser);
	// channels = channels_;
	// channel_layout = (ChannelLayout)params_.channel_layout;
	// sample_rate = params_.sample_rate;
	// frames_per_buffer = params_.frames_per_buffer;
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
	// UNUSED_PARAMETER(browser);
	// if (!bs) {
	// 	return;
	// }
}

void BrowserClient::OnAudioStreamError(CefRefPtr<CefBrowser> browser,
				       const CefString &message)
{
	// UNUSED_PARAMETER(browser);
	// UNUSED_PARAMETER(message);
	// if (!bs) {
	// 	return;
	// }
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
	// NOT IMPLEMENTED
	return false;
}
