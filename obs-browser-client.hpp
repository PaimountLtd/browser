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
using helloworld::SignalBeginFrameResponse;

extern bool hwaccel;

class BrowserGRPCClient {
public:
	BrowserGRPCClient(std::shared_ptr<Channel> channel)
      : stub_(BrowserServer::NewStub(channel)) {}

    void IntializeBrowserCEF();
    void CreateBrowserSource(
        bool hwaccel, bool reroute_audio, int width,
        int height, int fps, bool fps_custom, int video_fps,
        std::string url
    );
    void SetShowing(bool showing);
    void SetActive(bool active);
    void Refresh();
    void* SignalBeginFrame();

private:
  std::unique_ptr<BrowserServer::Stub> stub_;
};