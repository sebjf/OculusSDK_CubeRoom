/************************************************************************************
Filename    :   Win32_DX11AppUtil.h
Content     :   D3D11 and Application/Window setup functionality for RoomTiny
Created     :   October 20th, 2014
Author      :   Tom Heath
Copyright   :   Copyright 2014 Oculus, Inc. All Rights reserved.
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
 
#include "Kernel/OVR_Math.h"
#include <d3d11.h>
#include <D3DX11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <comdef.h>
using namespace OVR;

//---------------------------------------------------------------------
struct DirectX11
{
    HWND                     Window;
    bool                     Key[256];
    Sizei                    WinSize;
    struct ImageBuffer     * MainDepthBuffer;
    ID3D11Device           * Device;
    ID3D11DeviceContext    * Context;
    IDXGISwapChain         * SwapChain;
    ID3D11Texture2D        * BackBuffer;
    ID3D11RenderTargetView * BackBufferRT;
    struct DataBuffer      * UniformBufferGen;

    bool InitWindowAndDevice(HINSTANCE hinst, Recti vp,  bool windowed);
    void ClearAndSetRenderTarget(ID3D11RenderTargetView * rendertarget, ImageBuffer * depthbuffer, Recti vp);
    void Render(struct ShaderFill* fill, DataBuffer* vertices, DataBuffer* indices,UINT stride, int count);

    bool IsAnyKeyPressed() const
    {
        for (unsigned i = 0; i < (sizeof(Key) / sizeof(Key[0])); i++)        
            if (Key[i]) return true;        
        return false;
    }

    void SetMaxFrameLatency(int value)
    {
        IDXGIDevice1* DXGIDevice1 = NULL;
        HRESULT hr = Device->QueryInterface( __uuidof( IDXGIDevice1 ), (void**)&DXGIDevice1 );
        if ( FAILED( hr ) | ( DXGIDevice1 == NULL ) ) return;
        DXGIDevice1->SetMaximumFrameLatency( value ); 
        DXGIDevice1->Release();
    }

    void WaitUntilGpuIdle()
    {
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        ID3D11Query *  query;
        BOOL           done = FALSE;
        if (Device->CreateQuery(&queryDesc, &query) == S_OK)
        { 
            Context->End(query);
            while(!done && !FAILED(Context->GetData(query, &done, sizeof(BOOL), 0)));
        }
    }

    void HandleMessages()
    {
        MSG msg;
        if (PeekMessage(&msg,NULL,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessage(&msg);}
    }

    void OutputFrameTime(double currentTime)
    {
        static double lastTime = 0;
        char tempString[100];
        sprintf_s(tempString,"Frame time = %0.2f ms\n",(currentTime-lastTime)*1000.0f);
        OutputDebugStringA(tempString);
        lastTime = currentTime;
    }

	void ReleaseWindow(HINSTANCE hinst)
    {
        DestroyWindow(DX11.Window); UnregisterClassW(L"OVRAppWindow", hinst);
    };

} DX11;

//--------------------------------------------------------------------------
struct Shader 
{
    ID3D11VertexShader * D3DVert;
    ID3D11PixelShader  * D3DPix;
    unsigned char      * UniformData;
    int                  UniformsSize;

    struct Uniform  {
        char Name[40];
        int Offset, Size;
    };

    int                  numUniformInfo;
    Uniform              UniformInfo[10];
 
    Shader(ID3D10Blob* s, int which_type) : numUniformInfo(0)
    {
        if (which_type==0) DX11.Device->CreateVertexShader(s->GetBufferPointer(),s->GetBufferSize(), NULL, &D3DVert);
        else               DX11.Device->CreatePixelShader(s->GetBufferPointer(),s->GetBufferSize(), NULL, &D3DPix);
 
        ID3D11ShaderReflection* ref;
        D3DReflect(s->GetBufferPointer(), s->GetBufferSize(), IID_ID3D11ShaderReflection, (void**) &ref);
        ID3D11ShaderReflectionConstantBuffer* buf = ref->GetConstantBufferByIndex(0);
        D3D11_SHADER_BUFFER_DESC bufd;
        if (FAILED(buf->GetDesc(&bufd))) return;
     
        for(unsigned i = 0; i < bufd.Variables; i++)
        {
            ID3D11ShaderReflectionVariable* var = buf->GetVariableByIndex(i);
            D3D11_SHADER_VARIABLE_DESC vd;
            var->GetDesc(&vd);
            Uniform u;
            strcpy_s(u.Name, (const char*)vd.Name);;
            u.Offset = vd.StartOffset;
            u.Size   = vd.Size;
            UniformInfo[numUniformInfo++]=u;
        }
        UniformsSize = bufd.Size;
        UniformData  = (unsigned char*)OVR_ALLOC(bufd.Size);
    }

    void SetUniform(const char* name, int n, const float* v)
    {
        for (int i=0;i<numUniformInfo;i++)
        {
            if (!strcmp(UniformInfo[i].Name,name))
            {
                memcpy(UniformData + UniformInfo[i].Offset, v, n * sizeof(float));
                return;
            }
        }
    }
};
//------------------------------------------------------------
struct ImageBuffer
{
    ID3D11Texture2D *            Tex;
    ID3D11ShaderResourceView *   TexSv;
    ID3D11RenderTargetView *     TexRtv;
    ID3D11DepthStencilView *     TexDsv;
    Sizei                        Size;

	ImageBuffer::ImageBuffer(LPCWSTR image_filename)
	{
		HRESULT res = D3DX11CreateTextureFromFile(DX11.Device, image_filename, NULL, NULL, (ID3D11Resource**)&Tex, NULL);
		if(res != S_OK){
			_com_error error(res);
			printf("Error loading texture - %s\n", error.ErrorMessage());
		}

		res = DX11.Device->CreateShaderResourceView(Tex, NULL, &TexSv);
		if(res != S_OK)
		{
			_com_error error(res);
			printf("Error creating resource view - %s\n", error.ErrorMessage());
		}
	}

    ImageBuffer::ImageBuffer(bool rendertarget, bool depth, Sizei size, int mipLevels = 1,
                             unsigned char * data = NULL) : Size(size)
    {
        D3D11_TEXTURE2D_DESC dsDesc;
        dsDesc.Width     = size.w;
        dsDesc.Height    = size.h;
        dsDesc.MipLevels = mipLevels;
        dsDesc.ArraySize = 1;
        dsDesc.Format    = depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        dsDesc.SampleDesc.Count = 1;
        dsDesc.SampleDesc.Quality = 0;
        dsDesc.Usage     = D3D11_USAGE_DEFAULT;
        dsDesc.CPUAccessFlags = 0;
        dsDesc.MiscFlags      = 0;
        dsDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (rendertarget &&  depth) dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (rendertarget && !depth) dsDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
        DX11.Device->CreateTexture2D(&dsDesc, NULL, &Tex);
        DX11.Device->CreateShaderResourceView(Tex, NULL, &TexSv);
        
        if (rendertarget &&  depth) DX11.Device->CreateDepthStencilView(Tex, NULL, &TexDsv);
        if (rendertarget && !depth) DX11.Device->CreateRenderTargetView(Tex, NULL, &TexRtv);
 
        if (data) // Note data is trashed, as is width and height
        {
            for (int level=0; level < mipLevels; level++)
            {
                DX11.Context->UpdateSubresource(Tex, level, NULL, data, size.w * 4, size.h * 4);
                for(int j = 0; j < (size.h & ~1); j += 2)
                {
                    const uint8_t* psrc = data + (size.w * j * 4);
                    uint8_t*       pdest = data + ((size.w >> 1) * (j >> 1) * 4);
                    for(int i = 0; i < size.w >> 1; i++, psrc += 8, pdest += 4)
                    {
                        pdest[0] = (((int)psrc[0]) + psrc[4] + psrc[size.w * 4 + 0] + psrc[size.w * 4 + 4]) >> 2;
                        pdest[1] = (((int)psrc[1]) + psrc[5] + psrc[size.w * 4 + 1] + psrc[size.w * 4 + 5]) >> 2;
                        pdest[2] = (((int)psrc[2]) + psrc[6] + psrc[size.w * 4 + 2] + psrc[size.w * 4 + 6]) >> 2;
                        pdest[3] = (((int)psrc[3]) + psrc[7] + psrc[size.w * 4 + 3] + psrc[size.w * 4 + 7]) >> 2;
                    }
                }
                size.w >>= 1;  size.h >>= 1;
            }
        }
    }
};
//-----------------------------------------------------
struct ShaderFill
{
    Shader             * VShader, *PShader;
    ImageBuffer        * OneTexture;
    ID3D11InputLayout  * InputLayout;
    ID3D11SamplerState * SamplerState;

    ShaderFill::ShaderFill(D3D11_INPUT_ELEMENT_DESC * VertexDesc, int numVertexDesc,
                           char* vertexShader, char* pixelShader, ImageBuffer * t, bool wrap=1)
        : OneTexture(t)
    {
        ID3D10Blob *blobData;
        D3DCompile(vertexShader, strlen(vertexShader), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &blobData, NULL);
        VShader = new Shader(blobData,0);
        DX11.Device->CreateInputLayout(VertexDesc, numVertexDesc,
                                       blobData->GetBufferPointer(), blobData->GetBufferSize(), &InputLayout);
        D3DCompile(pixelShader, strlen(pixelShader), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &blobData, NULL);
        PShader  = new Shader(blobData,1);

        D3D11_SAMPLER_DESC ss; memset(&ss, 0, sizeof(ss));
        ss.AddressU = ss.AddressV = ss.AddressW = wrap ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_BORDER;
        ss.Filter        = D3D11_FILTER_ANISOTROPIC;
        ss.MaxAnisotropy = 8;
        ss.MaxLOD        = 15;
        DX11.Device->CreateSamplerState(&ss, &SamplerState);
    }
};

//----------------------------------------------------------------
struct DataBuffer 
{
    ID3D11Buffer * D3DBuffer;
    size_t         Size;

    DataBuffer(D3D11_BIND_FLAG use, const void* buffer, size_t size) : Size(size)
    {
        D3D11_BUFFER_DESC desc;   memset(&desc, 0, sizeof(desc));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = use;
        desc.ByteWidth = (unsigned)size;
        D3D11_SUBRESOURCE_DATA sr;
        sr.pSysMem = buffer;
        sr.SysMemPitch = sr.SysMemSlicePitch = 0;
        DX11.Device->CreateBuffer(&desc, buffer ? &sr : NULL, &D3DBuffer);
    }
    void Refresh(const void* buffer, size_t size)
    {
        D3D11_MAPPED_SUBRESOURCE map;
        DX11.Context->Map(D3DBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);   
        memcpy((void *)map.pData, buffer, size);
        DX11.Context->Unmap(D3DBuffer, 0);
    }
};

//---------------------------------------------------------------------------
struct Model 
{
    struct Color
    { 
        unsigned char R,G,B,A;
        
        Color(unsigned char r = 0,unsigned char g=0,unsigned char b=0, unsigned char a = 0xff)
            : R(r), G(g), B(b), A(a) 
        { }
    };
    struct Vertex
    { 
        Vector3f  Pos;
        Color     C;
        float     U, V;
    };

    Vector3f     Pos;
    Quatf        Rot;
    Matrix4f     Mat;
    int          numVertices, numIndices;
    Vertex       Vertices[2000]; //Note fixed maximum
    uint16_t     Indices[2000];
    ShaderFill * Fill;
    DataBuffer * VertexBuffer, * IndexBuffer;  
   
    Model(Vector3f arg_pos, ShaderFill * arg_Fill ) { numVertices=0;numIndices=0;Pos = arg_pos; Fill = arg_Fill; }
    Matrix4f& GetMatrix()                           { Mat = Matrix4f(Rot); Mat = Matrix4f::Translation(Pos) * Mat; return Mat;   }
    void AddVertex(const Vertex& v)                 { Vertices[numVertices++] = v; OVR_ASSERT(numVertices<2000); }
    void AddIndex(uint16_t a)                       { Indices[numIndices++] = a;   OVR_ASSERT(numIndices<2000);  }

    void AllocateBuffers()
    {
        VertexBuffer = new DataBuffer(D3D11_BIND_VERTEX_BUFFER, &Vertices[0], numVertices * sizeof(Vertex));
        IndexBuffer  = new DataBuffer(D3D11_BIND_INDEX_BUFFER, &Indices[0], numIndices * 2);
    }

	void Model::AddPlane(float scale)
	{
		Vertex v1;
		v1.Pos = Vector3f(-1,1,0) * scale;
		v1.U = 0;
		v1.V = 0;
		v1.C = Color(255,255,255,255);

		Vertex v2;
		v2.Pos = Vector3f(1,1,0) * scale;
		v2.U = 1;
		v2.V = 0;
		v2.C = Color(255,255,255,255);

		Vertex v3;
		v3.Pos = Vector3f(1,-1,0) * scale;
		v3.U = 1;
		v3.V = 1;
		v3.C = Color(255,255,255,255);
		
		Vertex v4;
		v4.Pos = Vector3f(-1,-1,0) * scale;
		v4.U = 0;
		v4.V = 1;
		v4.C = Color(255,255,255,255);

		AddVertex(v1);
		AddVertex(v2);
		AddVertex(v3);
		AddVertex(v4);

		AddIndex(0);
		AddIndex(1);
		AddIndex(2);
		AddIndex(2);
		AddIndex(3);
		AddIndex(0);
	}

    void Model::AddSolidColorBox(float x1, float y1, float z1, float x2, float y2, float z2, Color c)
    {
        Vector3f Vert[][2] =
        {   Vector3f(x1, y2, z1), Vector3f(z1, x1),  Vector3f(x2, y2, z1), Vector3f(z1, x2),
            Vector3f(x2, y2, z2), Vector3f(z2, x2),  Vector3f(x1, y2, z2), Vector3f(z2, x1),
            Vector3f(x1, y1, z1), Vector3f(z1, x1),  Vector3f(x2, y1, z1), Vector3f(z1, x2),
            Vector3f(x2, y1, z2), Vector3f(z2, x2),  Vector3f(x1, y1, z2), Vector3f(z2, x1),
            Vector3f(x1, y1, z2), Vector3f(z2, y1),  Vector3f(x1, y1, z1), Vector3f(z1, y1),
            Vector3f(x1, y2, z1), Vector3f(z1, y2),  Vector3f(x1, y2, z2), Vector3f(z2, y2),
            Vector3f(x2, y1, z2), Vector3f(z2, y1),  Vector3f(x2, y1, z1), Vector3f(z1, y1),
            Vector3f(x2, y2, z1), Vector3f(z1, y2),  Vector3f(x2, y2, z2), Vector3f(z2, y2),
            Vector3f(x1, y1, z1), Vector3f(x1, y1),  Vector3f(x2, y1, z1), Vector3f(x2, y1),
            Vector3f(x2, y2, z1), Vector3f(x2, y2),  Vector3f(x1, y2, z1), Vector3f(x1, y2),
            Vector3f(x1, y1, z2), Vector3f(x1, y1),  Vector3f(x2, y1, z2), Vector3f(x2, y1),
            Vector3f(x2, y2, z2), Vector3f(x2, y2),  Vector3f(x1, y2, z2), Vector3f(x1, y2), };

        uint16_t CubeIndices[] = {0, 1, 3,     3, 1, 2,     5, 4, 6,     6, 4, 7,
                                  8, 9, 11,    11, 9, 10,   13, 12, 14,  14, 12, 15,
                                  16, 17, 19,  19, 17, 18,  21, 20, 22,  22, 20, 23 };
        
        for(int i = 0; i < 36; i++)
            AddIndex(CubeIndices[i] + (uint16_t) numVertices);

        for(int v = 0; v < 24; v++)
        {
            Vertex vvv; vvv.Pos = Vert[v][0];  vvv.U = Vert[v][1].x; vvv.V = Vert[v][1].y;
            float dist1 = (vvv.Pos - Vector3f(-2,4,-2)).Length();
            float dist2 = (vvv.Pos - Vector3f(3,4,-3)).Length();
            float dist3 = (vvv.Pos - Vector3f(-4,3,25)).Length();
            int   bri   = rand() % 160;
            float RRR   = c.R * (bri + 192.0f*(0.65f + 8/dist1 + 1/dist2 + 4/dist3)) / 255.0f;
            float GGG   = c.G * (bri + 192.0f*(0.65f + 8/dist1 + 1/dist2 + 4/dist3)) / 255.0f;
            float BBB   = c.B * (bri + 192.0f*(0.65f + 8/dist1 + 1/dist2 + 4/dist3)) / 255.0f;
            vvv.C.R = RRR > 255 ? 255: (unsigned char) RRR;
            vvv.C.G = GGG > 255 ? 255: (unsigned char) GGG;
            vvv.C.B = BBB > 255 ? 255: (unsigned char) BBB;
            AddVertex(vvv);
        }
    }
};
//------------------------------------------------------------------------- 
struct Scene  
{
    int     num_models;
    Model * Models[10];

    void    Add(Model * n)
    { Models[num_models++] = n; }



    Scene(int reducedVersion) : num_models(0) // Main world
    {
        D3D11_INPUT_ELEMENT_DESC ModelVertexDesc[] =
        {   {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Model::Vertex, Pos),   D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"Color",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, offsetof(Model::Vertex, C),     D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Model::Vertex, U),     D3D11_INPUT_PER_VERTEX_DATA, 0},    };

        char* VertexShaderSrc =
            "float4x4 Proj, View;"
            "float4 NewCol;"
            "void main(in  float4 Position  : POSITION,    in  float4 Color : COLOR0, in  float2 TexCoord  : TEXCOORD0,"
            "          out float4 oPosition : SV_Position, out float4 oColor: COLOR0, out float2 oTexCoord : TEXCOORD0)"
            "{   oPosition = mul(Proj, mul(View, Position)); oTexCoord = TexCoord; oColor = Color; }";
        char* PixelShaderSrc =
            "Texture2D Texture   : register(t0); SamplerState Linear : register(s0); "
            "float4 main(in float4 Position : SV_Position, in float4 Color: COLOR0, in float2 TexCoord : TEXCOORD0) : SV_Target"
            "{   return Color * Texture.Sample(Linear, TexCoord); }";
 
		//create the planes

		float scale = 10;
		float scale2 = 10;
		
		ShaderFill* params = new ShaderFill(ModelVertexDesc,3,VertexShaderSrc,PixelShaderSrc,new ImageBuffer(TEXT("C:\\Users\\sfriston\\Dropbox\\Investigations\\Environment Map Renderer\\lazarus_room\\maps\\posx.jpg")));
		Model* m = new Model(Vector3f(1,0,0) * scale, params);
		m->AddPlane(scale2);
		m->Rot = Quatf(Vector3f(0,1,0),DegreeToRad(90.0f));	
		m->AllocateBuffers();
		Add(m);

		params = new ShaderFill(ModelVertexDesc,3,VertexShaderSrc,PixelShaderSrc,new ImageBuffer(TEXT("C:\\Users\\sfriston\\Dropbox\\Investigations\\Environment Map Renderer\\lazarus_room\\maps\\negx.jpg")));
		m = new Model(Vector3f(-1,0,0) * scale, params);
		m->AddPlane(scale2);
		m->Rot = Quatf(Vector3f(0,1,0),DegreeToRad(-90.0f));	
		m->AllocateBuffers();
		Add(m);

		params = new ShaderFill(ModelVertexDesc,3,VertexShaderSrc,PixelShaderSrc,new ImageBuffer(TEXT("C:\\Users\\sfriston\\Dropbox\\Investigations\\Environment Map Renderer\\lazarus_room\\maps\\posy.jpg")));
		m = new Model(Vector3f(0,1,0) * scale, params);
		m->AddPlane(scale2);
		m->Rot = Quatf(Vector3f(1,0,0),DegreeToRad(-90.0f));	
		m->AllocateBuffers();
		Add(m);

		params = new ShaderFill(ModelVertexDesc,3,VertexShaderSrc,PixelShaderSrc,new ImageBuffer(TEXT("C:\\Users\\sfriston\\Dropbox\\Investigations\\Environment Map Renderer\\lazarus_room\\maps\\negy.jpg")));
		m = new Model(Vector3f(0,-1,0) * scale, params);
		m->AddPlane(scale2);
		m->Rot = Quatf(Vector3f(1,0,0),DegreeToRad(90.0f));	
		m->AllocateBuffers();
		Add(m);

		params = new ShaderFill(ModelVertexDesc,3,VertexShaderSrc,PixelShaderSrc,new ImageBuffer(TEXT("C:\\Users\\sfriston\\Dropbox\\Investigations\\Environment Map Renderer\\lazarus_room\\maps\\posz.jpg")));
		m = new Model(Vector3f(0,0,1) * scale, params);
		m->AddPlane(scale2);
		//m->Rot = Quatf(Vector3f(0,1,0),DegreeToRad(0.0f));	
		m->AllocateBuffers();
		Add(m);

		params = new ShaderFill(ModelVertexDesc,3,VertexShaderSrc,PixelShaderSrc,new ImageBuffer(TEXT("C:\\Users\\sfriston\\Dropbox\\Investigations\\Environment Map Renderer\\lazarus_room\\maps\\negz.jpg")));
		m = new Model(Vector3f(0,0,-1) * scale, params);
		m->AddPlane(scale2);
		m->Rot = Quatf(Vector3f(0,1,0),DegreeToRad(180.0f));	
		m->AllocateBuffers();
		Add(m);
     }

    // Simple latency box (keep similar vertex format and shader params same, for ease of code)
    Scene() : num_models(0) 
    {
        D3D11_INPUT_ELEMENT_DESC ModelVertexDesc[] =
        {   {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Model::Vertex, Pos),   D3D11_INPUT_PER_VERTEX_DATA, 0},};

        char* VertexShaderSrc =
            "float4x4 Proj, View;"
            "float4 NewCol;"
            "void main(in float4 Position : POSITION, out float4 oPosition : SV_Position, out float4 oColor: COLOR0)"
            "{   oPosition = mul(Proj, Position); oColor = NewCol; }";
        char* PixelShaderSrc =
            "float4 main(in float4 Position : SV_Position, in float4 Color: COLOR0) : SV_Target"
            "{   return Color ; }";
 
        Model* m = new Model(Vector3f(0,0,0),new ShaderFill(ModelVertexDesc,3,VertexShaderSrc,PixelShaderSrc,0));  
        float scale = 0.04f;  float extra_y = ((float)DX11.WinSize.w/(float)DX11.WinSize.h);
        m->AddSolidColorBox( 1-scale,  1-(scale*extra_y), -1, 1+scale,  1+(scale*extra_y), -1,  Model::Color(0,128,0)); 
        m->AllocateBuffers(); Add(m);
     }
 
    void Render(Matrix4f view, Matrix4f proj)
    {
        for(int i = 0; i < num_models; i++)
        {
            Matrix4f modelmat = Models[i]->GetMatrix();
            Matrix4f mat      = (view * modelmat).Transposed();

            Models[i]->Fill->VShader->SetUniform("View",16,(float *) &mat);
            Models[i]->Fill->VShader->SetUniform("Proj",16,(float *) &proj);

            DX11.Render(Models[i]->Fill, Models[i]->VertexBuffer,  Models[i]->IndexBuffer,
                        sizeof(Model::Vertex), Models[i]->numIndices);
        }
    }
};

//----------------------------------------------------------------------------------------------------------
void DirectX11::ClearAndSetRenderTarget(ID3D11RenderTargetView * rendertarget,
                                        ImageBuffer * depthbuffer, Recti vp)
{
    float black[] = {0, 0, 0, 1}; 
    Context->OMSetRenderTargets(1, &rendertarget, depthbuffer->TexDsv);
    Context->ClearRenderTargetView(rendertarget,black);
    Context->ClearDepthStencilView(depthbuffer->TexDsv,D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL ,1,0);
    D3D11_VIEWPORT D3Dvp;
    D3Dvp.Width    = (float)vp.w;    D3Dvp.Height   = (float)vp.h;
    D3Dvp.MinDepth = 0;              D3Dvp.MaxDepth = 1;
    D3Dvp.TopLeftX = (float)vp.x;    D3Dvp.TopLeftY = (float)vp.y;    
    Context->RSSetViewports(1, &D3Dvp);
}

//---------------------------------------------------------------
LRESULT CALLBACK SystemWindowProc(HWND arg_hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case(WM_NCCREATE):  DX11.Window = arg_hwnd;                     break;
        case WM_KEYDOWN:    DX11.Key[(unsigned)wp] = true;              break;
        case WM_KEYUP:      DX11.Key[(unsigned)wp] = false;             break;
        case WM_SETFOCUS:   SetCapture(DX11.Window); ShowCursor(FALSE); break;
        case WM_KILLFOCUS:  ReleaseCapture(); ShowCursor(TRUE);         break;
     }
    return DefWindowProc(DX11.Window, msg, wp, lp);
}

//-----------------------------------------------------------------------
bool DirectX11::InitWindowAndDevice(HINSTANCE hinst, Recti vp, bool windowed)
{
    WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
    wc.lpszClassName = L"OVRAppWindow";
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = SystemWindowProc;
    wc.cbWndExtra    = NULL;
    RegisterClassW(&wc);
     
    DWORD wsStyle     = WS_POPUP;
    DWORD sizeDivisor = 1;

    if (windowed)
    {
        wsStyle |= WS_OVERLAPPEDWINDOW; sizeDivisor = 2;
    }

    RECT winSize = { 0, 0, vp.w / sizeDivisor, vp.h / sizeDivisor};
    AdjustWindowRect(&winSize, wsStyle, false);
    Window = CreateWindowW(L"OVRAppWindow", L"OculusRoomTiny",wsStyle |WS_VISIBLE,
                         vp.x, vp.y, winSize.right-winSize.left, winSize.bottom-winSize.top,
                         NULL, NULL, hinst, NULL);

    if (!Window)
        return(false);
    if (windowed)
        WinSize = vp.GetSize();
    else
    {
        RECT rc; GetClientRect(Window, &rc);
        WinSize = Sizei(rc.right-rc.left,rc.bottom-rc.top);
    }
      
    IDXGIFactory * DXGIFactory;    
    IDXGIAdapter * Adapter;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&DXGIFactory))))
        return(false);
    if (FAILED(DXGIFactory->EnumAdapters(0, &Adapter)))
        return(false);
    if (FAILED(D3D11CreateDevice(	Adapter, 
									Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
									NULL, 
									0,//D3D11_CREATE_DEVICE_DEBUG, 
									NULL, 
									0, 
									D3D11_SDK_VERSION, &Device, NULL, &Context)))
        return(false);
    
    DXGI_SWAP_CHAIN_DESC scDesc;
    memset(&scDesc, 0, sizeof(scDesc));
    scDesc.BufferCount          = 2;
    scDesc.BufferDesc.Width     = WinSize.w;
    scDesc.BufferDesc.Height    = WinSize.h;
    scDesc.BufferDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator   = 0;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.BufferUsage          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow         = Window;
    scDesc.SampleDesc.Count     = 1;
    scDesc.SampleDesc.Quality   = 0;
    scDesc.Windowed             = windowed;
    scDesc.Flags                = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    scDesc.SwapEffect           = DXGI_SWAP_EFFECT_SEQUENTIAL;
 
    if (FAILED(DXGIFactory->CreateSwapChain(Device, &scDesc, &SwapChain)))               return(false);    
    if (FAILED(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&BackBuffer))) return(false) ;
	HRESULT r = Device->CreateRenderTargetView(BackBuffer, NULL, &BackBufferRT);
    if (FAILED(r))         return(false) ;

 
    MainDepthBuffer = new ImageBuffer(true,true, Sizei(WinSize.w, WinSize.h));
    Context->OMSetRenderTargets(1, &BackBufferRT, MainDepthBuffer->TexDsv);
    if (!windowed) SwapChain->SetFullscreenState(1, NULL);
    UniformBufferGen = new DataBuffer(D3D11_BIND_CONSTANT_BUFFER, NULL, 2000);// make sure big enough
 
    D3D11_RASTERIZER_DESC rs;
    memset(&rs, 0, sizeof(rs));
    rs.AntialiasedLineEnable = rs.DepthClipEnable = true;
    rs.CullMode              = D3D11_CULL_NONE;    
     rs.FillMode             = D3D11_FILL_SOLID;
    ID3D11RasterizerState *  Rasterizer = NULL;
    Device->CreateRasterizerState(&rs, &Rasterizer);
    Context->RSSetState(Rasterizer);
 
    D3D11_DEPTH_STENCIL_DESC dss;
    memset(&dss, 0, sizeof(dss));
    dss.DepthEnable    = true;
    dss.DepthFunc      = D3D11_COMPARISON_LESS; 
    dss.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ID3D11DepthStencilState * DepthState;
    Device->CreateDepthStencilState(&dss, &DepthState);
    Context->OMSetDepthStencilState(DepthState, 0);
    return(true);
}

//---------------------------------------------------------------------------------------------
void DirectX11::Render(ShaderFill* fill, DataBuffer* vertices, DataBuffer* indices,UINT stride, int count)
{
    Context->IASetInputLayout(fill->InputLayout);
    Context->IASetIndexBuffer(indices->D3DBuffer, DXGI_FORMAT_R16_UINT, 0);

    UINT offset = 0;
    Context->IASetVertexBuffers(0, 1, &vertices->D3DBuffer, &stride, &offset);
    UniformBufferGen->Refresh(fill->VShader->UniformData, fill->VShader->UniformsSize);
    Context->VSSetConstantBuffers(0, 1, &UniformBufferGen->D3DBuffer);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->VSSetShader(fill->VShader->D3DVert, NULL, 0);
    Context->PSSetShader(fill->PShader->D3DPix, NULL, 0);
    Context->PSSetSamplers(0, 1, &fill->SamplerState);
    if (fill->OneTexture)
        Context->PSSetShaderResources(0, 1, &fill->OneTexture->TexSv);
    Context->DrawIndexed(count, 0, 0);
}

//--------------------------------------------------------------------------------
// Due to be removed once the functionality is in the SDK
void UtilFoldExtraYawIntoTimewarpMatrix(Matrix4f * timewarpMatrix, Quatf eyePose, Quatf extraQuat)
{
       timewarpMatrix->M[0][1] = -timewarpMatrix->M[0][1];
       timewarpMatrix->M[0][2] = -timewarpMatrix->M[0][2];
       timewarpMatrix->M[1][0] = -timewarpMatrix->M[1][0];
       timewarpMatrix->M[2][0] = -timewarpMatrix->M[2][0];
       Quatf newtimewarpStartQuat = eyePose * extraQuat * (eyePose.Inverted())*(Quatf(*timewarpMatrix));
       *timewarpMatrix = Matrix4f(newtimewarpStartQuat);
       timewarpMatrix->M[0][1] = -timewarpMatrix->M[0][1];
       timewarpMatrix->M[0][2] = -timewarpMatrix->M[0][2];
       timewarpMatrix->M[1][0] = -timewarpMatrix->M[1][0];
       timewarpMatrix->M[2][0] = -timewarpMatrix->M[2][0];
}
