
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <map>
#include <condition_variable>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

#include "cef-headers.hpp"
#include "browser-app.hpp"
#include "browser-client.hpp"
#include "browser-scheme.hpp"
#include "wide-string.hpp"

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerUnaryReactor;
using helloworld::BrowserServer;
using helloworld::NoReply;
using helloworld::Request;
using helloworld::CreateRequest;
using helloworld::SetShowingRequest;
using helloworld::SetActiveRequest;
using helloworld::NoArgs;
using helloworld::SignalBeginFrameResponse;
using helloworld::IdRequest;
using helloworld::DestroyBrowserSourceRequest;
using helloworld::MouseEventRequest;
using helloworld::OnAudioStreamStartedReply;
using helloworld::OnAudioStreamPacketRequest;

std::unique_ptr<Server> server;
static CefRefPtr<BrowserApp> app;
static std::thread manager_thread;
std::map<uint64_t, CefRefPtr<BrowserClient>> browserClients;
std::mutex browser_clients_mtx;
std::thread* shutdown_thread;

static void BrowserShutdown(void)
{
	CefShutdown();
	app = nullptr;
}

static void ShutdownServer(void)
{
	server->Shutdown();
}

static void BrowserInit(
	uint32_t obs_version, std::string obs_locale,
	std::string obs_conf_path, std::string obs_browser_subprocess_path,
	bool hwaccel)
{
	// Enable High-DPI support on Windows 7 or newer.
	CefEnableHighDPISupport();

	CefMainArgs args;

	CefSettings settings;
	settings.log_severity = LOGSEVERITY_VERBOSE;
	settings.windowless_rendering_enabled = true;
	settings.no_sandbox = true;

	uint32_t obs_ver = obs_version;
	uint32_t obs_maj = obs_ver >> 24;
	uint32_t obs_min = (obs_ver >> 16) & 0xFF;
	uint32_t obs_pat = obs_ver & 0xFFFF;

	/* This allows servers the ability to determine that browser panels and
		* browser sources are coming from OBS. */
	std::stringstream prod_ver;
	prod_ver << "Chrome/";
	prod_ver << std::to_string(CHROME_VERSION_MAJOR) << "."
			<< std::to_string(CHROME_VERSION_MINOR) << "."
			<< std::to_string(CHROME_VERSION_BUILD) << "."
			<< std::to_string(CHROME_VERSION_PATCH);
	prod_ver << " OBS/";
	prod_ver << std::to_string(obs_maj) << "." << std::to_string(obs_min)
			<< "." << std::to_string(obs_pat);

	CefString(&settings.product_version) = prod_ver.str();

	std::string accepted_languages;
	if (obs_locale != "en-US") {
		accepted_languages = obs_locale;
		accepted_languages += ",";
		accepted_languages += "en-US,en";
	} else {
		accepted_languages = "en-US,en";
	}

	CefString(&settings.locale) = obs_locale;
	CefString(&settings.accept_language_list) = accepted_languages;
	CefString(&settings.cache_path) = obs_conf_path;

	CefString(&settings.browser_subprocess_path) =
		obs_browser_subprocess_path;

	app = new BrowserApp(hwaccel);

	// CEF applications have multiple sub-processes (render, plugin, GPU, etc)
	// that share the same executable. This function checks the command-line and,
	// if this is a sub-process, executes the appropriate logic.
	int exit_code = CefExecuteProcess(args, app, nullptr);
	if (exit_code >= 0) {
		// The sub-process has completed so return here.
		// return Status::Ok;
	}

	uintptr_t zeroed_memory_lol[32] = {};
	CefInitialize(args, settings, app, zeroed_memory_lol);

#if !ENABLE_LOCAL_FILE_URL_SCHEME
		/* Register http://absolute/ scheme handler for older
		* CEF builds which do not support file:// URLs */
		CefRegisterSchemeHandlerFactory("http", "absolute",
						new BrowserSchemeHandlerFactory());
#endif
}

static void BrowserManagerThread(
	uint32_t obs_version, std::string obs_locale,
	std::string obs_conf_path, std::string obs_browser_subprocess_path,
	bool hwaccel)
{
	BrowserInit(
		obs_version,
		obs_locale,
		obs_conf_path,
		obs_browser_subprocess_path,
		hwaccel
	);
	CefRunMessageLoop();
	BrowserShutdown();
}

class BrowserTask : public CefTask {
public:
	std::function<void()> task;

