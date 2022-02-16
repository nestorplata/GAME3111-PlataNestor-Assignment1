/** @file Week4-6-ShapeComplete.cpp
 *  @brief Shape Practice Solution.
 *
 *  Place all of the scene geometry in one big vertex and index buffer.
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */


#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class ShapesApp : public D3DApp
{
public:
	ShapesApp(HINSTANCE hInstance);
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	~ShapesApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		ShapesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create root CBVs.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}


void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(1.0f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.5f, 1.5f, 6.0f, 20, 20);

	GeometryGenerator::MeshData cone = geoGen.CreateCone(2.0f, 0.0f, 3.0f, 20, 20);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 0.0f, 3.0f, 4, 20);
	GeometryGenerator::MeshData prism = geoGen.CreatePrism(1.0f, 1.0f, 1.0f, 3, 1);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 0.5f, 1.0f, 0.5f, 10, 20);

	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT wedgeVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT pyramidVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT prismVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT diamondVertexOffset = prismVertexOffset + (UINT)prism.Vertices.size();




	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT wedgeIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT pyramidIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT prismIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT diamondIndexOffset = prismIndexOffset + (UINT)prism.Indices32.size();





	// Define the SubmeshGeometry that cover different
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry prismSubmesh;
	prismSubmesh.IndexCount = (UINT)prism.Indices32.size();
	prismSubmesh.StartIndexLocation = prismIndexOffset;
	prismSubmesh.BaseVertexLocation = prismVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;



	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		cone.Vertices.size() +
		wedge.Vertices.size() +
		pyramid.Vertices.size() +
		prism.Vertices.size()+
		diamond.Vertices.size();


	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Black);
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::White);
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Yellow);
	}

	for (size_t i = 0; i < prism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = prism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Orange);
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::GhostWhite);
	}


	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));	
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(prism.GetIndices16()), std::end(prism.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));




	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";


	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["prism"] = prismSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;


	mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects.

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();

	opaquePsoDesc.VS =
	{
	 reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
	 mShaders["standardVS"]->GetBufferSize()
	};

	opaquePsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
	 mShaders["opaquePS"]->GetBufferSize()
	};

	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// PSO for opaque wireframe objects.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}

