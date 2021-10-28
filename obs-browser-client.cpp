  #include "obs-browser-client.hpp"

 void BrowserGRPCClient::IntializeBrowserCEF() {
	uint32_t obs_version = obs_get_version();
	std::string obs_locale = obs_get_locale();
	std::string bin_path = obs_get_module_binary_path(obs_current_module());
	bin_path = bin_path.substr(0, bin_path.find_last_of('/') + 1);
	bin_path += "//obs-browser-page";
	bin_path += ".exe";
	char *abs_path = os_get_abs_path_ptr(bin_path.c_str());

	BPtr<char> conf_path = obs_module_config_path("");
	os_mkdir(conf_path);
	BPtr<char> conf_path_abs = os_get_abs_path_ptr(conf_path);

#ifdef SHARED_TEXTURE_SUPPORT_ENABLED
	if (hwaccel) {
		obs_enter_graphics();
		hwaccel = gs_shared_texture_available();
		obs_leave_graphics();
	}
#endif
    // Data we are sending to the server.
    Request request;
    request.set_obs_version(obs_version);
    request.set_obs_locale(obs_locale);
    request.set_obs_conf_path(std::string(conf_path_abs.Get()));
	  request.set_obs_browser_subprocess_path(std::string(abs_path));
    request.set_hwaccel(hwaccel);

    // Container for the data we expect from the server.
    NoReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->IntializeBrowserCEF(&context, request, &reply);

    // Act upon its status.
    // if (status.ok()) {
    //   return reply.message();
    // } else {
    //   std::cout << status.error_code() << ": " << status.error_message()
    //             << std::endl;
    //   return "RPC failed";
    // }
  }

void BrowserGRPCClient::CreateBrowserSource(
    uint64_t sourceId, bool hwaccel, bool reroute_audio,
    int width, int height, int fps, bool fps_custom,
    int video_fps, std::string url) {
  CreateRequest request;
  request.set_id(sourceId);
  request.set_hwaccel(hwaccel);
  request.set_reroute_audio(reroute_audio);
  request.set_width(width);
  request.set_height(height);
  request.set_fps(fps);
  request.set_fps_custom(fps_custom);
  request.set_video_fps(video_fps);
  request.set_url(url);

  NoReply reply;
  ClientContext context;
  stub_->CreateBrowserSource(&context, request, &reply);
}

void BrowserGRPCClient::SetShowing(uint64_t sourceId, bool showing) {
  SetShowingRequest request;
  request.set_showing(showing);
  request.set_id(sourceId);

  NoReply reply;
  ClientContext context;
  stub_->SetShowing(&context, request, &reply);
}

void BrowserGRPCClient::SetActive(uint64_t sourceId, bool active) {
  SetActiveRequest request;
  request.set_active(active);
  request.set_id(sourceId);

  NoReply reply;
  ClientContext context;
  stub_->SetActive(&context, request, &reply);
}

void BrowserGRPCClient::Refresh(uint64_t sourceId) {
  IdRequest request;
  request.set_id(sourceId);
  NoReply reply;
  ClientContext context;
  stub_->Refresh(&context, request, &reply);
}

void* BrowserGRPCClient::SignalBeginFrame(uint64_t sourceId) {
  IdRequest request;
  request.set_id(sourceId);
  SignalBeginFrameResponse reply;
  ClientContext context;
  stub_->SignalBeginFrame(&context, request, &reply);
  return (void*)reply.shared_handle();
}

void BrowserGRPCClient::DestroyBrowserSource(uint64_t sourceId, bool async) {
  DestroyBrowserSourceRequest request;
  request.set_id(sourceId);
  request.set_async(async);
  NoReply reply;
  ClientContext context;
  stub_->DestroyBrowserSource(&context, request, &reply);
}

void BrowserGRPCClient::ShutdownBrowserCEF() {
  NoArgs request;
  NoReply reply;
  ClientContext context;
  stub_->ShutdownBrowserCEF(&context, request, &reply);
}

void BrowserGRPCClient::SendMouseClick(
  uint64_t sourceId, uint32_t modifiers, int32_t x, int32_t y,
  int32_t type, bool mouse_up, uint32_t click_count) {
  MouseEventRequest request;
  request.set_id(sourceId);
  request.set_modifiers(modifiers);
  request.set_x(x);
  request.set_y(y);
  request.set_type(type);
  request.set_mouse_up(mouse_up);
  request.set_click_count(click_count);
  NoReply reply;
  ClientContext context;
  stub_->SendMouseClick(&context, request, &reply);
}

void BrowserGRPCClient::SendMouseMove(
  uint64_t sourceId, uint32_t modifiers,
  int32_t x, int32_t y, bool mouse_leave) {
  MouseEventRequest request;
  request.set_id(sourceId);
  request.set_modifiers(modifiers);
  request.set_x(x);
  request.set_y(y);
  request.set_mouse_leave(mouse_leave);
  NoReply reply;
  ClientContext context;
  stub_->SendMouseMove(&context, request, &reply);
}

void BrowserGRPCClient::SendMouseWheel(
  uint64_t sourceId, uint32_t modifiers,
  int32_t x, int32_t y, int32_t x_delta, int32_t y_delta) {
  MouseEventRequest request;
  request.set_id(sourceId);
  request.set_modifiers(modifiers);
  request.set_x(x);
  request.set_y(y);
  request.set_x_delta(x_delta);
  request.set_y_delta(y_delta);
  NoReply reply;
  ClientContext context;
  stub_->SendMouseWheel(&context, request, &reply);
}

void BrowserGRPCClient::SendFocus(uint64_t sourceId, bool focus) {
  MouseEventRequest request;
  request.set_id(sourceId);
  request.set_focus(focus);
  NoReply reply;
  ClientContext context;
  stub_->SendFocus(&context, request, &reply);
}

void BrowserGRPCClient::SendKeyClick(
  uint64_t sourceId, std::string text,
  uint32_t native_vkey, uint32_t modifiers,
  uint32_t native_scancode, bool key_up) {
  MouseEventRequest request;
  request.set_id(sourceId);
  request.set_text(text);
  request.set_native_vkey(native_vkey);
  request.set_modifiers(modifiers);
  request.set_native_scancode(native_scancode);
  request.set_key_up(key_up);
  NoReply reply;
  ClientContext context;
  stub_->SendKeyClick(&context, request, &reply);
}