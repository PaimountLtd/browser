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

#pragma once

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "obs_browser_api.grpc.pb.h"
#endif

#include "cef-headers.hpp"
#include <mutex>
#include "browser-config.h"

using grpc::ServerUnaryReactor;
using grpc::Status;
using obs_browser_api::OnAudioStreamStartedReply;
using obs_browser_api::OnAudioStreamPacketReply;
using obs_browser_api::OnAudioStreamStoppedReply;
using obs_browser_api::SignalBeginFrameReply;
using obs_browser_api::RequestPaintReply;

class BrowserClient : public CefClient,
		      public CefDisplayHandler,
		      public CefLifeSpanHandler,
		      public CefContextMenuHandler,
		      public CefRenderHandler,
#if CHROME_VERSION_BUILD >= 3683
		      public CefAudioHandler,
#endif
		      public CefLoadHandler {

	bool sharing_available = false;

public:
	CefRect popupRect;
	CefRect originalPopupRect;

	uint32_t width;
	uint32_t height;
	bool reroute_audio;
	CefRefPtr<CefBrowser> cefBrowser;
	std::mutex browser_mtx;
	int32_t channels;

	ServerUnaryReactor* OnAudioStreamStarted_reactor = nullptr;
	OnAudioStreamStartedReply* OnAudioStreamStarted_reply = nullptr;
	bool OnAudioStreamStarted_pending = false;

	ServerUnaryReactor* OnAudioStreamPacket_reactor = nullptr;
	OnAudioStreamPacketReply* OnAudioStreamPacket_reply = nullptr;
	bool OnAudioStreamPacket_requested = false;

	ServerUnaryReactor* OnAudioStreamStopped_reactor = nullptr;
	OnAudioStreamStoppedReply* OnAudioStreamStopped_reply = nullptr;
	bool OnAudioStreamStopped_pending = false;

	ServerUnaryReactor* SignalBeginFrame_reactor = nullptr;
	SignalBeginFrameReply* SignalBeginFrame_reply = nullptr;
	bool SignalBeginFrame_requested = false;

	ServerUnaryReactor* OnPaint_reactor = nullptr;
	RequestPaintReply* OnPaint_reply = nullptr;
	bool OnPaint_requested = false;

	int sample_rate;
	ChannelLayout channel_layout;
	int frames_per_buffer;
	std::string css;

	inline BrowserClient(bool sharing_avail,
			     bool reroute_audio_)
		: sharing_available(sharing_avail),
		  reroute_audio(reroute_audio_)
	{
	}

	virtual ~BrowserClient();

	/* CefClient */
	virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override;
	virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override;
	virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
	virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
	virtual CefRefPtr<CefContextMenuHandler>
	GetContextMenuHandler() override;
#if CHROME_VERSION_BUILD >= 3683
	virtual CefRefPtr<CefAudioHandler> GetAudioHandler() override;
#endif

	virtual bool
	OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
#if CHROME_VERSION_BUILD >= 3770
				 CefRefPtr<CefFrame> frame,
#endif
				 CefProcessId source_process,
				 CefRefPtr<CefProcessMessage> message) override;

	/* CefDisplayHandler */
	virtual bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
#if CHROME_VERSION_BUILD >= 3282
				      cef_log_severity_t level,
#endif
				      const CefString &message,
				      const CefString &source,
				      int line) override;

	/* CefLifeSpanHandler */
	virtual bool
	OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
		      const CefString &target_url,
		      const CefString &target_frame_name,
		      WindowOpenDisposition target_disposition,
		      bool user_gesture, const CefPopupFeatures &popupFeatures,
		      CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client,
		      CefBrowserSettings &settings,
#if CHROME_VERSION_BUILD >= 3770
		      CefRefPtr<CefDictionaryValue> &extra_info,
#endif
		      bool *no_javascript_access) override;

	/* CefContextMenuHandler */
	virtual void
	OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
			    CefRefPtr<CefFrame> frame,
			    CefRefPtr<CefContextMenuParams> params,
			    CefRefPtr<CefMenuModel> model) override;

	/* CefRenderHandler */
#if CHROME_VERSION_BUILD >= 3578
	virtual void GetViewRect(
#else
	virtual bool GetViewRect(
#endif
		CefRefPtr<CefBrowser> browser, CefRect &rect) override;
	virtual void OnPaint(CefRefPtr<CefBrowser> browser,
			     PaintElementType type, const RectList &dirtyRects,
			     const void *buffer, int width,
			     int height) override;
	virtual void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
					PaintElementType type,
					const RectList &dirtyRects,
					void *shared_handle) override;
#if CHROME_VERSION_BUILD >= 4103
	virtual void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
					 const float **data, int frames,
					 int64_t pts) override;

	virtual void
	OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override;

	virtual void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
					  const CefAudioParameters &params,
					  int channels) override;
	virtual void OnAudioStreamError(CefRefPtr<CefBrowser> browser,
					const CefString &message) override;
	const int kFramesPerBuffer = 1024;
	virtual bool GetAudioParameters(CefRefPtr<CefBrowser> browser,
					CefAudioParameters &params) override;
#elif CHROME_VERSION_BUILD >= 3683
	virtual void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
					 int audio_stream_id,
					 const float **data, int frames,
					 int64_t pts) override;

	virtual void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser,
					  int audio_stream_id);

	virtual void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
					  int audio_stream_id, int channels,
					  ChannelLayout channel_layout,
					  int sample_rate,
					  int frames_per_buffer) override;
#endif
	/* CefLoadHandler */
	virtual void OnLoadEnd(CefRefPtr<CefBrowser> browser,
			       CefRefPtr<CefFrame> frame,
			       int httpStatusCode) override;

	IMPLEMENT_REFCOUNTING(BrowserClient);
};
