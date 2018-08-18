#include "GameApp.h"
#include <filesystem>
#include <algorithm>

using namespace DirectX;
using namespace std::experimental;

GameApp::GameApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

GameApp::~GameApp()
{
}

bool GameApp::Init()
{
	if (!D3DApp::Init())
		return false;

	if (!mBasicFX.InitAll(md3dDevice))
		return false;

	if (!InitResource())
		return false;

	// 初始化鼠标，键盘不需要
	mMouse->SetWindow(mhMainWnd);
	mMouse->SetMode(DirectX::Mouse::MODE_ABSOLUTE);

	return true;
}

void GameApp::OnResize()
{
	assert(md2dFactory);
	assert(mdwriteFactory);
	// 释放D2D的相关资源
	mColorBrush.Reset();
	md2dRenderTarget.Reset();

	D3DApp::OnResize();

	// 为D2D创建DXGI表面渲染目标
	ComPtr<IDXGISurface> surface;
	HR(mSwapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())));
	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
	HR(md2dFactory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, md2dRenderTarget.GetAddressOf()));

	surface.Reset();
	// 创建固定颜色刷和文本格式
	HR(md2dRenderTarget->CreateSolidColorBrush(
		D2D1::ColorF(D2D1::ColorF::White),
		mColorBrush.GetAddressOf()));
	HR(mdwriteFactory->CreateTextFormat(L"宋体", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15, L"zh-cn",
		mTextFormat.GetAddressOf()));
	
	// 只有常量缓冲区被初始化后才执行更新操作
	if (mBasicFX.IsInit())
	{
		mCBChangesOnReSize.proj = XMMatrixPerspectiveFovLH(XM_PI / 3, AspectRatio(), 0.5f, 1000.0f);
		mBasicFX.UpdateConstantBuffer(mCBChangesOnReSize);
	}
	
}

void GameApp::UpdateScene(float dt)
{

	// 更新鼠标事件，获取相对偏移量
	Mouse::State mouseState = mMouse->GetState();
	Mouse::State lastMouseState = mMouseTracker.GetLastState();
	mMouseTracker.Update(mouseState);

	Keyboard::State keyState = mKeyboard->GetState();
	mKeyboardTracker.Update(keyState);

	// 更新每帧变化的值
	if (mShowMode == Mode::SplitedTriangle)
	{
		mCBChangesEveryFrame.worldInvTranspose = mCBChangesEveryFrame.world = XMMatrixIdentity();
	}
	else
	{
		static float phi = 0.0f, theta = 0.0f;
		phi += 0.2f * dt, theta += 0.3f * dt;
		mCBChangesEveryFrame.world = XMMatrixRotationX(phi) * XMMatrixRotationY(theta);
		mCBChangesEveryFrame.worldInvTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, mCBChangesEveryFrame.world));
	}
	mBasicFX.UpdateConstantBuffer(mCBChangesEveryFrame);


	// 切换显示模式
	if (mKeyboardTracker.IsKeyPressed(Keyboard::D1))
	{
		mShowMode = Mode::SplitedTriangle;
		ResetTriangle();
		// 输入装配阶段的顶点缓冲区设置
		UINT stride = sizeof(VertexPosColor);		// 跨越字节数
		UINT offset = 0;							// 起始偏移量
		md3dImmediateContext->IASetVertexBuffers(0, 1, mVertexBuffer.GetAddressOf(), &stride, &offset);
		mBasicFX.SetRenderSplitedTriangle();
	}
	else if (mKeyboardTracker.IsKeyPressed(Keyboard::D2))
	{
		mShowMode = Mode::CylinderNoCap;
		ResetRoundWire();
		// 输入装配阶段的顶点缓冲区设置
		UINT stride = sizeof(VertexPosNormalColor);		// 跨越字节数
		UINT offset = 0;								// 起始偏移量
		md3dImmediateContext->IASetVertexBuffers(0, 1, mVertexBuffer.GetAddressOf(), &stride, &offset);
		mBasicFX.SetRenderCylinderNoCap();
	}

	// 显示法向量
	if (mKeyboardTracker.IsKeyPressed(Keyboard::Q))
	{
		if (mShowMode == Mode::CylinderNoCap)
			mShowMode = Mode::CylinderNoCapWithNormal;
		else if (mShowMode == Mode::CylinderNoCapWithNormal)
			mShowMode = Mode::CylinderNoCap;
	}

}

