/************************************************************************************
Filename    :   Win32_RoomTiny_AppRendered.cpp
Content     :   First-person view test application for Oculus Rift
Created     :   October 20th, 2014
Authors     :   Tom Heath
Copyright   :   Copyright 2012 Oculus, Inc. All Rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*************************************************************************************/

// Additional structures needed for app-rendered
Scene      * pLatencyTestScene;
DataBuffer * MeshVBs[2] = { NULL, NULL };
DataBuffer * MeshIBs[2] = { NULL, NULL };
ShaderFill * DistortionShaderFill[2];

//-----------------------------------------------------------------------------------
void MakeNewDistortionMeshes(float overrideEyeRelief)
{
    for (int eye=0; eye<2; eye++)
    {
        if (MeshVBs[eye]) delete MeshVBs[eye];
        if (MeshIBs[eye]) delete MeshIBs[eye];

        ovrDistortionMesh meshData;
        ovrHmd_CreateDistortionMeshDebug(HMD, (ovrEyeType)eye, EyeRenderDesc[eye].Fov,
                                         ovrDistortionCap_Chromatic | ovrDistortionCap_TimeWarp,
                                         &meshData, overrideEyeRelief);
        MeshVBs[eye] = new DataBuffer(D3D11_BIND_VERTEX_BUFFER, meshData.pVertexData,
                                      sizeof(ovrDistortionVertex)*meshData.VertexCount);
        MeshIBs[eye] = new DataBuffer(D3D11_BIND_INDEX_BUFFER, meshData.pIndexData,
                                      sizeof(unsigned short)* meshData.IndexCount);
        ovrHmd_DestroyDistortionMesh(&meshData);
    }
}

