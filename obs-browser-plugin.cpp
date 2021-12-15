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

// #include <util/threading.h>
#include <util/platform.h>
#include <util/util.hpp>
#include <util/dstr.hpp>
#include <obs-module.h>
#include <obs.hpp>
#include <functional>

#include "obs-browser-source.hpp"

#include <string>

#ifdef _WIN32
#include <windows.h>
#include <util/windows/ComPtr.hpp>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <winnt.h>
#else
#include <spawn.h>
extern char **environ;
#endif


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-browser", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "CEF-based web browser source & panels";
}

#if defined(_WIN32) || defined(__APPLE__)
static int adapterCount = 0;
#endif
static std::wstring deviceId;

bool hwaccel = false;

static const char *default_css = "\
body { \
background-color: rgba(0, 0, 0, 0); \
margin: 0px auto; \
overflow: hidden; \
}";

static void browser_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "url",
				    "https://obsproject.com/browser-source");
	obs_data_set_default_int(settings, "width", 800);
	obs_data_set_default_int(settings, "height", 600);
	obs_data_set_default_int(settings, "fps", 30);
#ifdef SHARED_TEXTURE_SUPPORT_ENABLED
	obs_data_set_default_bool(settings, "fps_custom", false);
#else
	obs_data_set_default_bool(settings, "fps_custom", true);
#endif
	obs_data_set_default_bool(settings, "shutdown", false);
	obs_data_set_default_bool(settings, "restart_when_active", false);
	obs_data_set_default_string(settings, "css", default_css);

#ifdef __APPLE__
	obs_data_set_default_bool(settings, "reroute_audio", true);
#else
	obs_data_set_default_bool(settings, "reroute_audio", false);
#endif
}

static bool is_local_file_modified(obs_properties_t *props, obs_property_t *,
				   obs_data_t *settings)
{
	bool enabled = obs_data_get_bool(settings, "is_local_file");
	obs_property_t *url = obs_properties_get(props, "url");
	obs_property_t *local_file = obs_properties_get(props, "local_file");
	obs_property_set_visible(url, !enabled);
	obs_property_set_visible(local_file, enabled);

	return true;
}

static bool is_fps_custom(obs_properties_t *props, obs_property_t *,
			  obs_data_t *settings)
{
	bool enabled = obs_data_get_bool(settings, "fps_custom");
	obs_property_t *fps = obs_properties_get(props, "fps");
	obs_property_set_visible(fps, enabled);

	return true;
}

static obs_properties_t *browser_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	BrowserSource *bs = static_cast<BrowserSource *>(data);
	DStr path;

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);
	obs_property_t *prop = obs_properties_add_bool(props, "is_local_file", obs_module_text("LocalFile"));

	if (bs && !bs->url.empty()) {
		const char *slash;

		dstr_copy(path, bs->url.c_str());
		dstr_replace(path, "\\", "/");
		slash = strrchr(path->array, '/');
		if (slash)
			dstr_resize(path, slash - path->array + 1);
	}

	obs_property_set_modified_callback(prop, is_local_file_modified);
	obs_properties_add_path(props, "local_file",
				obs_module_text("LocalFile"), OBS_PATH_FILE,
				"*.*", path->array);
	obs_properties_add_text(props, "url", obs_module_text("URL"),
				OBS_TEXT_DEFAULT);

	obs_properties_add_int(props, "width", obs_module_text("Width"), 1, 4096, 1);
	obs_properties_add_int(props, "height", obs_module_text("Height"), 1, 4096, 1);

	obs_property_t *fps_set = obs_properties_add_bool(
		props, "fps_custom", obs_module_text("CustomFrameRate"));
	obs_property_set_modified_callback(fps_set, is_fps_custom);

#ifndef SHARED_TEXTURE_SUPPORT_ENABLED
	obs_property_set_enabled(fps_set, false);
#endif

	obs_properties_add_bool(props, "reroute_audio",
				obs_module_text("RerouteAudio"));

	obs_properties_add_int(props, "fps", obs_module_text("FPS"), 1, 60, 1);
	obs_property_t *p = obs_properties_add_text(
		props, "css", obs_module_text("CSS"), OBS_TEXT_MULTILINE);
	obs_property_text_set_monospace(p, true);
	obs_properties_add_bool(props, "shutdown",
				obs_module_text("ShutdownSourceNotVisible"));
	obs_properties_add_bool(props, "restart_when_active",
				obs_module_text("RefreshBrowserActive"));

	obs_properties_add_button(
		props, "refreshnocache", obs_module_text("RefreshNoCache"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			static_cast<BrowserSource *>(data)->Refresh();
			return false;
		});
	return props;
}