void ShapesApp::BuildRenderItems()
{
	UINT objCBIndex = 0;

	//castle Walls
	// 
	//towers cylinders
	auto cylinderRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderRitem->World, XMMatrixTranslation(8.0f, 3.0f, -13.0f) * XMMatrixScaling(1, 1, 1));

	cylinderRitem->ObjCBIndex = objCBIndex++;
	cylinderRitem->Geo = mGeometries["shapeGeo"].get();
	cylinderRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderRitem->IndexCount = cylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderRitem->StartIndexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderRitem->BaseVertexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderRitem));


	auto cylinderRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&cylinderRitem2->World, XMMatrixTranslation(-8.0f, 3.0f, -13.0f) * XMMatrixScaling(1, 1, 1));
	cylinderRitem2->ObjCBIndex = objCBIndex++;
	cylinderRitem2->Geo = mGeometries["shapeGeo"].get();
	cylinderRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderRitem2->IndexCount = cylinderRitem2->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderRitem2->StartIndexLocation = cylinderRitem2->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderRitem2->BaseVertexLocation = cylinderRitem2->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderRitem2));

	auto cylinderRitem3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderRitem3->World, XMMatrixTranslation(-8.0f, 3.0f, 13.0f) * XMMatrixScaling(1, 1, 1));

	cylinderRitem3->ObjCBIndex = objCBIndex++;
	cylinderRitem3->Geo = mGeometries["shapeGeo"].get();
	cylinderRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderRitem3->IndexCount = cylinderRitem3->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderRitem3->StartIndexLocation = cylinderRitem3->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderRitem3->BaseVertexLocation = cylinderRitem3->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderRitem3));

	auto cylinderRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&cylinderRitem4->World, XMMatrixTranslation(8.0f, 3.0f, 13.0f) * XMMatrixScaling(1, 1, 1));
	cylinderRitem4->ObjCBIndex = objCBIndex++;
	cylinderRitem4->Geo = mGeometries["shapeGeo"].get();
	cylinderRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderRitem4->IndexCount = cylinderRitem4->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderRitem4->StartIndexLocation = cylinderRitem4->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderRitem4->BaseVertexLocation = cylinderRitem4->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderRitem4));

	//Entrance prism

	auto prismRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem->World, XMMatrixTranslation(1.9f, 0.5f, -5.75f) * XMMatrixScaling(1.5f, 7.0f, 2.25f));

	prismRitem->ObjCBIndex = objCBIndex++;
	prismRitem->Geo = mGeometries["shapeGeo"].get();
	prismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	prismRitem->IndexCount = prismRitem->Geo->DrawArgs["prism"].IndexCount;
	prismRitem->StartIndexLocation = prismRitem->Geo->DrawArgs["prism"].StartIndexLocation;
	prismRitem->BaseVertexLocation = prismRitem->Geo->DrawArgs["prism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(prismRitem));

	auto prismRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&prismRitem2->World, XMMatrixTranslation(1.9f, 0.5f, 5.75f) * XMMatrixScaling(1.5f, 7.0f, 2.25f)
		* XMMatrixRotationY(3.1416));

	prismRitem2->ObjCBIndex = objCBIndex++;
	prismRitem2->Geo = mGeometries["shapeGeo"].get();
	prismRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	prismRitem2->IndexCount = prismRitem2->Geo->DrawArgs["prism"].IndexCount;
	prismRitem2->StartIndexLocation = prismRitem2->Geo->DrawArgs["prism"].StartIndexLocation;
	prismRitem2->BaseVertexLocation = prismRitem2->Geo->DrawArgs["prism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(prismRitem2));

	//gate
	auto posts = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&posts->World, XMMatrixTranslation(0.0f, 4.0f, -4.25f) * XMMatrixScaling(9.0f, 2.0f, 3.0f));

	posts->ObjCBIndex = objCBIndex++;
	posts->Geo = mGeometries["shapeGeo"].get();
	posts->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	posts->IndexCount = posts->Geo->DrawArgs["box"].IndexCount;
	posts->StartIndexLocation = posts->Geo->DrawArgs["box"].StartIndexLocation;
	posts->BaseVertexLocation = posts->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(posts));

	auto pyramidRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixTranslation(0.0f, 10.5f, -8.5f)* XMMatrixScaling(4.0f, 1.0f, 1.5f));

	pyramidRitem->ObjCBIndex = objCBIndex++;
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem));


	//TowerCones

	auto coneRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&coneRitem->World, XMMatrixTranslation(8.0f, 7.0f, -13.0f) * XMMatrixScaling(1.0f, 1.0f, 1.0f));

	coneRitem->ObjCBIndex = objCBIndex++;
	coneRitem->Geo = mGeometries["shapeGeo"].get();
	coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem));

	auto coneRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&coneRitem2->World, XMMatrixTranslation(-8.0f, 7.0f, -13.0f) * XMMatrixScaling(1.0f, 1.0f, 1.0f));

	coneRitem2->ObjCBIndex = objCBIndex++;
	coneRitem2->Geo = mGeometries["shapeGeo"].get();
	coneRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem2->IndexCount = coneRitem2->Geo->DrawArgs["cone"].IndexCount;
	coneRitem2->StartIndexLocation = coneRitem2->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem2->BaseVertexLocation = coneRitem2->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem2));

	auto coneRitem3 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&coneRitem3->World, XMMatrixTranslation(-8.0f, 7.0f, 13.0f) * XMMatrixScaling(1.0f, 1.0f, 1.0f));

	coneRitem3->ObjCBIndex = objCBIndex++;
	coneRitem3->Geo = mGeometries["shapeGeo"].get();
	coneRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem3->IndexCount = coneRitem3->Geo->DrawArgs["cone"].IndexCount;
	coneRitem3->StartIndexLocation = coneRitem3->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem3->BaseVertexLocation = coneRitem3->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem3));

	auto coneRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&coneRitem4->World, XMMatrixTranslation(8.0f, 7.0f, 13.0f) * XMMatrixScaling(1.0f, 1.0f, 1.0f));

	coneRitem4->ObjCBIndex = objCBIndex++;
	coneRitem4->Geo = mGeometries["shapeGeo"].get();
	coneRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem4->IndexCount = coneRitem4->Geo->DrawArgs["cone"].IndexCount;
	coneRitem4->StartIndexLocation = coneRitem4->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem4->BaseVertexLocation = coneRitem4->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem4));

	//spheres
	auto sphereRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem->World, XMMatrixTranslation(8.0f, 10.0f, -13.0f)* XMMatrixScaling(1.0f, 1.0f, 1.0f));

	sphereRitem->ObjCBIndex = objCBIndex++;
	sphereRitem->Geo = mGeometries["shapeGeo"].get();
	sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(sphereRitem));

	auto sphereRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem2->World, XMMatrixTranslation(-8.0f, 10.0f, -13.0f)* XMMatrixScaling(1.0f, 1.0f, 1.0f));

	sphereRitem2->ObjCBIndex = objCBIndex++;
	sphereRitem2->Geo = mGeometries["shapeGeo"].get();
	sphereRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem2->IndexCount = sphereRitem2->Geo->DrawArgs["sphere"].IndexCount;
	sphereRitem2->StartIndexLocation = sphereRitem2->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRitem2->BaseVertexLocation = sphereRitem2->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(sphereRitem2));

	auto sphereRitem3 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem3->World, XMMatrixTranslation(-8.0f, 10.0f, 13.0f)* XMMatrixScaling(1.0f, 1.0f, 1.0f));

	sphereRitem3->ObjCBIndex = objCBIndex++;
	sphereRitem3->Geo = mGeometries["shapeGeo"].get();
	sphereRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem3->IndexCount = sphereRitem3->Geo->DrawArgs["sphere"].IndexCount;
	sphereRitem3->StartIndexLocation = sphereRitem3->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRitem3->BaseVertexLocation = sphereRitem3->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(sphereRitem3));


	auto sphereRitem4 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&sphereRitem4->World, XMMatrixTranslation(8.0f, 10.0f, 13.0f)* XMMatrixScaling(1.0f, 1.0f, 1.0f));

	sphereRitem4->ObjCBIndex = objCBIndex++;
	sphereRitem4->Geo = mGeometries["shapeGeo"].get();
	sphereRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem4->IndexCount = sphereRitem4->Geo->DrawArgs["sphere"].IndexCount;
	sphereRitem4->StartIndexLocation = sphereRitem4->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRitem4->BaseVertexLocation = sphereRitem4->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(sphereRitem4));

	//Walls
	// 
	//left
	auto boxRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(-4.0f, 0.5f, 0.0f) * XMMatrixScaling(2.0f, 4.0f, 28.0f)) ;

	boxRitem->ObjCBIndex = objCBIndex++;
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

	

	for (int i = 0; i < 12; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(-8.5f, 4.5f, -11.0f + i * 2);
		XMMATRIX dersborleft = XMMatrixTranslation(8.5f, 4.5f, -12.0f + i * 2) *XMMatrixRotationY(3.1416);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}


	//right
	auto boxRitem2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem2->World, XMMatrixTranslation(4.0f, 0.5f, 0.0f)* XMMatrixScaling(2.0f, 4.0f, 28.0f));

	boxRitem2->ObjCBIndex = objCBIndex++;
	boxRitem2->Geo = mGeometries["shapeGeo"].get();
	boxRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem2->IndexCount = boxRitem2->Geo->DrawArgs["box"].IndexCount;
	boxRitem2->StartIndexLocation = boxRitem2->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem2->BaseVertexLocation = boxRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem2));

	for (int i = 0; i < 12; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(8.5f, 4.5f, -11.0f + i * 2);
		XMMATRIX dersborleft = XMMatrixTranslation(-8.5f, 4.5f, -12.0f + i * 2) * XMMatrixRotationY(3.1416);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}

	//backwall
	auto backWall = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&backWall->World, XMMatrixTranslation(0.0f, 0.5f, 6.5f)* XMMatrixScaling(14.0f, 4.0f, 2.0f));

	backWall->ObjCBIndex = objCBIndex++;
	backWall->Geo = mGeometries["shapeGeo"].get();
	backWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backWall->IndexCount = backWall->Geo->DrawArgs["box"].IndexCount;
	backWall->StartIndexLocation = backWall->Geo->DrawArgs["box"].StartIndexLocation;
	backWall->BaseVertexLocation = backWall->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(backWall));

	for (int i = 0; i < 7; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(13.5f, 4.5f, -5.0f + i * 2)*XMMatrixRotationY(-3.1416 / 2);
		XMMATRIX dersborleft = XMMatrixTranslation(-13.5f, 4.5f, 6.0f - i * 2) * XMMatrixRotationY(3.1416/2);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}

	auto FrontWall = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&FrontWall->World, XMMatrixTranslation(0.95f, 0.5f, -6.5f)* XMMatrixScaling(5.0f, 4.0f, 2.0f));

	FrontWall->ObjCBIndex = objCBIndex++;
	FrontWall->Geo = mGeometries["shapeGeo"].get();
	FrontWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FrontWall->IndexCount = FrontWall->Geo->DrawArgs["box"].IndexCount;
	FrontWall->StartIndexLocation = FrontWall->Geo->DrawArgs["box"].StartIndexLocation;
	FrontWall->BaseVertexLocation = FrontWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(FrontWall));

	auto FrontWall2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&FrontWall2->World, XMMatrixTranslation(-0.95f, 0.5f, -6.5f)* XMMatrixScaling(5.0f, 4.0f, 2.0f));

	FrontWall2->ObjCBIndex = objCBIndex++;
	FrontWall2->Geo = mGeometries["shapeGeo"].get();
	FrontWall2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FrontWall2->IndexCount = FrontWall2->Geo->DrawArgs["box"].IndexCount;
	FrontWall2->StartIndexLocation = FrontWall2->Geo->DrawArgs["box"].StartIndexLocation;
	FrontWall2->BaseVertexLocation = FrontWall2->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(FrontWall2));

	for (int i = 0; i < 3; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(-13.5f, 4.5f, -8.0f + i * 2) * XMMatrixRotationY(-3.1416 / 2);
		XMMATRIX dersborleft = XMMatrixTranslation(13.5f, 4.5f, 7.0f - i * 2) * XMMatrixRotationY(3.1416 / 2);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}

	for (int i = 0; i < 3; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(-13.5f, 4.5f, 4.0f + i * 2) * XMMatrixRotationY(-3.1416 / 2);
		XMMATRIX dersborleft = XMMatrixTranslation(13.5f, 4.5f, -3.0f - i * 2) * XMMatrixRotationY(3.1416 / 2);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}

	//stairs

	auto Stairs = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&Stairs->World, XMMatrixTranslation(0.0f, 0.5f, -14.5f) * XMMatrixScaling(4.5f, 0.5f, 1.0f) );

	Stairs->ObjCBIndex = objCBIndex++;
	Stairs->Geo = mGeometries["shapeGeo"].get();
	Stairs->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Stairs->IndexCount = Stairs->Geo->DrawArgs["wedge"].IndexCount;
	Stairs->StartIndexLocation = Stairs->Geo->DrawArgs["wedge"].StartIndexLocation;
	Stairs->BaseVertexLocation = Stairs->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Stairs));

	auto Stairs2 = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&Stairs2->World, XMMatrixTranslation(0.0f, 0.5f, 11.5f)* XMMatrixScaling(4.5f, 0.5f, 1.0f)* XMMatrixRotationY(3.1416));

	Stairs2->ObjCBIndex = objCBIndex++;
	Stairs2->Geo = mGeometries["shapeGeo"].get();
	Stairs2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Stairs2->IndexCount = Stairs2->Geo->DrawArgs["wedge"].IndexCount;
	Stairs2->StartIndexLocation = Stairs2->Geo->DrawArgs["wedge"].StartIndexLocation;
	Stairs2->BaseVertexLocation = Stairs2->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Stairs2));

	auto middlestairs = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&middlestairs->World, XMMatrixTranslation(0.0f, 0.5f, -6.5f)* XMMatrixScaling(4.5f, 0.5f, 2.0f));

	middlestairs->ObjCBIndex = objCBIndex++;
	middlestairs->Geo = mGeometries["shapeGeo"].get();
	middlestairs->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	middlestairs->IndexCount = middlestairs->Geo->DrawArgs["box"].IndexCount;
	middlestairs->StartIndexLocation = middlestairs->Geo->DrawArgs["box"].StartIndexLocation;
	middlestairs->BaseVertexLocation = middlestairs->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(middlestairs));

	//Garden


	for (int i = 0; i < 3; i++)
	{
		auto bushes = std::make_unique<RenderItem>();
		auto bushes2 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&bushes->World, XMMatrixTranslation(-2.0f, 0.0f, -5.0f + i * 2)* XMMatrixScaling(2.0f, 2.0f+i, 2.0f));
		XMStoreFloat4x4(&bushes2->World, XMMatrixTranslation(2.0f, 0.0f, -5.0f + i * 2)* XMMatrixScaling(2.0f, 2.0f+i, 2.0f));

		bushes->ObjCBIndex = objCBIndex++;
		bushes->Geo = mGeometries["shapeGeo"].get();
		bushes->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		bushes->IndexCount = bushes->Geo->DrawArgs["sphere"].IndexCount;
		bushes->StartIndexLocation = bushes->Geo->DrawArgs["sphere"].StartIndexLocation;
		bushes->BaseVertexLocation = bushes->Geo->DrawArgs["sphere"].BaseVertexLocation;

		bushes2->ObjCBIndex = objCBIndex++;
		bushes2->Geo = mGeometries["shapeGeo"].get();
		bushes2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		bushes2->IndexCount = bushes2->Geo->DrawArgs["sphere"].IndexCount;
		bushes2->StartIndexLocation = bushes2->Geo->DrawArgs["sphere"].StartIndexLocation;
		bushes2->BaseVertexLocation = bushes2->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(bushes));
		mAllRitems.push_back(std::move(bushes2));
	}

	//house

	auto house = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&house->World, XMMatrixTranslation(0.0f, 0.5f, 0.5f) * XMMatrixScaling(13.0f, 8.0f, 11.0f));

	house->ObjCBIndex = objCBIndex++;
	house->Geo = mGeometries["shapeGeo"].get();
	house->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	house->IndexCount = house->Geo->DrawArgs["box"].IndexCount;
	house->StartIndexLocation = house->Geo->DrawArgs["box"].StartIndexLocation;
	house->BaseVertexLocation = house->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(house));

	auto Door = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&Door->World, XMMatrixTranslation(0.0f, 0.5f, -1.0f)* XMMatrixScaling(4.0f, 5.0f, 0.1f));

	Door->ObjCBIndex = objCBIndex++;
	Door->Geo = mGeometries["shapeGeo"].get();
	Door->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Door->IndexCount = Door->Geo->DrawArgs["wedge"].IndexCount;
	Door->StartIndexLocation = Door->Geo->DrawArgs["wedge"].StartIndexLocation;
	Door->BaseVertexLocation = Door->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Door));

	//top house
	auto tophouse = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&tophouse->World, XMMatrixTranslation(0.0f, 5.5f, 1.0f)* XMMatrixScaling(6.5f, 2.0f, 5.5f));

	tophouse->ObjCBIndex = objCBIndex++;
	tophouse->Geo = mGeometries["shapeGeo"].get();
	tophouse->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	tophouse->IndexCount = tophouse->Geo->DrawArgs["pyramid"].IndexCount;
	tophouse->StartIndexLocation = tophouse->Geo->DrawArgs["pyramid"].StartIndexLocation;
	tophouse->BaseVertexLocation = tophouse->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(tophouse));

	auto diamondRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&diamondRitem->World, XMMatrixTranslation(0.0f, 4.5f, 0.0f)* XMMatrixScaling(2.0f, 2.0f, 2.0f));

	diamondRitem->ObjCBIndex = objCBIndex++;
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem));

	for (int i = 0; i < 5; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(6.0f, 8.5f, 0.5f + i * 2);
		XMMATRIX dersborleft = XMMatrixTranslation(-6.0f, 8.5f, -1.5f - i * 2) * XMMatrixRotationY(3.1416);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}
	for (int i = 0; i < 5; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(-6.0f, 8.5f, 0.5f + i * 2);
		XMMATRIX dersborleft = XMMatrixTranslation(6.0f, 8.5f, -1.5f - i * 2) * XMMatrixRotationY(3.1416);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}
	for (int i = 0; i < 6; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(10.5f, 8.5f, -5.5f + i * 2) * XMMatrixRotationY(-3.1416 / 2);
		XMMATRIX dersborleft = XMMatrixTranslation(-10.5f, 8.5f, 4.5f - i * 2) * XMMatrixRotationY(3.1416 / 2);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}
	for (int i = 0; i < 6; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMMATRIX bordersleft = XMMatrixTranslation(0.5f, 8.5f, -5.5f + i * 2) * XMMatrixRotationY(-3.1416 / 2);
		XMMATRIX dersborleft = XMMatrixTranslation(-0.5f, 8.5f, 4.5f - i * 2) * XMMatrixRotationY(3.1416 / 2);

		XMStoreFloat4x4(&borders->World, bordersleft);
		XMStoreFloat4x4(&dersbor->World, dersborleft);


		borders->ObjCBIndex = objCBIndex++;
		borders->Geo = mGeometries["shapeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;


		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Geo = mGeometries["shapeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mAllRitems.push_back(std::move(borders));

		mAllRitems.push_back(std::move(dersbor));
	}
	//grid
	auto gridRitem = std::make_unique<RenderItem>();

	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mAllRitems.push_back(std::move(gridRitem));

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	// For each render item...

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.

		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;

		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());

		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