//-----------------------------------------------------------------------------------------
void APP_RENDER_SetupGeometryAndShaders(void)
    {
    D3D11_INPUT_ELEMENT_DESC VertexDesc[] = {
        { "Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "Position", 1, DXGI_FORMAT_R32_FLOAT,    0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "Position", 2, DXGI_FORMAT_R32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TexCoord", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TexCoord", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 } };

    char* vShader =
        "float2   EyeToSourceUVScale, EyeToSourceUVOffset;                                      \n"
        "float4x4 EyeRotationStart,   EyeRotationEnd;                                           \n"
        "float2   TimewarpTexCoord(float2 TexCoord, float4x4 rotMat)                            \n"
        "{                                                                                      \n"
             // Vertex inputs are in TanEyeAngle space for the R,G,B channels (i.e. after chromatic 
             // aberration and distortion). These are now "real world" vectors in direction (x,y,1) 
             // relative to the eye of the HMD.    Apply the 3x3 timewarp rotation to these vectors.
        "    float3 transformed = float3( mul ( rotMat, float4(TexCoord.xy, 1, 1) ).xyz);       \n"
             // Project them back onto the Z=1 plane of the rendered images.
        "    float2 flattened = (transformed.xy / transformed.z);                               \n"
             // Scale them into ([0,0.5],[0,1]) or ([0.5,0],[0,1]) UV lookup space (depending on eye)
        "    return(EyeToSourceUVScale * flattened + EyeToSourceUVOffset);                      \n"
        "}                                                                                      \n"
        "void main(in float2  Position   : POSITION,  in float timewarpLerpFactor : POSITION1,  \n"
        "          in float   Vignette   : POSITION2, in float2 TexCoord0         : TEXCOORD0,  \n"
        "          in float2  TexCoord1  : TEXCOORD1, in float2 TexCoord2         : TEXCOORD2,  \n"
        "          out float4 oPosition  : SV_Position,                                         \n"
        "          out float2 oTexCoord0 : TEXCOORD0, out float2 oTexCoord1 : TEXCOORD1,        \n"
        "          out float2 oTexCoord2 : TEXCOORD2, out float  oVignette  : TEXCOORD3)        \n"
        "{                                                                                      \n"
        "    float4x4 lerpedEyeRot = lerp(EyeRotationStart, EyeRotationEnd, 0);\n"
        "    oTexCoord0  = TimewarpTexCoord(TexCoord0,lerpedEyeRot);                            \n"
        "    oTexCoord1  = TimewarpTexCoord(TexCoord1,lerpedEyeRot);                            \n"
        "    oTexCoord2  = TimewarpTexCoord(TexCoord2,lerpedEyeRot);                            \n"
        "    oPosition = float4(Position.xy, 0.5, 1.0);    oVignette = Vignette;                \n"
        "}";

    char* pShader =
        "Texture2D Texture   : register(t0);                                                    \n"
        "SamplerState Linear : register(s0);                                                    \n"
        "float4 main(in float4 oPosition  : SV_Position,  in float2 oTexCoord0 : TEXCOORD0,     \n"
        "            in float2 oTexCoord1 : TEXCOORD1,    in float2 oTexCoord2 : TEXCOORD2,     \n"
        "            in float  oVignette  : TEXCOORD3)    : SV_Target                           \n"
        "{                                                                                      \n"
             // 3 samples for fixing chromatic aberrations
        "    float R = Texture.Sample(Linear, oTexCoord0.xy).r;                                 \n"
			 //only use the red channel's distortion (oTexCoord0) to match what the camera should see on the display (i.e. no chromatic aberration)
        "    float G = Texture.Sample(Linear, oTexCoord1.xy).g;                                 \n" 
        "    float B = Texture.Sample(Linear, oTexCoord2.xy).b;                                 \n"
        "    return (oVignette*float4(R,G,B,1));                                                \n"
        "}";

	    char* pRenderDistortionMapShader =
        "Texture2D Texture   : register(t0);                                                    \n"
        "SamplerState Linear : register(s0);                                                    \n"
        "float4 main(in float4 oPosition  : SV_Position,  in float2 oTexCoord0 : TEXCOORD0,     \n"
        "            in float2 oTexCoord1 : TEXCOORD1,    in float2 oTexCoord2 : TEXCOORD2,     \n"
        "            in float  oVignette  : TEXCOORD3)    : SV_Target                           \n"
        "{                                                                                      \n"
		"    float R = oTexCoord0.x;                                 \n"
        "    float G = oTexCoord0.y;                                 \n"
        "    float B = 0;                                 \n"

        "    return (oVignette*float4(R,G,B,1));                                                \n"
        "}";

    DistortionShaderFill[0]= new ShaderFill(VertexDesc,6,vShader,pShader,pEyeRenderTexture[0],false);
    DistortionShaderFill[1]= new ShaderFill(VertexDesc,6,vShader,pShader,pEyeRenderTexture[1],false);

    // Create eye render descriptions
    for (int eye = 0; eye<2; eye++)
        EyeRenderDesc[eye] = ovrHmd_GetRenderDesc(HMD, (ovrEyeType)eye, HMD->DefaultEyeFov[eye]);

    MakeNewDistortionMeshes();

    // A model for the latency test colour in the corner
    pLatencyTestScene = new Scene();

    ExampleFeatures3(VertexDesc, 6, vShader, pShader);    
}

HANDLE hFile;

void OpenRenderFile()
{
	hFile = CreateFile(L"C:\\Renders\\Lazarus",                // name of the write
					GENERIC_WRITE,          // open for writing
					0,                      // do not share
					NULL,                   // default security
					CREATE_NEW,             // create new file only
					FILE_ATTRIBUTE_NORMAL,  // normal file
					NULL);                  // no attr. template
}

void CloseRenderFile()
{
	CloseHandle(hFile);
}

//----------------------------------------------------------------------------------
BYTE* APP_RENDER_DistortAndPresent()
{
    bool waitForGPU = true;
    
	/* Create a texture and a render target which directs the render output into this texture */

	// Create the texture to receive the distortion map - it should be high precision

	static ID3D11Texture2D* renderTargetTextureMap;
	static ID3D11RenderTargetView* renderTargetViewMap;	

	static D3D11_TEXTURE2D_DESC textureDesc;
	static D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;

	static bool renderTargetInitialised = false;

	if(!renderTargetInitialised){
		// Initialize the  texture description.
		ZeroMemory(&textureDesc, sizeof(textureDesc));

		// Setup the texture description.
		// We will have our map be a square
		// We will need to have this texture bound as a render target AND a shader resource
		textureDesc.Width = DX11.WinSize.w;
		textureDesc.Height = DX11.WinSize.h;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; //DXGI_FORMAT_R32G32B32A32_FLOAT for saving the distortion map
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		textureDesc.CPUAccessFlags = 0;
		textureDesc.MiscFlags = 0;

		renderTargetViewDesc.Format = textureDesc.Format;
		renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		renderTargetViewDesc.Texture2D.MipSlice = 0;

		// Create the texture
		DX11.Device->CreateTexture2D(&textureDesc, NULL, &renderTargetTextureMap);

		// Create the render target view.
		DX11.Device->CreateRenderTargetView(renderTargetTextureMap, &renderTargetViewDesc, &renderTargetViewMap);

		renderTargetInitialised = true;
	}

	/**/

	/*That is sufficient to render to, but a render target texture cannot be mapped to the CPU. Create a second texture that can. The first texture shall be copied into this.*/

	// create a staging texture that can be read by the cpu

	static ID3D11Texture2D* renderTargetTextureMapStaging;
	static D3D11_TEXTURE2D_DESC stagingTextureDesc;

	if(!renderTargetInitialised){
		ZeroMemory(&stagingTextureDesc, sizeof(stagingTextureDesc));

		stagingTextureDesc = textureDesc;
		stagingTextureDesc.Usage = D3D11_USAGE_STAGING;
		stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingTextureDesc.BindFlags = 0;

		DX11.Device->CreateTexture2D(&stagingTextureDesc, NULL, &renderTargetTextureMapStaging);

		renderTargetInitialised = true;
	}

	/**/

	/* Render the image - specifying our texture render target as the destination */

    // Clear screen
	DX11.ClearAndSetRenderTarget(renderTargetViewMap,
                                 DX11.MainDepthBuffer, Recti(0,0,DX11.WinSize.w,DX11.WinSize.h));

    // Render latency-tester square
    unsigned char latencyColor[3];
    if (ovrHmd_GetLatencyTest2DrawColor(HMD, latencyColor))
    {
        float       col[] = { latencyColor[0] / 255.0f, latencyColor[1] / 255.0f,
                              latencyColor[2] / 255.0f, 1 };
        Matrix4f    view;
        ovrFovPort  fov = { 1, 1, 1, 1 };
        Matrix4f    proj = ovrMatrix4f_Projection(fov, 0.15f, 2, true);

        pLatencyTestScene->Models[0]->Fill->VShader->SetUniform("NewCol", 4, col);
        pLatencyTestScene->Render(view, proj.Transposed());
    }

    // Render distorted eye buffers
    for (int eye=0; eye<2; eye++)  
    {
        ShaderFill * useShaderfill     = DistortionShaderFill[eye];
        ovrPosef   * useEyePose        = &EyeRenderPose[eye];
        float      * useYaw            = &YawAtRender[eye];
        double       debugTimeAdjuster = 0.0;

        ExampleFeatures4(eye,&useShaderfill,&useEyePose,&useYaw,&debugTimeAdjuster,&waitForGPU);

        // Get and set shader constants
        ovrVector2f UVScaleOffset[2];
        ovrHmd_GetRenderScaleAndOffset(EyeRenderDesc[eye].Fov,
                                       pEyeRenderTexture[eye]->Size, EyeRenderViewport[eye], UVScaleOffset);
        useShaderfill->VShader->SetUniform("EyeToSourceUVScale", 2, (float*)&UVScaleOffset[0]);
        useShaderfill->VShader->SetUniform("EyeToSourceUVOffset", 2, (float *)&UVScaleOffset[1]);

		//dont do any time warping since most of the time this design will play back prerecorded logs

		ovrMatrix4f    timeWarpMatrices[2];
		timeWarpMatrices[0] = Matrix4f::Identity();
		timeWarpMatrices[1] = Matrix4f::Identity();

        useShaderfill->VShader->SetUniform("EyeRotationStart", 16, (float *)&timeWarpMatrices[0]);
		useShaderfill->VShader->SetUniform("EyeRotationEnd",   16, (float *)&timeWarpMatrices[0]);

        // Perform distortion
        DX11.Render(useShaderfill, MeshVBs[eye], MeshIBs[eye], sizeof(ovrDistortionVertex), (int)MeshVBs[eye]->Size);
    }


	// Read the texture into memory

	bool render = false;
	if(render){

		DX11.Context->CopyResource(renderTargetTextureMapStaging, renderTargetTextureMap);

		D3D11_MAP eMapType = D3D11_MAP_READ;
		D3D11_MAPPED_SUBRESOURCE mappedResource;

		HRESULT mapResult = DX11.Context->Map(renderTargetTextureMapStaging, 0, eMapType, NULL, &mappedResource);

		BYTE* pYourBytes = (BYTE*)mappedResource.pData;
		unsigned int uiPitch = mappedResource.RowPitch;

		DWORD bytesWritten;
		WriteFile(hFile, pYourBytes, mappedResource.DepthPitch, &bytesWritten, NULL);

		DX11.Context->Unmap(renderTargetTextureMapStaging, 0);
	}

	/* So we can see what we are capturing, copy the rendered image into the back buffer and show it on the screen */

	//beware this will fail silently if the render target texture and the back buffer are not the same format!
	DX11.Context->CopyResource(DX11.BackBuffer, renderTargetTextureMap);

    DX11.SwapChain->Present(true, 0); // Vsync enabled

	/**/

    // Only flush GPU for ExtendDesktop; not needed in Direct App Rendering with Oculus driver.
    if (HMD->HmdCaps & ovrHmdCap_ExtendDesktop)
    {
        DX11.Context->Flush();
        if (waitForGPU) 
            DX11.WaitUntilGpuIdle();
    }
    DX11.OutputFrameTime(ovr_GetTimeInSeconds());
    ovrHmd_EndFrameTiming(HMD);

	return 0;
}