static void missing_file_callback(void *src, const char *new_path, void *data)
{
	BrowserSource *bs = static_cast<BrowserSource *>(src);

	if (bs) {
		obs_source_t *source = bs->source;
		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_set_string(settings, "local_file", new_path);
		obs_source_update(source, settings);
		obs_data_release(settings);
	}

	UNUSED_PARAMETER(data);
}

static obs_missing_files_t *browser_source_missingfiles(void *data)
{
	BrowserSource *bs = static_cast<BrowserSource *>(data);
	obs_missing_files_t *files = obs_missing_files_create();

	if (bs) {
		obs_source_t *source = bs->source;
		obs_data_t *settings = obs_source_get_settings(source);

		bool enabled = obs_data_get_bool(settings, "is_local_file");
		const char *path = obs_data_get_string(settings, "local_file");

		if (enabled && strcmp(path, "") != 0) {
			if (!os_file_exists(path)) {
				obs_missing_file_t *file =
					obs_missing_file_create(
						path, missing_file_callback,
						OBS_MISSING_FILE_SOURCE,
						bs->source, NULL);

				obs_missing_files_add_file(files, file);
			}
		}

		obs_data_release(settings);
	}

	return files;
}

void RegisterBrowserSource()
{
	struct obs_source_info info = {};
	info.id = "browser_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO |
#if CHROME_VERSION_BUILD >= 3683
			    OBS_SOURCE_AUDIO |
#endif
			    OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION |
			    OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_SRGB;
	info.get_properties = browser_source_get_properties;
	info.get_defaults = browser_source_get_defaults;
	info.icon_type = OBS_ICON_TYPE_BROWSER;

	info.get_name = [](void *) { return obs_module_text("BrowserSource"); };
	blog(LOG_INFO, "RegisterBrowserSource");
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
        blog(LOG_INFO, "Browser Source, INIT via info.create , settings %p source %p", settings, source);

        obs_source_set_audio_mixers(source, 0xFF);
        obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_ONLY);
        BrowserSource *bs = new BrowserSource(settings, source);
        
        return bs;
	};
	info.destroy = [](void *data) {
		delete static_cast<BrowserSource *>(data);
	};
	info.missing_files = browser_source_missingfiles;
	info.update = [](void *data, obs_data_t *settings) {
		BrowserSource *bs = static_cast<BrowserSource *>(data);
		bs->Update(settings);
	};
	info.get_width = [](void *data) {
		return (uint32_t) static_cast<BrowserSource *>(data)->width;
	};
	info.get_height = [](void *data) {
		return (uint32_t) static_cast<BrowserSource *>(data)->height;
	};
	info.video_tick = [](void *data, float) {
		static_cast<BrowserSource *>(data)->Tick();
	};
	info.video_render = [](void *data, gs_effect_t *) {
		static_cast<BrowserSource *>(data)->Render();
	};
#if CHROME_VERSION_BUILD >= 3683 && CHROME_VERSION_BUILD < 4103
	info.audio_mix = [](void *data, uint64_t *ts_out,
			    struct audio_output_data *audio_output,
			    size_t channels, size_t sample_rate) {
		return static_cast<BrowserSource *>(data)->AudioMix(
			ts_out, audio_output, channels, sample_rate);
	};
	info.enum_active_sources = [](void *data, obs_source_enum_proc_t cb,
				      void *param) {
		static_cast<BrowserSource *>(data)->EnumAudioStreams(cb, param);
	};
#endif
	info.mouse_click = [](void *data, const struct obs_mouse_event *event,
			      int32_t type, bool mouse_up,
			      uint32_t click_count) {
		static_cast<BrowserSource *>(data)->SendMouseClick(
			event, type, mouse_up, click_count);
	};
	info.mouse_move = [](void *data, const struct obs_mouse_event *event,
			     bool mouse_leave) {
		static_cast<BrowserSource *>(data)->SendMouseMove(event,
								  mouse_leave);
	};
	info.mouse_wheel = [](void *data, const struct obs_mouse_event *event,
			      int x_delta, int y_delta) {
		static_cast<BrowserSource *>(data)->SendMouseWheel(
			event, x_delta, y_delta);
	};
	info.focus = [](void *data, bool focus) {
		static_cast<BrowserSource *>(data)->SendFocus(focus);
	};
	info.key_click = [](void *data, const struct obs_key_event *event,
			    bool key_up) {
		static_cast<BrowserSource *>(data)->SendKeyClick(event, key_up);
	};
	info.show = [](void *data) {
		static_cast<BrowserSource *>(data)->SetShowing(true);
	};
	info.hide = [](void *data) {
		static_cast<BrowserSource *>(data)->SetShowing(false);
	};
	info.activate = [](void *data) {
		BrowserSource *bs = static_cast<BrowserSource *>(data);
		if (bs->restart)
			bs->Refresh();
		bs->SetActive(true);
	};
	info.deactivate = [](void *data) {
		static_cast<BrowserSource *>(data)->SetActive(false);
	};

	obs_register_source(&info);
}