	inline BrowserTask(std::function<void()> task_) : task(task_) {}
	virtual void Execute() override
	{
		task();
	}

	IMPLEMENT_REFCOUNTING(BrowserTask);
};

bool QueueCEFTask(std::function<void()> task)
{
	return CefPostTask(TID_UI,
			   CefRefPtr<BrowserTask>(new BrowserTask(task)));
}

void ExecuteOnBrowser(BrowserFunc func, CefRefPtr<CefBrowser> cefBrowser, bool async)
{
	if (!async) {
		std::condition_variable cv;
		std::mutex mtx;
		if (!!cefBrowser)
			QueueCEFTask([&]() {
				func(cefBrowser); 
				cv.notify_one();
			});
		std::unique_lock<std::mutex> lk(mtx);
		cv.wait(lk);
	} else {
		if (!!cefBrowser)
			QueueCEFTask([=]() { func(cefBrowser); });
	}
}

// Logic and data behind the server's behavior.
class BrowserServerServiceImpl final : public BrowserServer::CallbackService {
	ServerUnaryReactor* IntializeBrowserCEF(
		CallbackServerContext* context, const Request* request,
		NoReply* reply) override {
		auto binded_fn = std::bind(BrowserManagerThread,
			request->obs_version(),
			request->obs_locale(),
			request->obs_conf_path(),
			request->obs_browser_subprocess_path(),
			request->hwaccel()
		);
		manager_thread = std::thread(binded_fn);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* CreateBrowserSource(
		CallbackServerContext* context, const CreateRequest* request,
		NoReply* reply) override {
		uint64_t id = request->id();
		bool hwaccel = request->hwaccel();
		bool reroute_audio = request->reroute_audio();
		uint32_t width = request->width();
		uint32_t height = request->height();
		uint32_t fps = request->fps();
		bool fps_custom = request->fps_custom();
		uint32_t video_fps = request->video_fps();
		std::string url = request->url();
		QueueCEFTask([this, id, hwaccel, reroute_audio,
			width, height, fps, fps_custom,
			video_fps, url]() {
			std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);

			browserClients.insert_or_assign(
				id,
				new BrowserClient(hwaccel, reroute_audio)
			);
			std::lock_guard<std::mutex> lock_client(browserClients[id]->browser_mtx);
			CefWindowInfo windowInfo;
			windowInfo.width = width;
			windowInfo.height = height;
			windowInfo.windowless_rendering_enabled = true;
			windowInfo.shared_texture_enabled = hwaccel;

			browserClients[id]->width = width;
			browserClients[id]->height = height;
			browserClients[id]->reroute_audio = reroute_audio;

			CefBrowserSettings cefBrowserSettings;
			if (!fps_custom) {
				windowInfo.external_begin_frame_enabled = true;
				cefBrowserSettings.windowless_frame_rate = 0;
			} else {
				cefBrowserSettings.windowless_frame_rate = fps;
			}

			cefBrowserSettings.default_font_size = 16;
			cefBrowserSettings.default_fixed_font_size = 16;

			browserClients[id]->cefBrowser = CefBrowserHost::CreateBrowserSync(
				windowInfo, browserClients[id], url, cefBrowserSettings,
				CefRefPtr<CefDictionaryValue>(),
				nullptr
			);
		
			if (!browserClients[id]->cefBrowser) return;

			if (reroute_audio)
				browserClients[id]->cefBrowser->GetHost()->SetAudioMuted(true);

			CefRefPtr<CefProcessMessage> msg =
				CefProcessMessage::Create("Visibility");
			CefRefPtr<CefListValue> args = msg->GetArgumentList();
			args->SetBool(0, true);
			SendBrowserProcessMessage(browserClients[id]->cefBrowser, PID_RENDERER, msg);

			CefRefPtr<CefProcessMessage> msg2 =
				CefProcessMessage::Create("Active");
			CefRefPtr<CefListValue> args2 = msg2->GetArgumentList();
			args2->SetBool(0, true);
			SendBrowserProcessMessage(browserClients[id]->cefBrowser, PID_RENDERER, msg2);
		});
		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SetShowing(
		CallbackServerContext* context, const SetShowingRequest* request,
		NoReply* reply) override {
		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);
		ExecuteOnBrowser(
			[=](CefRefPtr<CefBrowser> cefBrowser) {
				CefRefPtr<CefProcessMessage> msg =
					CefProcessMessage::Create("Visibility");
				CefRefPtr<CefListValue> args =
					msg->GetArgumentList();
				args->SetBool(0, request->showing());
				SendBrowserProcessMessage(cefBrowser,
							  PID_RENDERER, msg);
			}, browserClients[request->id()]->cefBrowser, true);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SetActive(
		CallbackServerContext* context, const SetActiveRequest* request,
		NoReply* reply) override {
		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);
		ExecuteOnBrowser(
			[=](CefRefPtr<CefBrowser> cefBrowser) {
				CefRefPtr<CefProcessMessage> msg =
					CefProcessMessage::Create("Active");
				CefRefPtr<CefListValue> args = msg->GetArgumentList();
				args->SetBool(0, request->active());
				SendBrowserProcessMessage(cefBrowser, PID_RENDERER,
							msg);
			}, browserClients[request->id()]->cefBrowser, true);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* Refresh(
		CallbackServerContext* context, const IdRequest* request,
		NoReply* reply) override {
		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);
		ExecuteOnBrowser(
			[](CefRefPtr<CefBrowser> cefBrowser) {
				cefBrowser->ReloadIgnoreCache();
			}, browserClients[request->id()]->cefBrowser, true);


		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SignalBeginFrame(
		CallbackServerContext* context,
		const IdRequest* request,
		SignalBeginFrameResponse* reply) override {
		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);
		// if (reset_frame) {
			ExecuteOnBrowser(
				[](CefRefPtr<CefBrowser> cefBrowser) {
					cefBrowser->GetHost()->SendExternalBeginFrame();
				}, browserClients[request->id()]->cefBrowser, true);

			// reset_frame = false;
		// }
			reply->set_shared_handle(
				(int64_t) browserClients[request->id()]->last_handle
			);


		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* DestroyBrowserSource(
		CallbackServerContext* context,
		const DestroyBrowserSourceRequest* request,
		NoReply* reply) override {
		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		ExecuteOnBrowser(
			[](CefRefPtr<CefBrowser> cefBrowser) {
				/*
			* This stops rendering
			* http://magpcss.org/ceforum/viewtopic.php?f=6&t=12079
			* https://bitbucket.org/chromiumembedded/cef/issues/1363/washidden-api-got-broken-on-branch-2062)
			*/
				cefBrowser->GetHost()->WasHidden(true);
				cefBrowser->GetHost()->CloseBrowser(true);
			}, browserClients[request->id()]->cefBrowser, request->async());

		browserClients[request->id()]->cefBrowser = nullptr;
		browserClients.erase(browserClients.find(request->id()));

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* ShutdownBrowserCEF(
		CallbackServerContext* context,
		const NoArgs* request,
		NoReply* reply) override {
		if (manager_thread.joinable()) {
			while (!QueueCEFTask([]() { CefQuitMessageLoop(); }))
				Sleep(5);

			manager_thread.join();
		}
		
		shutdown_thread = new std::thread(ShutdownServer);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SendMouseClick(
		CallbackServerContext* context,
		const MouseEventRequest* request,
		NoReply* reply) override {

		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);

		uint32_t modifiers = request->modifiers();
		int32_t x = request->x();
		int32_t y = request->y();
		int32_t type = request->type();
		bool mouse_up = request->mouse_up();
		uint32_t click_count = request->click_count();

		ExecuteOnBrowser(
			[=](CefRefPtr<CefBrowser> cefBrowser) {
				CefMouseEvent e;
				e.modifiers = modifiers;
				e.x = x;
				e.y = y;
				CefBrowserHost::MouseButtonType buttonType =
					(CefBrowserHost::MouseButtonType)type;
				cefBrowser->GetHost()->SendMouseClickEvent(
					e, buttonType, mouse_up, click_count);
			}, browserClients[request->id()]->cefBrowser, true);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SendMouseMove(
		CallbackServerContext* context,
		const MouseEventRequest* request,
		NoReply* reply) override {

		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);

		uint32_t modifiers = request->modifiers();
		int32_t x = request->x();
		int32_t y = request->y();
		bool mouse_leave = request->mouse_leave();

		ExecuteOnBrowser(
			[=](CefRefPtr<CefBrowser> cefBrowser) {
				CefMouseEvent e;
				e.modifiers = modifiers;
				e.x = x;
				e.y = y;
				cefBrowser->GetHost()->SendMouseMoveEvent(e,
									mouse_leave);
			}, browserClients[request->id()]->cefBrowser, true);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SendMouseWheel(
		CallbackServerContext* context,
		const MouseEventRequest* request,
		NoReply* reply) override {

		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);

		uint32_t modifiers = request->modifiers();
		int32_t x = request->x();
		int32_t y = request->y();
		int32_t x_delta = request->x_delta();
		int32_t y_delta = request->y_delta();

		ExecuteOnBrowser(
			[=](CefRefPtr<CefBrowser> cefBrowser) {
				CefMouseEvent e;
				e.modifiers = modifiers;
				e.x = x;
				e.y = y;
				cefBrowser->GetHost()->SendMouseWheelEvent(e, x_delta,
									y_delta);
			}, browserClients[request->id()]->cefBrowser, true);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SendFocus(
		CallbackServerContext* context,
		const MouseEventRequest* request,
		NoReply* reply) override {

		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);

		bool focus = request->focus();

		ExecuteOnBrowser(
			[=](CefRefPtr<CefBrowser> cefBrowser) {
				cefBrowser->GetHost()->SendFocusEvent(focus);
			}, browserClients[request->id()]->cefBrowser, true);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* SendKeyClick(
		CallbackServerContext* context,
		const MouseEventRequest* request,
		NoReply* reply) override {

		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);

		std::string text = request->text();
#ifdef __linux__
		uint32_t native_vkey = KeyboardCodeFromXKeysym(request->native_vkey());
		uint32_t modifiers = request->modifiers();
#elif defined(_WIN32)
		uint32_t native_vkey = request->native_vkey();
		uint32_t modifiers = request->modifiers();
#else
		uint32_t native_vkey = request->native_vkey();
		uint32_t native_scancode = request->native_scancode();
		uint32_t modifiers = request->modifiers();
#endif
		bool key_up = request->key_up();

		ExecuteOnBrowser(
			[=](CefRefPtr<CefBrowser> cefBrowser) {
				CefKeyEvent e;
				e.windows_key_code = native_vkey;
#ifdef __APPLE__
				e.native_key_code = native_scancode;
#endif

				e.type = key_up ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;

				if (!text.empty()) {
					std::wstring wide = to_wide(text);
					if (wide.size())
						e.character = wide[0];
				}

				//e.native_key_code = native_vkey;
				e.modifiers = modifiers;

				cefBrowser->GetHost()->SendKeyEvent(e);
				if (!text.empty() && !key_up) {
					e.type = KEYEVENT_CHAR;
#ifdef __linux__
					e.windows_key_code =
						KeyboardCodeFromXKeysym(e.character);
#elif defined(_WIN32)
					e.windows_key_code = e.character;
#else
					e.native_key_code = native_scancode;
#endif
					cefBrowser->GetHost()->SendKeyEvent(e);
				}
			}, browserClients[request->id()]->cefBrowser, true);

		ServerUnaryReactor* reactor = context->DefaultReactor();
		reactor->Finish(Status::OK);
		return reactor;
	}

	ServerUnaryReactor* OnAudioStreamStarted(
		CallbackServerContext* context,
		const IdRequest* request,
		OnAudioStreamStartedReply* reply) override {
		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}

		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);
		browserClients[request->id()]->OnAudioStreamStarted_reactor = context->DefaultReactor();
		browserClients[request->id()]->OnAudioStreamStarted_reply = reply;
		return browserClients[request->id()]->OnAudioStreamStarted_reactor;
	}

	ServerUnaryReactor* OnAudioStreamPacket(
		CallbackServerContext* context,
		const OnAudioStreamPacketRequest* request,
		OnAudioStreamPacketReply* reply) override {
		std::lock_guard<std::mutex> lock_clients(browser_clients_mtx);
		if (!browserClients[request->id()]) {
			ServerUnaryReactor* reactor = context->DefaultReactor();
			reactor->Finish(Status::OK);
			return reactor;
		}
		
		std::lock_guard<std::mutex> lock_client(browserClients[request->id()]->browser_mtx);
		browserClients[request->id()]->OnAudioStreamPacket_requested = true;
		browserClients[request->id()]->channels = request->channels();
		browserClients[request->id()]->OnAudioStreamPacket_reactor = context->DefaultReactor();
		browserClients[request->id()]->OnAudioStreamPacket_reply = reply;
		return browserClients[request->id()]->OnAudioStreamPacket_reactor;
	}
};


void RunServer() {
  std::string server_address("0.0.0.0:50051");
  BrowserServerServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  server = builder.BuildAndStart();
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}


int main(int argc, char** argv) {
	RunServer();

	if (shutdown_thread && shutdown_thread->joinable())
		shutdown_thread->join();
	return 0;
}
