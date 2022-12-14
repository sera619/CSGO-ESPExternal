#include <iostream>
#include <format>

#include "memory.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>

namespace offsets {
	constexpr auto local_player = 0xDE6964;
	constexpr auto entity_list = 0x4DFBE54;
	constexpr auto view_matrix = 0x4DECC84;

	constexpr auto bone_matrix= 0x26A8;
	constexpr auto team_num= 0xF4;
	constexpr auto life_state = 0x25F;
	constexpr auto origin = 0x138;
	constexpr auto dormant = 0xED;
}

struct Vector {
	Vector() noexcept
		: x(), y(), z() {}

	Vector(float x, float y, float z) noexcept
		: x(x), y(y), z(z) {}

	Vector& operator+(const Vector& v) noexcept {
		x += v.x;
		y += v.y;
		z += v.z;
		return *this;
	}
	Vector& operator-(const Vector& v) noexcept {
		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	}

	float x, y, z;
};

struct ViewMatrix {
	ViewMatrix() noexcept
		: data() {}

	float* operator[](int index) noexcept {
		return data[index];
	}

	const float* operator[](int index)const noexcept {
		return data[index];
	}
	float data[4][4];
};

static bool world_to_screen(const Vector& world, Vector& screen, const ViewMatrix& vm) noexcept {
	float w = vm[3][0] * world.x + vm[3][1] * world.y + vm[3][2] * world.z + vm[3][3];
	if (w < 0.001f) {
		return false;
	}

	const float x = world.x * vm[0][0] + world.y * vm[0][1] + world.z * vm[0][2] + vm[0][3];
	const float y = world.x * vm[1][0] + world.y * vm[1][1] + world.z * vm[1][2] + vm[1][3];

	w = 1.f / w;
	float nx = x * w;
	float ny = y * w;

	const ImVec2 size = ImGui::GetIO().DisplaySize;

	screen.x = (size.x * 0.5f * nx) + (nx + size.x * 0.5f);
	screen.y = -(size.y * 0.5f * ny) + (ny + size.y * 0.5f);

	return true;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
	if (ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param)) {
		return 0L;
	}

	if (message == WM_DESTROY) {
		PostQuitMessage(0);
		return 0L;
	}
	return DefWindowProc(window, message, w_param, l_param);
}

INT APIENTRY WinMain(HINSTANCE instance, HINSTANCE, PSTR, INT cmd_show) {
	if (!AllocConsole()) {
		return FALSE;
	}

	FILE* file{ nullptr };
	freopen_s(&file, "CONIN$", "r", stdin);
	freopen_s(&file, "CONOUT$", "w", stdout);
	freopen_s(&file, "CONIN$", "w", stderr);

	DWORD pid = memory::get_process_id(L"csgo.exe");
	const HANDLE handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);

	if (!handle) {
		return FALSE;
	}


	if (!pid) {
		std::cout << "Waiting for CS:GO ... \n";
	}


	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = window_procedure;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = instance;
	wc.hIcon = nullptr;
	wc.hCursor = nullptr;
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = L"External Overlay Class";
	wc.hIconSm = nullptr;
	RegisterClassExW(&wc);

	const HWND window = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
		wc.lpszClassName,
		L"External Overlay",
		WS_POPUP,
		0,
		0,
		1920,
		1080,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);

	if (!window) {
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return FALSE;
	}

	if (!SetLayeredWindowAttributes(window, RGB(0, 0, 0), BYTE(255), LWA_ALPHA)) {
		DestroyWindow(window);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return FALSE;
	}

	{
		RECT client_area{};
		GetClientRect(window, &client_area);

		RECT window_area{};
		GetWindowRect(window, &window_area);

		POINT diff{};
		ClientToScreen(window, &diff);

		const MARGINS margins{
			window_area.left + (diff.x - window_area.left),
			window_area.top + (diff.y - window_area.top),
			client_area.right,
			client_area.bottom
		};
		DwmExtendFrameIntoClientArea(window, &margins);
	}

	DXGI_SWAP_CHAIN_DESC sd{};
	ZeroMemory(&sd, sizeof(sd));

	sd.BufferDesc.RefreshRate.Numerator = 60U;
	sd.BufferDesc.RefreshRate.Denominator = 1U;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.SampleDesc.Count = 1U;
	sd.SampleDesc.Quality = 0U;

	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2U;
	sd.OutputWindow = window;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	constexpr D3D_FEATURE_LEVEL levels[2]{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0
	};

	D3D_FEATURE_LEVEL feature_level{};

	ID3D11Device* device{ nullptr };
	ID3D11DeviceContext* device_context{ nullptr };
	IDXGISwapChain* swap_chain{ nullptr };
	ID3D11RenderTargetView* render_target_view{ nullptr };

	D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0U,
		levels,
		2U,
		D3D11_SDK_VERSION,
		&sd,
		&swap_chain,
		&device,
		&feature_level,
		&device_context
	);

	ID3D11Texture2D* back_buffer{ nullptr };
	if (FAILED(swap_chain->GetBuffer(0U, IID_PPV_ARGS(&back_buffer)))) {
		return FALSE;
	}

	if (FAILED(device->CreateRenderTargetView(back_buffer, nullptr, &render_target_view))) {
		return FALSE;
	}

	if (back_buffer) {
		back_buffer->Release();
	}



	ShowWindow(window, cmd_show);
	UpdateWindow(window);

	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(device, device_context);

	bool running = true;
	while (running) {
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		
			if (msg.message == WM_QUIT) {
				running = false;
			}
		}

		if (!running) {
			break;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();

		ImGui::NewFrame();
		const auto local_player = memory::Read<DWORD>(handle, client + offsets::local_player);

		ImGui::GetBackgroundDrawList()->AddCircleFilled({ 500, 500 }, 10.f, ImColor(1.f, 0.f, 0.f));

		ImGui::Render();

		constexpr float color[4]{ 0.f, 0.f, 0.f, 0.f };
		device_context->OMSetRenderTargets(1U, &render_target_view, nullptr);
		device_context->ClearRenderTargetView(render_target_view, color);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		swap_chain->Present(1U, 0U);

	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	
	ImGui::DestroyContext();

	if (swap_chain) {
		swap_chain->Release();
	}
	if (device_context) {
		device_context->Release();
	}
	if (device) {
		device->Release();
	}
	if (render_target_view) {
		render_target_view->Release();
	}

	DestroyWindow(window);
	UnregisterClassW(wc.lpszClassName, instance);

	return 0;
}