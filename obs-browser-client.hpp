#pragma once

#ifndef __OBS_BROWSER_CLIENT_H__
#define __OBS_BROWSER_CLIENT_H__

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
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
using helloworld::BrowserServer;
using helloworld::NoReply;
using helloworld::Request;
using helloworld::CreateRequest;
using helloworld::SetShowingRequest;
using helloworld::SetActiveRequest;
using helloworld::NoArgs;
using helloworld::SignalBeginFrameReply;
using helloworld::IdRequest;
using helloworld::DestroyBrowserSourceRequest;
using helloworld::MouseEventRequest;
using helloworld::OnAudioStreamStartedReply;
using helloworld::OnAudioStreamPacketReply;
using helloworld::OnAudioStreamPacketRequest;
using helloworld::OnAudioStreamStoppedReply;
using helloworld::RequestPaintReply;

extern bool hwaccel;

struct BrowserSource;

class BrowserGRPCClient {
public:
  std::mutex mtx;
  bool active;
	BrowserGRPCClient(std::shared_ptr<Channel> channel)
      : stub_(BrowserServer::NewStub(channel)) {}

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