void GameApp::DrawScene()
{
	assert(md3dImmediateContext);
	assert(mSwapChain);

	md3dImmediateContext->ClearRenderTargetView(mRenderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Black));
	md3dImmediateContext->ClearDepthStencilView(mDepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	md3dImmediateContext->Draw(mVertexCount, 0);
	// 绘制法向量，绘制完后记得归位
	if (mShowMode == Mode::CylinderNoCapWithNormal)
	{
		mBasicFX.SetRenderNormal();
		md3dImmediateContext->Draw(mVertexCount, 0);
		mBasicFX.SetRenderCylinderNoCap();
	}


	//
	// 绘制Direct2D部分
	//
	md2dRenderTarget->BeginDraw();
	std::wstring text = L"切换类型：1-分裂的三角形 2-圆线构造柱面\n"
		"当前模式: ";
	if (mShowMode == Mode::SplitedTriangle)
		text += L"分裂的三角形";
	else if (mShowMode == Mode::CylinderNoCap)
		text += L"圆线构造柱面(Q-显示圆线的法向量)";
	else
		text += L"圆线构造柱面(Q-隐藏圆线的法向量)";
	md2dRenderTarget->DrawTextW(text.c_str(), (UINT32)text.length(), mTextFormat.Get(),
		D2D1_RECT_F{ 0.0f, 0.0f, 600.0f, 200.0f }, mColorBrush.Get());
	HR(md2dRenderTarget->EndDraw());

	HR(mSwapChain->Present(0, 0));
}



bool GameApp::InitResource()
{
	
	// 默认绘制三角形
	mShowMode = Mode::SplitedTriangle;
	ResetTriangle();
	
	// ******************
	// 初始化常量缓冲区的值
	// 方向光
	mCBNeverChange.dirLight.Ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	mCBNeverChange.dirLight.Diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	mCBNeverChange.dirLight.Specular = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	mCBNeverChange.dirLight.Direction = XMFLOAT3(-0.577f, -0.577f, 0.577f);
	// 材质
	mCBNeverChange.material.Ambient = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	mCBNeverChange.material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	mCBNeverChange.material.Specular = XMFLOAT4(0.5f, 0.5f, 0.5f, 5.0f);
	// 摄像机位置
	mCBNeverChange.eyePos = XMFLOAT3(0.0f, 0.0f, -5.0f);
	// 矩阵
	mCBChangesEveryFrame.world = XMMatrixIdentity();
	mCBChangesEveryFrame.worldInvTranspose = XMMatrixIdentity();
	mCBNeverChange.view = XMMatrixLookAtLH(
		XMVectorSet(0.0f, 0.0f, -5.0f, 1.0f), 
		XMVectorZero(), 
		XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	mCBNeverChange.cylinderHeight = 2.0f;
	mCBChangesOnReSize.proj = XMMatrixPerspectiveFovLH(XM_PI / 3, AspectRatio(), 1.0f, 1000.0f);

	// 更新常量缓冲区资源
	mBasicFX.UpdateConstantBuffer(mCBChangesOnReSize);
	mBasicFX.UpdateConstantBuffer(mCBNeverChange);

	// ******************
	// 给渲染管线各个阶段绑定好所需资源

	// 输入装配阶段的顶点缓冲区设置
	UINT stride = sizeof(VertexPosColor);		// 跨越字节数
	UINT offset = 0;							// 起始偏移量
	md3dImmediateContext->IASetVertexBuffers(0, 1, mVertexBuffer.GetAddressOf(), &stride, &offset);
	// 设置默认渲染状态
	mBasicFX.SetRenderSplitedTriangle();


	return true;
}


void GameApp::ResetTriangle()
{
	// ******************
	// 初始化三角形
	// 设置三角形顶点
	VertexPosColor vertices[] =
	{
		{ XMFLOAT3(-1.0f * 3, -0.866f * 3, 0.0f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) },
		{ XMFLOAT3(0.0f * 3, 0.866f * 3, 0.0f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) },
		{ XMFLOAT3(1.0f * 3, -0.866f * 3, 0.0f), XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f) }
	};
	// 设置顶点缓冲区描述
	D3D11_BUFFER_DESC vbd;
	ZeroMemory(&vbd, sizeof(vbd));
	vbd.Usage = D3D11_USAGE_DEFAULT;
	vbd.ByteWidth = sizeof vertices;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	// 新建顶点缓冲区
	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertices;
	HR(md3dDevice->CreateBuffer(&vbd, &InitData, mVertexBuffer.ReleaseAndGetAddressOf()));
	// 三角形顶点数
	mVertexCount = 3;
}

void GameApp::ResetRoundWire()
{
	// ******************
	// 初始化圆线
	// 设置圆边上各顶点
	// 必须要按顺时针设置
	// 由于要形成闭环，起始点需要使用2次
	//  ______
	// /      \
	// \______/
	//
	VertexPosNormalColor vertices[41];
	for (int i = 0; i < 40; ++i)
	{
		vertices[i].pos = XMFLOAT3(cosf(XM_PI / 20 * i), -1.0f, -sinf(XM_PI / 20 * i));
		vertices[i].normal = XMFLOAT3(cosf(XM_PI / 20 * i), 0.0f, -sinf(XM_PI / 20 * i));
		vertices[i].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	vertices[40] = vertices[0];

	// 设置顶点缓冲区描述
	D3D11_BUFFER_DESC vbd;
	ZeroMemory(&vbd, sizeof(vbd));
	vbd.Usage = D3D11_USAGE_DEFAULT;
	vbd.ByteWidth = sizeof vertices;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	// 新建顶点缓冲区
	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertices;
	HR(md3dDevice->CreateBuffer(&vbd, &InitData, mVertexBuffer.ReleaseAndGetAddressOf()));
	// 线框顶点数
	mVertexCount = 41;
}



