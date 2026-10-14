#include "audio/CoreAudio.hpp"

// Defines PKEY_Device_FriendlyName here instead of declaring it, since no import library carries it. objbase.h
// declares PROPERTYKEY and GUID_NULL, which WIN32_LEAN_AND_MEAN leaves out of windows.h, and must come before
// endpointvolume.h pulls in ks.h, which turns GUID_NULL into a macro. propkeydef.h declares the key header's macro.
#include <objbase.h>

#include <initguid.h>

#include <propkeydef.h>

#include <functiondiscoverykeys_devpkey.h>

#include "platform/ComScope.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

namespace {

    constexpr DWORD kRetryMs = 2000;

    EDataFlow       CoreAudioFlow(AudioFlow flow) {
        return flow == AudioFlow::Output ? eRender : eCapture;
    }

    std::string FlowName(AudioFlow flow) {
        return flow == AudioFlow::Output ? "output" : "input";
    }

    // Tells the worker when the default device of its flow changes. The notification threads must not call back into
    // Core Audio, so this only wakes the worker.
    class DeviceNotifications final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMMNotificationClient> {
      public:
        DeviceNotifications(CoreAudio& audio, EDataFlow flow) : _audio(audio), _flow(flow) {}

        HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
            if (flow == _flow && role == eConsole) {
                _audio.OnDefaultDeviceChanged();
            }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
            return S_OK;
        }

      private:
        CoreAudio& _audio;
        EDataFlow  _flow;
    };

    // Carries every volume or mute change on the device, whoever made it, into the snapshot.
    class VolumeNotifications final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAudioEndpointVolumeCallback> {
      public:
        explicit VolumeNotifications(CoreAudio& audio) : _audio(audio) {}

        HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
            _audio.OnVolumeNotified(data->fMasterVolume, data->bMuted != FALSE);
            return S_OK;
        }

      private:
        CoreAudio& _audio;
    };

    std::wstring FriendlyName(IMMDevice* device) {
        ComPtr<IPropertyStore> store;
        ThrowIfFailed(device->OpenPropertyStore(STGM_READ, &store), "IMMDevice::OpenPropertyStore");
        PROPVARIANT value;
        PropVariantInit(&value);
        ThrowIfFailed(store->GetValue(PKEY_Device_FriendlyName, &value), "IPropertyStore::GetValue(PKEY_Device_FriendlyName)");
        std::wstring name = value.vt == VT_LPWSTR && value.pwszVal != nullptr ? std::wstring(value.pwszVal) : std::wstring();
        PropVariantClear(&value);
        return name;
    }

    // One device's volume control, with its change notifications registered for as long as it lives.
    class Endpoint {
      public:
        Endpoint(IMMDevice* device, CoreAudio& audio) : _name(FriendlyName(device)), _callback(Make<VolumeNotifications>(audio)) {
            ThrowIfFailed(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, &_volume), "IMMDevice::Activate(IAudioEndpointVolume)");
            ThrowIfFailed(_volume->RegisterControlChangeNotify(_callback.Get()), "IAudioEndpointVolume::RegisterControlChangeNotify");
        }
        ~Endpoint() {
            _volume->UnregisterControlChangeNotify(_callback.Get());
        }
        Endpoint(const Endpoint&)              = delete;
        Endpoint&   operator=(const Endpoint&) = delete;

        AudioDevice Read() const {
            float volume = 0.0f;
            ThrowIfFailed(_volume->GetMasterVolumeLevelScalar(&volume), "IAudioEndpointVolume::GetMasterVolumeLevelScalar");
            BOOL muted = FALSE;
            ThrowIfFailed(_volume->GetMute(&muted), "IAudioEndpointVolume::GetMute");
            return AudioDevice{
                .name   = _name,
                .volume = volume,
                .muted  = muted != FALSE,
            };
        }

        void SetVolume(float volume) const {
            ThrowIfFailed(_volume->SetMasterVolumeLevelScalar(volume, nullptr), "IAudioEndpointVolume::SetMasterVolumeLevelScalar");
        }

        void SetMuted(bool muted) const {
            ThrowIfFailed(_volume->SetMute(muted ? TRUE : FALSE, nullptr), "IAudioEndpointVolume::SetMute");
        }

      private:
        std::wstring                         _name;
        ComPtr<IAudioEndpointVolumeCallback> _callback;
        ComPtr<IAudioEndpointVolume>         _volume;
    };

    // The device enumerator, with the default device notifications of one flow registered for as long as it lives.
    class Devices {
      public:
        Devices(CoreAudio& audio, EDataFlow flow) : _flow(flow), _callback(Make<DeviceNotifications>(audio, flow)) {
            ThrowIfFailed(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&_enumerator)), "CoCreateInstance(MMDeviceEnumerator)");
            ThrowIfFailed(_enumerator->RegisterEndpointNotificationCallback(_callback.Get()), "IMMDeviceEnumerator::RegisterEndpointNotificationCallback");
        }
        ~Devices() {
            _enumerator->UnregisterEndpointNotificationCallback(_callback.Get());
        }
        Devices(const Devices&)            = delete;
        Devices& operator=(const Devices&) = delete;

        // The default device's control, or null when the system has no device of the flow.
        std::unique_ptr<Endpoint> DefaultEndpoint(CoreAudio& audio) const {
            ComPtr<IMMDevice> device;
            const HRESULT     found = _enumerator->GetDefaultAudioEndpoint(_flow, eConsole, &device);
            if (found == E_NOTFOUND) {
                return nullptr;
            }
            ThrowIfFailed(found, "IMMDeviceEnumerator::GetDefaultAudioEndpoint");
            return std::make_unique<Endpoint>(device.Get(), audio);
        }

      private:
        EDataFlow                     _flow;
        ComPtr<IMMNotificationClient> _callback;
        ComPtr<IMMDeviceEnumerator>   _enumerator;
    };

} // namespace

