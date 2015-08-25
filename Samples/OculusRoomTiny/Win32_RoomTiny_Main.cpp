/************************************************************************************
Filename    :   Win32_RoomTiny_Main.cpp
Content     :   First-person view test application for Oculus Rift
Created     :   October 4, 2012
Authors     :   Tom Heath, Michael Antonov, Andrew Reisse, Volga Aksoy
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

// This app renders a simple room, with right handed coord system :  Y->Up, Z->Back, X->Right
// 'W','A','S','D' and arrow keys to navigate. (Other keys in Win32_RoomTiny_ExamplesFeatures.h)
// 1.  SDK-rendered is the simplest path (this file)
// 2.  APP-rendered involves other functions, in Win32_RoomTiny_AppRendered.h
// 3.  Further options are illustrated in Win32_RoomTiny_ExampleFeatures.h
// 4.  Supporting D3D11 and utility code is in Win32_DX11AppUtil.h

// Choose whether the SDK performs rendering/distortion, or the application.
#define SDK_RENDER 0

#include "Win32_DX11AppUtil.h"         // Include Non-SDK supporting utilities
#include "OVR_CAPI.h"                  // Include the OculusVR SDK
#include <vector>
#include <iostream>
#include <istream>
#include <fstream>
#include <string>
#include <Windows.h>

ovrHmd           HMD;                  // The handle of the headset
ovrEyeRenderDesc EyeRenderDesc[2];     // Description of the VR.
ovrRecti         EyeRenderViewport[2]; // Useful to remember when varying resolution
ImageBuffer    * pEyeRenderTexture[2]; // Where the eye buffers will be rendered
ImageBuffer    * pEyeDepthBuffer[2];   // For the eye buffers to use when rendered
ovrPosef         EyeRenderPose[2];     // Useful to remember where the rendered eye originated
float            YawAtRender[2];       // Useful to remember where the rendered eye originated
float            Yaw(0.0f);       // Horizontal rotation of the player
float			 Pitch(0.0f);
Vector3f         Pos(0.0f,0.0f,0.0f); // Position of player

#include "Win32_RoomTiny_ExampleFeatures.h" // Include extra options to show some simple operations

#if SDK_RENDER
#define   OVR_D3D_VERSION 11
#include "OVR_CAPI_D3D.h"                   // Include SDK-rendered code for the D3D version
#else
#include "Win32_RoomTiny_AppRender.h"       // Include non-SDK-rendered specific code
#endif


//http://www.codeproject.com/Articles/3061/Creating-a-Serial-communication-on-Win
class ArduinoLED
{
public:
	ArduinoLED()
	{
		m_comPort = CreateFile(L"COM5", GENERIC_READ|GENERIC_WRITE,0, NULL, OPEN_EXISTING, 0, NULL);

		DCB commState;
		GetCommState(m_comPort, &commState);

		commState.BaudRate = 9600;
		commState.Parity = 0;
		commState.StopBits = 0;

		SetCommState(m_comPort, &commState);

		WaitAck();

		state = false;
		enable_loopback = false;
	}

	~ArduinoLED()
	{
		CloseHandle(m_comPort);
	}

	void On()
	{
		WriteByte('a');
		state = true;
	}

	void Off()
	{
		WriteByte('b');
		state = false;
	}

	void Invert()
	{
		if(state) 
			Off();
		else 
			On();
	}


private:
	HANDLE m_comPort;

	bool state;

	bool enable_loopback;

	void WriteByte(char byte)
	{
		DWORD length;
		WriteFile(m_comPort, &byte, 1, &length, NULL);
		FlushFileBuffers(m_comPort);

		if(enable_loopback){
			if(WaitAck() != byte)
			{
				printf("Error on serial");
			}
		}
	}

	int WaitAck()
	{
		char read;
		DWORD length;
		while(!ReadFile(m_comPort,&read,1,&length,NULL))
		{
		}
		return read;
	}


};

class Logging
{
public:

	struct Record
	{
		double timestamp;
		float x;
		float y;
		float z;
		float w;
		float yaw;
		float pitch;
		float roll;
	};

	Logging(ovrHmd hmd)
	{
		m_HMD = hmd;
		m_locked = false;
	}

	void Update()
	{
		if(m_locked)
		{
			return;
		}

		ovrTrackingState state = ovrHmd_GetTrackingState(m_HMD, 0);
		
		static double lasttime = 0;

		if(state.HeadPose.TimeInSeconds == lasttime)
		{
			return;
		}

		lasttime = state.HeadPose.TimeInSeconds;

		Record r;
		r.timestamp = state.HeadPose.TimeInSeconds;
		r.x = state.HeadPose.ThePose.Orientation.x;
		r.y = state.HeadPose.ThePose.Orientation.y;
		r.z = state.HeadPose.ThePose.Orientation.z;
		r.w = state.HeadPose.ThePose.Orientation.w;

		((OVR::Quatf)state.HeadPose.ThePose.Orientation).GetEulerAngles<Axis_Y, Axis_X, Axis_Z>(&(r.yaw), &(r.pitch), &(r.roll));
		
		log.push_back(r);
	}

	OVR::Quatf GetState(double timeInSeconds)
	{
		for(int i = 0; i < log.size(); i++)
		{
			double offset = log[i].timestamp - log.front().timestamp;
			if(offset > timeInSeconds)
			{
				Record r = log[i-1];
				return OVR::Quatf(r.x,r.y,r.z,r.w);
			}
		}

		return OVR::Quatf(log.back().x,log.back().y,log.back().z,log.back().w);
	}

	double GetLastTime()
	{
		return log.back().timestamp - log.front().timestamp;
	}

	void Reset()
	{
		if(m_locked)
		{
			return;
		}

		log = std::vector<Logging::Record>();
	}

	void Save()
	{
		if(m_locked)
		{
			return;
		}

		std::ofstream file;
		file.open("C:\\HeadLogs\\Log.csv",std::ios::trunc);

		for(int i = 0; i < log.size(); i++)
		{
			Record r = log[i];
			file << std::fixed << r.timestamp << "," << r.x << "," << r.y << "," << r.z << "," << r.w << "," << r.roll << "," << r.pitch << "," << r.yaw <<  "\n";
		}

		file.close();
		Reset();
	}

	void Load(std::string filename)
	{
		Reset();
		std::ifstream file(filename);
		std::string item;
		while(file.good())
		{
			Record r;
			std::getline(file, item, ','); r.timestamp = std::stod(item);

			if(item.length() <= 0)
				break;

			std::getline(file, item, ','); r.x = std::stod(item);
			std::getline(file, item, ','); r.y = std::stod(item);
			std::getline(file, item, ','); r.z = std::stod(item);
			std::getline(file, item, ','); r.w = std::stod(item);
			std::getline(file, item, ','); r.yaw = std::stod(item);
			std::getline(file, item, ','); r.pitch = std::stod(item);
			std::getline(file, item); r.roll = std::stod(item);
			log.push_back(r);
		}
		m_locked = true;
	}

	std::vector<Logging::Record> log;
	bool m_locked;

private:
	ovrHmd m_HMD;

};


//https://msdn.microsoft.com/en-us/library/windows/desktop/ms682516(v=vs.85).aspx

class LoggingThread
{
public:
	LoggingThread(Logging& log)
	{
		//begin the thread
		threadParams.run = true;
		threadParams.log = &log;
		threadHandle = CreateThread(NULL, 0, threadFunction, &threadParams, 0, &threadId);
	}

	void WaitForExit()
	{
		threadParams.run = false;
		WaitForSingleObject(threadHandle, 10000);
	}

private:

	struct params
	{
		Logging* log;
		bool run;
	};

	params threadParams;

	static DWORD WINAPI threadFunction(LPVOID lpParam)
	{
		params* threadParams = (params*)lpParam;

		if(threadParams->log->m_locked){
			threadParams->run = false;
		}

		while(threadParams->run){
			threadParams->log->Update();
		}

		return 0;
	}


	DWORD threadId;
	HANDLE threadHandle;

};

class Stopwatch
{
public:
	Stopwatch()
	{
		QueryPerformanceFrequency(&frequency);
		start_count.QuadPart = 0;
	}

	LARGE_INTEGER frequency;
	LARGE_INTEGER start_count;

	void Restart()
	{
		QueryPerformanceCounter(&start_count);
	}
	
	double getTimeInSeconds()
	{
		LARGE_INTEGER current;
		QueryPerformanceCounter(&current);
		LONGLONG offsetL = current.QuadPart - start_count.QuadPart;
		double offset = (double)offsetL;
		double f = (double)frequency.QuadPart;
		return ((double)offset / (double)(frequency.QuadPart));
	}
};

//-------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int)
{
	// initialise the arduino to signal when we begin clocking out the tracker data

	ArduinoLED led;


    // Initializes LibOVR, and the Rift
    ovr_Initialize();
    HMD = ovrHmd_Create(0);

    if (!HMD)                       { MessageBoxA(NULL,"Oculus Rift not detected.","", MB_OK); return(0); }
    if (HMD->ProductName[0] == '\0')  MessageBoxA(NULL,"Rift detected, display not enabled.", "", MB_OK);

    // Setup Window and Graphics - use window frame if relying on Oculus driver

	// For this, just create a window for debugging and do not connect the display

   // bool windowed = (HMD->HmdCaps & ovrHmdCap_ExtendDesktop) ? false : true;    
	bool windowed = true;
	if (!DX11.InitWindowAndDevice(hinst, Recti(OVR::Vector2<int>(0,0), HMD->Resolution), windowed))
        return(0);

    DX11.SetMaxFrameLatency(1);
    ovrHmd_AttachToWindow(HMD, DX11.Window, NULL, NULL);
    ovrHmd_SetEnabledCaps(HMD, ovrHmdCap_LowPersistence);

    // Start the sensor which informs of the Rift's pose and motion
    ovrHmd_ConfigureTracking(HMD, ovrTrackingCap_Orientation, 0);

    // Make the eye render buffers (caution if actual size < requested due to HW limits). 
    for (int eye=0; eye<2; eye++)
    {
        Sizei idealSize             = ovrHmd_GetFovTextureSize(HMD, (ovrEyeType)eye,
                                                               HMD->DefaultEyeFov[eye], 1.0f);
        pEyeRenderTexture[eye]      = new ImageBuffer(true, false, idealSize);
        pEyeDepthBuffer[eye]        = new ImageBuffer(true, true, pEyeRenderTexture[eye]->Size);
        EyeRenderViewport[eye].Pos  = Vector2i(0, 0);
        EyeRenderViewport[eye].Size = pEyeRenderTexture[eye]->Size;
    }

    // Setup VR components
#if SDK_RENDER
    ovrD3D11Config d3d11cfg;
    d3d11cfg.D3D11.Header.API            = ovrRenderAPI_D3D11;
    d3d11cfg.D3D11.Header.BackBufferSize = Sizei(HMD->Resolution.w, HMD->Resolution.h);
    d3d11cfg.D3D11.Header.Multisample    = 1;
    d3d11cfg.D3D11.pDevice               = DX11.Device;
    d3d11cfg.D3D11.pDeviceContext        = DX11.Context;
    d3d11cfg.D3D11.pBackBufferRT         = DX11.BackBufferRT;
    d3d11cfg.D3D11.pSwapChain            = DX11.SwapChain;

    if (!ovrHmd_ConfigureRendering(HMD, &d3d11cfg.Config,
                                   ovrDistortionCap_Chromatic | ovrDistortionCap_Vignette |
                                   ovrDistortionCap_TimeWarp | ovrDistortionCap_Overdrive,
                                   HMD->DefaultEyeFov, EyeRenderDesc))
        return(1);

#else
    APP_RENDER_SetupGeometryAndShaders();
#endif


	bool EnableOfflineRender = true;
	bool EnableOnlineRender = true;

	bool EnablePlayback = EnableOfflineRender | EnableOnlineRender;

    // Create the room model
    Scene roomScene(false); // Can simplify scene further with parameter if required.

	Logging log(HMD);

	if(EnableOfflineRender){
		log.Load("C:\\Users\\Sebastian\\Dropbox\\Investigations\\Rendering Experiment\\Head Tracking Logs\\HeadMotionMaster.csv");
	}

	LoggingThread loggingThread(log); 	//begin logging in seperate thread

	OpenRenderFile();

	double timeInSeconds = 0;

	Stopwatch stopwatch;
	stopwatch.Restart();

    // MAIN LOOP
    // =========
    while (!(DX11.Key['Q'] && DX11.Key[VK_CONTROL]) && !DX11.Key[VK_ESCAPE])
    {
        DX11.HandleMessages();
        
        float       speed                    = 1.0f; // Can adjust the movement speed. 
        int         timesToRenderScene       = 1;    // Can adjust the render burden on the app.
		ovrVector3f useHmdToEyeViewOffset[2] = {EyeRenderDesc[0].HmdToEyeViewOffset,
			                                    EyeRenderDesc[1].HmdToEyeViewOffset};
        // Start timing
    #if SDK_RENDER
        ovrHmd_BeginFrame(HMD, 0);
    #else
        ovrHmd_BeginFrameTiming(HMD, 0);
    #endif

		if(!EnablePlayback){

        // Handle key toggles for re-centering, meshes, FOV, etc.
        ExampleFeatures1(&speed, &timesToRenderScene, useHmdToEyeViewOffset);

        // Keyboard inputs to adjust player orientation
        if (DX11.Key['E'])  Yaw += 0.02f;
        if (DX11.Key['Q']) Yaw -= 0.02f;
		if (DX11.Key['R'])  Pitch += 0.02f;
        if (DX11.Key['F']) Pitch -= 0.02f;

        // Keyboard inputs to adjust player position
        if (DX11.Key['W']||DX11.Key[VK_UP])   Pos+=Matrix4f::RotationY(Yaw).Transform(Vector3f(0,0,-speed*0.05f));
        if (DX11.Key['S']||DX11.Key[VK_DOWN]) Pos+=Matrix4f::RotationY(Yaw).Transform(Vector3f(0,0,+speed*0.05f));
        if (DX11.Key['D'])                    Pos+=Matrix4f::RotationY(Yaw).Transform(Vector3f(+speed*0.05f,0,0));
        if (DX11.Key['A'])                    Pos+=Matrix4f::RotationY(Yaw).Transform(Vector3f(-speed*0.05f,0,0));

		}
       
		//Pos.y = ovrHmd_GetFloat(HMD, OVR_KEY_EYE_HEIGHT, Pos.y);

		//set eye pose, either from tracker (here) or recorded log
		ovrPosef hmdPose;
		hmdPose.Orientation = Quatf(Vector3f(0,0,1),0);
		hmdPose.Position = Pos;

		//overwrite tracking data with actual value from hmd for live interaction
		ovrTrackingState state = ovrHmd_GetTrackingState(HMD, 0);
		hmdPose.Orientation = state.HeadPose.ThePose.Orientation;

		if(EnablePlayback)
		{
			hmdPose.Orientation = log.GetState(timeInSeconds); //overwrite the tracking data with that from the prerecorded logs
		}

		if(EnableOnlineRender)
		{
			static bool isFirstRun = true;

			//when we signal we are about to read the first head log, that should be point at which we start syncing the leds
			if(isFirstRun)
			{
				isFirstRun = false;
				stopwatch.Restart();
				led.On();
			}

			timeInSeconds = stopwatch.getTimeInSeconds();

			static float lastFiveHundredMsSegment = 0;
			float fiveHundredMsSegmentNum = std::floor(timeInSeconds / 0.5f);

			if(fiveHundredMsSegmentNum != lastFiveHundredMsSegment)
			{
				lastFiveHundredMsSegment = fiveHundredMsSegmentNum;
				led.Invert();
			}
		}

		if(EnableOfflineRender)
		{
			timeInSeconds += 0.001; //in the offline render we increment the time on each iteration
		}

		if(EnablePlayback){
			double logLength = log.GetLastTime();
			if(timeInSeconds >= logLength)
			{
				break;
			}
		}

		ovrPosef temp_EyeRenderPose[2];
		temp_EyeRenderPose[0] = Posef(hmdPose.Orientation, ((Posef)hmdPose).Apply(-((Vector3f)useHmdToEyeViewOffset[0])));
		temp_EyeRenderPose[1] = Posef(hmdPose.Orientation, ((Posef)hmdPose).Apply(-((Vector3f)useHmdToEyeViewOffset[1])));

        // Render the two undistorted eye views into their render buffers.  
        for (int eye = 0; eye < 2; eye++)
        {
            ImageBuffer * useBuffer      = pEyeRenderTexture[eye];  
            ovrPosef    * useEyePose     = &EyeRenderPose[eye];
            float       * useYaw         = &YawAtRender[eye];
            bool          clearEyeImage  = true;
            bool          updateEyeImage = true;

			if(!EnablePlayback){

            // Handle key toggles for half-frame rendering, buffer resolution, etc.
            ExampleFeatures2(eye, &useBuffer, &useEyePose, &useYaw, &clearEyeImage, &updateEyeImage);

			}

            if (clearEyeImage)
                DX11.ClearAndSetRenderTarget(useBuffer->TexRtv,
                                             pEyeDepthBuffer[eye], Recti(EyeRenderViewport[eye]));
            if (updateEyeImage)
            {
                // Write in values actually used (becomes significant in Example features)
                *useEyePose = temp_EyeRenderPose[eye];
                *useYaw     = Yaw;

                // Get view and projection matrices (note near Z to reduce eye strain)
				Matrix4f rollPitchYaw       = Matrix4f::RotationY(Yaw) * Matrix4f::RotationX(Pitch);
				Matrix4f finalRollPitchYaw  = rollPitchYaw * Matrix4f(EyeRenderPose[eye].Orientation);
				Vector3f finalUp            = finalRollPitchYaw.Transform(Vector3f(0, 1, 0));
				Vector3f finalForward       = finalRollPitchYaw.Transform(Vector3f(0, 0, -1));
                Vector3f shiftedEyePos      = (useEyePose->Position);

                Matrix4f view = Matrix4f::LookAtRH(shiftedEyePos, shiftedEyePos + finalForward, finalUp);
                Matrix4f proj = ovrMatrix4f_Projection(EyeRenderDesc[eye].Fov, 0.001f, 1000.0f, true); 

                // Render the scene
                for (int t=0; t<timesToRenderScene; t++)
                    roomScene.Render(view, proj.Transposed());
            }
        }

        // Do distortion rendering, Present and flush/sync
    #if SDK_RENDER    
        ovrD3D11Texture eyeTexture[2]; // Gather data for eye textures 
        for (int eye = 0; eye<2; eye++)
        {
            eyeTexture[eye].D3D11.Header.API            = ovrRenderAPI_D3D11;
            eyeTexture[eye].D3D11.Header.TextureSize    = pEyeRenderTexture[eye]->Size;
            eyeTexture[eye].D3D11.Header.RenderViewport = EyeRenderViewport[eye];
            eyeTexture[eye].D3D11.pTexture              = pEyeRenderTexture[eye]->Tex;
            eyeTexture[eye].D3D11.pSRView               = pEyeRenderTexture[eye]->TexSv;
        }
        ovrHmd_EndFrame(HMD, EyeRenderPose, &eyeTexture[0].Texture);

    #else
        APP_RENDER_DistortAndPresent();
    #endif
    }

	double t = stopwatch.getTimeInSeconds();

	loggingThread.WaitForExit();
	log.Save();

	CloseRenderFile();

    // Release and close down
    ovrHmd_Destroy(HMD);
    ovr_Shutdown();
	DX11.ReleaseWindow(hinst);

    return(0);
}