#ifdef _WIN32
static inline void EnumAdapterCount()
{
	ComPtr<IDXGIFactory1> factory;
	ComPtr<IDXGIAdapter1> adapter;
	HRESULT hr;
	UINT i = 0;

	hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory);
	if (FAILED(hr))
		return;

	while (factory->EnumAdapters1(i++, &adapter) == S_OK) {
		DXGI_ADAPTER_DESC desc;

		hr = adapter->GetDesc(&desc);
		if (FAILED(hr))
			continue;

		if (i == 1)
			deviceId = desc.Description;

		/* ignore Microsoft's 'basic' renderer' */
		if (desc.VendorId == 0x1414 && desc.DeviceId == 0x8c)
			continue;

		adapterCount++;
	}
}
#endif

#ifdef SHARED_TEXTURE_SUPPORT_ENABLED
#ifdef _WIN32
static const wchar_t *blacklisted_devices[] = {
	L"Intel", L"Microsoft", L"Radeon HD 8850M", L"Radeon HD 7660", nullptr};

static inline bool is_intel(const std::wstring &str)
{
	return wstrstri(str.c_str(), L"Intel") != 0;
}

static void check_hwaccel_support(void)
{
	/* do not use hardware acceleration if a blacklisted device is the
	 * default and on 2 or more adapters */
	const wchar_t **device = blacklisted_devices;

	if (adapterCount >= 2 || !is_intel(deviceId)) {
		while (*device) {
			if (!!wstrstri(deviceId.c_str(), *device)) {
				hwaccel = false;
				blog(LOG_INFO, "[obs-browser]: "
					       "Blacklisted device "
					       "detected, "
					       "disabling browser "
					       "source hardware "
					       "acceleration.");
				break;
			}
			device++;
		}
	}
}
#elif defined(__APPLE__)
extern bool atLeast10_15(void);

static void check_hwaccel_support(void)
{
	if (!atLeast10_15()) {
		blog(LOG_INFO,
		     "[obs-browser]: OS version older than 10.15 Disabling hwaccel");
		hwaccel = false;
	}
	return;
}
#else
static void check_hwaccel_support(void)
{
	return;
}
#endif
#endif

// #include <grpcpp/grpcpp.h>

BrowserGRPCClient* bc;
#include <locale>
#include <codecvt>
static thread_local std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>
	converter;

std::wstring from_utf8_to_utf16_wide(const char *from, size_t length = -1)
{
	const char *from_end;

	if (length == 0)
		return {};
	else if (length != -1)
		from_end = from + length;
	else
		return converter.from_bytes(from);

	return converter.from_bytes(from, from_end);
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-browser]: loading module");
	std::string path = obs_get_module_binary_path(obs_current_module());
	path = path.substr(0, path.find_last_of('/') + 1);
	path += "//obs-browser-server.exe";

	const std::wstring utfProgram(from_utf8_to_utf16_wide(path.c_str()));

#ifdef WIN32
	STARTUPINFO info={sizeof(info)};
	PROCESS_INFORMATION processInfo;

	BOOL success = CreateProcess(
	    utfProgram.c_str(),
	    NULL,
	    NULL,
	    NULL,
	    FALSE,
	    CREATE_NO_WINDOW | DETACHED_PROCESS,
	    NULL,
	    NULL,
	    &info,
	    &processInfo);

	if (!success) return false;
#else
	pid_t pid;
	int success = posix_spawnp(&pid, path.c_str(), NULL, NULL, NULL, environ);
#endif

	std::string target_str = "localhost:50051";
	bc = new BrowserGRPCClient(
		grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
	
#ifdef SHARED_TEXTURE_SUPPORT_ENABLED
	obs_data_t *private_data = obs_get_private_data();
	hwaccel = obs_data_get_bool(private_data, "BrowserHWAccel");

	if (hwaccel) {
		check_hwaccel_support();
	}
	obs_data_release(private_data);
#endif

	RegisterBrowserSource();
	bc->IntializeBrowserCEF();

	return true;
}

void obs_module_unload(void)
{
	bc->ShutdownBrowserCEF();
}