CoreAudio::CoreAudio(ChangeSignal& changes, AudioFlow flow) :
    _changes(changes), _flow(flow), _deviceEvent(AutoResetEvent("audio device")), _requestEvent(AutoResetEvent("audio request")),
    _thread("audio", [this](HANDLE stopEvent) { Run(stopEvent); }) {}

std::optional<AudioDevice> CoreAudio::Device() const {
    const std::scoped_lock lock(_mutex);
    return _device;
}

void CoreAudio::SetVolume(float volume) {
    {
        const std::scoped_lock lock(_mutex);
        _pendingVolume = std::clamp(volume, 0.0f, 1.0f);
    }
    SetEvent(_requestEvent.get());
}

void CoreAudio::SetMuted(bool muted) {
    {
        const std::scoped_lock lock(_mutex);
        _pendingMuted = muted;
    }
    SetEvent(_requestEvent.get());
}

void CoreAudio::OnDefaultDeviceChanged() {
    SetEvent(_deviceEvent.get());
}

void CoreAudio::OnVolumeNotified(float volume, bool muted) {
    {
        const std::scoped_lock lock(_mutex);
        if (!_device.has_value() || (_device->volume == volume && _device->muted == muted)) {
            return;
        }
        _device->volume = volume;
        _device->muted  = muted;
    }
    _changes.Notify("audio");
}

void CoreAudio::Publish(std::optional<AudioDevice> device) {
    {
        const std::scoped_lock lock(_mutex);
        if (device == _device) {
            return;
        }
        _device = std::move(device);
    }
    _changes.Notify("audio");
}

// Nothing here may throw out of the thread, so every Core Audio failure is logged and retried.
void CoreAudio::Run(HANDLE stopEvent) {
    // Core Audio calls back on its own threads, which a multithreaded apartment allows without marshalling.
    std::optional<ComScope> com;
    try {
        com.emplace(ComThreading::Multi, "audio");
    } catch (const Win32Error& error) {
        Log(LogLevel::Error, "audio", "com_init_failed", {{.key = "flow", .value = FlowName(_flow)}, {.key = "reason", .value = error.what()}});
        return;
    }
    std::unique_ptr<Devices>    devices;
    std::unique_ptr<Endpoint>   endpoint;
    bool                        attachDue = true;
    bool                        failing   = false;
    const std::array<HANDLE, 3> waits{stopEvent, _deviceEvent.get(), _requestEvent.get()};
    while (true) {
        if (attachDue) {
            attachDue = false;
            endpoint.reset();
            try {
                if (devices == nullptr) {
                    devices = std::make_unique<Devices>(*this, CoreAudioFlow(_flow));
                }
                endpoint = devices->DefaultEndpoint(*this);
                Publish(endpoint == nullptr ? std::nullopt : std::optional<AudioDevice>(endpoint->Read()));
                if (failing) {
                    Log(LogLevel::Info, "audio", "recovered", {{.key = "flow", .value = FlowName(_flow)}});
                }
                failing = false;
            } catch (const Win32Error& error) {
                endpoint.reset();
                devices.reset();
                Publish(std::nullopt);
                if (!failing) {
                    Log(LogLevel::Warning, "audio", "unavailable",
                        {{.key = "flow", .value = FlowName(_flow)}, {.key = "reason", .value = error.what()}, {.key = "retry_ms", .value = std::to_string(kRetryMs)}});
                }
                failing = true;
            }
        }
        const DWORD woke = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, failing ? kRetryMs : INFINITE);
        if (woke == WAIT_FAILED) {
            Log(LogLevel::Error, "audio", "wait_failed", {{.key = "flow", .value = FlowName(_flow)}, {.key = "error", .value = std::to_string(GetLastError())}});
            Publish(std::nullopt);
            break;
        }
        if (woke == WAIT_OBJECT_0) {
            break;
        }
        if (woke == WAIT_OBJECT_0 + 2) {
            std::optional<float> volume;
            std::optional<bool>  muted;
            {
                const std::scoped_lock lock(_mutex);
                volume = std::exchange(_pendingVolume, std::nullopt);
                muted  = std::exchange(_pendingMuted, std::nullopt);
            }
            // A request while there is no device has nothing to change.
            if (endpoint == nullptr) {
                continue;
            }
            try {
                if (volume.has_value()) {
                    endpoint->SetVolume(*volume);
                }
                if (muted.has_value()) {
                    endpoint->SetMuted(*muted);
                }
            } catch (const Win32Error& error) {
                // The device most likely went away mid-change, so the request is dropped and the device read again.
                Log(LogLevel::Warning, "audio", "change_failed", {{.key = "flow", .value = FlowName(_flow)}, {.key = "reason", .value = error.what()}});
                attachDue = true;
            }
            continue;
        }
        // The default device changed or the retry wait ran out.
        attachDue = true;
    }
}
