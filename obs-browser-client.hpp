#pragma once

#ifndef __OBS_BROWSER_CLIENT_H__
#define __OBS_BROWSER_CLIENT_H__

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "obs_browser_api.grpc.pb.h"
#include <util/platform.h>
#include <util/util.hpp>
#include <util/dstr.hpp>
#include <obs-module.h>
#include <obs.hpp>
#include <functional>

#include "obs-browser-source.hpp"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using obs_browser_api::BrowserServer;
using obs_browser_api::NoReply;
using obs_browser_api::RegisterPIDRequest;
using obs_browser_api::Request;
using obs_browser_api::CreateRequest;
using obs_browser_api::SetShowingRequest;
using obs_browser_api::SetActiveRequest;
using obs_browser_api::NoArgs;
using obs_browser_api::SignalBeginFrameReply;
using obs_browser_api::IdRequest;
using obs_browser_api::DestroyBrowserSourceRequest;
using obs_browser_api::MouseEventRequest;
using obs_browser_api::OnAudioStreamStartedReply;
using obs_browser_api::OnAudioStreamPacketReply;
using obs_browser_api::OnAudioStreamPacketRequest;
using obs_browser_api::OnAudioStreamStoppedReply;
using obs_browser_api::RequestPaintReply;

extern bool hwaccel;

struct BrowserSource;

class BrowserGRPCClient {
public:
  std::mutex mtx;
  bool active;
	BrowserGRPCClient(std::shared_ptr<Channel> channel)
      : stub_(BrowserServer::NewStub(channel)) {}

  void RegisterPID(uint32_t PID);
  void IntializeBrowserCEF();
  void CreateBrowserSource(
      uint64_t sourceId, bool hwaccel, bool reroute_audio, int width,
      int height, int fps, bool fps_custom, int video_fps,
      std::string url, std::string css
  );
  void SetShowing(uint64_t sourceId, bool showing);
  void SetActive(uint64_t sourceId, bool active);
  void Refresh(uint64_t sourceId);
#if defined(_WIN32) && defined(SHARED_TEXTURE_SUPPORT_ENABLED)
  void SignalBeginFrame(BrowserSource* bs);
#endif
  void RequestPaint(BrowserSource* bs);
  void DestroyBrowserSource(uint64_t sourceId, bool async);
  void ShutdownBrowserCEF();
  
  void SendMouseClick(
    uint64_t sourceId,
    uint32_t modifiers,
    int32_t x,
    int32_t y,
    int32_t type,
    bool mouse_up,
    uint32_t click_count);
  void SendMouseMove(
    uint64_t sourceId,
    uint32_t modifiers,
    int32_t x,
    int32_t y,
    bool mouse_leave);
  void SendMouseWheel(
    uint64_t sourceId,
    uint32_t modifiers,
    int32_t x,
    int32_t y,
    int32_t x_delta,
    int32_t y_delta);
  void SendFocus(uint64_t sourceId, bool focus);
  void SendKeyClick(
    uint64_t sourceId,
    std::string text,
    uint32_t native_vkey,
    uint32_t modifiers,
    uint32_t native_scancode,
    bool key_up
  );

  void OnAudioStreamStarted(BrowserSource* bs);
  void OnAudioStreamPacket(BrowserSource* bs);
  void OnAudioStreamStopped(BrowserSource* bs);

private:
  std::unique_ptr<BrowserServer::Stub> stub_;
};
#endif
