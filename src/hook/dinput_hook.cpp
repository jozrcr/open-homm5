// DirectInput 8 hooks shared by the robot and the overlay (see dinput_hook.h).
#define DIRECTINPUT_VERSION 0x0800
#include "dinput_hook.h"

#include <dinput.h>

#include "log.h"

namespace dihook {
namespace {

// vtable slots (IUnknown's 3 first): IDirectInput8 CreateDevice = 3;
// IDirectInputDevice8 Acquire = 7, GetDeviceState = 9, GetDeviceData = 10.
const int kCreateDeviceSlot = 3;
const int kAcquireSlot = 7;
const int kGetDeviceStateSlot = 9;
const int kGetDeviceDataSlot = 10;

typedef HRESULT(STDMETHODCALLTYPE *CreateDeviceFn)(void *self, REFGUID guid, void **device, IUnknown *outer);
typedef HRESULT(STDMETHODCALLTYPE *AcquireFn)(void *self);
typedef HRESULT(STDMETHODCALLTYPE *GetDeviceStateFn)(void *self, DWORD size, void *data);
typedef HRESULT(STDMETHODCALLTYPE *GetDeviceDataFn)(void *self, DWORD size, DIDEVICEOBJECTDATA *data, DWORD *count,
                                                    DWORD flags);

struct User {
	Filters filters;
	int order;
};
const int kMaxUsers = 4;
User g_users[kMaxUsers];
int g_user_count = 0;

// The replaced entries, per vtable (the ANSI and wide interfaces have their
// own; a vtable is hooked once, and an entry that is already ours is left).
struct InputTable {
	void **vtable;
	CreateDeviceFn create_device;
};
struct DeviceTable {
	void **vtable;
	AcquireFn acquire;
	GetDeviceStateFn get_state;
	GetDeviceDataFn get_data;
};
const int kTables = 4;
InputTable g_inputs[kTables];
DeviceTable g_devices[kTables];
int g_input_count = 0, g_device_count = 0;

// The keyboards and mice the game made (the last few).
struct Known {
	void *device;
	DeviceKind kind;
};
const int kKnown = 8;
Known g_known[kKnown];
int g_known_next = 0;
CRITICAL_SECTION g_lock;
bool g_lock_ready = false;

void *ReplaceSlot(void **vtable, int slot, void *replacement)
{
	DWORD old;
	if (!VirtualProtect(&vtable[slot], sizeof(void *), PAGE_READWRITE, &old))
		return nullptr;
	void *original = vtable[slot];
	vtable[slot] = replacement;
	VirtualProtect(&vtable[slot], sizeof(void *), old, &old);
	return original;
}

void **VtableOf(void *object)
{
	return *static_cast<void ***>(object);
}

const InputTable *InputTableOf(void *self)
{
	void **vtable = VtableOf(self);
	for (int i = 0; i < g_input_count; ++i)
		if (g_inputs[i].vtable == vtable)
			return &g_inputs[i];
	return nullptr;
}

const DeviceTable *DeviceTableOf(void *self)
{
	void **vtable = VtableOf(self);
	for (int i = 0; i < g_device_count; ++i)
		if (g_devices[i].vtable == vtable)
			return &g_devices[i];
	return &g_devices[0];  // (cannot happen: only the vtables in the table call us)
}

DeviceKind KindOf(void *device)
{
	for (const Known &k : g_known)
		if (k.device == device)
			return k.kind;
	return kOtherDevice;
}

HRESULT STDMETHODCALLTYPE HookAcquire(void *self)
{
	HRESULT hr = DeviceTableOf(self)->acquire(self);
	const DeviceKind kind = KindOf(self);
	if (kind == kOtherDevice)
		return hr;
	for (int i = 0; i < g_user_count; ++i)
		if (g_users[i].filters.acquire)
			hr = g_users[i].filters.acquire(kind, self, hr);
	return hr;
}

HRESULT STDMETHODCALLTYPE HookGetDeviceState(void *self, DWORD size, void *data)
{
	HRESULT hr = DeviceTableOf(self)->get_state(self, size, data);
	const DeviceKind kind = KindOf(self);
	if (kind == kOtherDevice || !data)
		return hr;
	for (int i = 0; i < g_user_count; ++i)
		if (g_users[i].filters.state)
			hr = g_users[i].filters.state(kind, self, size, data, hr);
	return hr;
}

HRESULT STDMETHODCALLTYPE HookGetDeviceData(void *self, DWORD size, DIDEVICEOBJECTDATA *data, DWORD *count, DWORD flags)
{
	const DWORD capacity = count ? *count : 0;
	HRESULT hr = DeviceTableOf(self)->get_data(self, size, data, count, flags);
	const DeviceKind kind = KindOf(self);
	if (kind == kOtherDevice)
		return hr;
	for (int i = 0; i < g_user_count; ++i)
		if (g_users[i].filters.data)
			hr = g_users[i].filters.data(kind, self, size, data, count, capacity, flags, hr);
	return hr;
}

void HookDevice(void *device, DeviceKind kind)
{
	EnterCriticalSection(&g_lock);
	g_known[g_known_next] = {device, kind};
	g_known_next = (g_known_next + 1) % kKnown;
	void **vtable = VtableOf(device);
	bool known_table = false;
	for (int i = 0; i < g_device_count; ++i)
		known_table = known_table || g_devices[i].vtable == vtable;
	const bool ours = vtable[kGetDeviceStateSlot] == reinterpret_cast<void *>(&HookGetDeviceState);
	if (!known_table && !ours && g_device_count < kTables) {
		// The originals first: an entry that cannot be replaced keeps calling
		// DirectInput directly, which is fine.
		DeviceTable &t = g_devices[g_device_count++];
		t.vtable = vtable;
		t.acquire = reinterpret_cast<AcquireFn>(vtable[kAcquireSlot]);
		t.get_state = reinterpret_cast<GetDeviceStateFn>(vtable[kGetDeviceStateSlot]);
		t.get_data = reinterpret_cast<GetDeviceDataFn>(vtable[kGetDeviceDataSlot]);
		const bool ok = ReplaceSlot(vtable, kAcquireSlot, reinterpret_cast<void *>(&HookAcquire)) &&
		                ReplaceSlot(vtable, kGetDeviceStateSlot, reinterpret_cast<void *>(&HookGetDeviceState)) &&
		                ReplaceSlot(vtable, kGetDeviceDataSlot, reinterpret_cast<void *>(&HookGetDeviceData));
		if (!ok)
			hooklog::Write("dinput: could not hook all of the device vtable %p", vtable);
	}
	LeaveCriticalSection(&g_lock);
	hooklog::Write("dinput: %s device %p", kind == kKeyboard ? "keyboard" : "mouse", device);
	for (int i = 0; i < g_user_count; ++i)
		if (g_users[i].filters.created)
			g_users[i].filters.created(kind, device);
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(void *self, REFGUID guid, void **device, IUnknown *outer)
{
	const InputTable *t = InputTableOf(self);
	const HRESULT hr = t ? t->create_device(self, guid, device, outer) : E_FAIL;
	if (SUCCEEDED(hr) && device && *device) {
		if (IsEqualGUID(guid, GUID_SysKeyboard))
			HookDevice(*device, kKeyboard);
		else if (IsEqualGUID(guid, GUID_SysMouse))
			HookDevice(*device, kMouse);
	}
	return hr;
}

}  // namespace

void AddFilters(const Filters &filters, int order)
{
	if (g_user_count >= kMaxUsers)
		return;
	int i = g_user_count++;
	while (i > 0 && g_users[i - 1].order > order) {
		g_users[i] = g_users[i - 1];
		--i;
	}
	g_users[i] = {filters, order};
}

void OnDirectInputCreated(void *direct_input)
{
	if (!g_user_count || !direct_input)
		return;
	if (!g_lock_ready) {
		InitializeCriticalSection(&g_lock);
		g_lock_ready = true;
	}
	void **vtable = VtableOf(direct_input);
	EnterCriticalSection(&g_lock);
	const bool known = InputTableOf(direct_input) != nullptr;
	if (!known && g_input_count < kTables && vtable[kCreateDeviceSlot] != reinterpret_cast<void *>(&HookCreateDevice)) {
		if (void *original = ReplaceSlot(vtable, kCreateDeviceSlot, reinterpret_cast<void *>(&HookCreateDevice))) {
			g_inputs[g_input_count++] = {vtable, reinterpret_cast<CreateDeviceFn>(original)};
			hooklog::Write("dinput: DirectInput %p hooked (%d users)", direct_input, g_user_count);
		}
	}
	LeaveCriticalSection(&g_lock);
}

}  // namespace dihook
