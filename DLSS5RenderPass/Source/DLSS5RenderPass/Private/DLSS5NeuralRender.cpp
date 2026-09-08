//Copyright MonsterGuoGuo. All Rights Reserved.2026
#include "DLSS5NeuralRender.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

FDLSS5NeuralRenderProcessor::FDLSS5NeuralRenderProcessor()
{
}

FDLSS5NeuralRenderProcessor::~FDLSS5NeuralRenderProcessor()
{
	Shutdown();
}

FString FDLSS5NeuralRenderProcessor::ResolveRuntimeDirectory(const FDLSS5Settings& InSettings) const
{
	// 1. 用户显式指定的目录优先
	if (!InSettings.RuntimeDirectory.IsEmpty())
	{
		const FString Candidate = FPaths::ConvertRelativePathToFull(InSettings.RuntimeDirectory);
		if (FPaths::FileExists(FPaths::Combine(Candidate, TEXT("dlssnr_host.dll"))))
		{
			return Candidate;
		}
	}

	// 2. 本插件 Binaries/Win64（插件自包含时使用）
	const FString CandidateDirs[] =
	{
		FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("DLSS5RenderPass/Binaries/Win64")),
	};

	for (const FString& Candidate : CandidateDirs)
	{
		if (FPaths::FileExists(FPaths::Combine(Candidate, TEXT("dlssnr_host.dll"))))
		{
			return FPaths::ConvertRelativePathToFull(Candidate);
		}
	}

	return FString();
}

bool FDLSS5NeuralRenderProcessor::Initialize(int32 InWidth, int32 InHeight, const FDLSS5Settings& InSettings)
{
	if (bReady)
	{
		// 已初始化且分辨率一致，只需更新参数
		if (Width == InWidth && Height == InHeight)
		{
			if (SetOptionsFn)
			{
				SetOptionsFn(
					1,
					(int32)InSettings.Style,
					InSettings.Intensity,
					InSettings.LocalToneStrength,
					InSettings.LocalStructureStrength,
					InSettings.SkinStructureStrength,
					0,  // 自动遮罩：离线渲染无 UI 叠加层，固定关闭
					0,  // UI 修正：离线渲染无 UI 叠加层，固定关闭
					0,
					2,
					1.0f,
					1.0f
				);
			}
			return true;
		}
		// 分辨率变化，走 Resize
		return Resize(InWidth, InHeight);
	}

	if (InWidth <= 0 || InHeight <= 0)
	{
		LastError = TEXT("Invalid resolution for DLSS init.");
		return false;
	}

	// 1. 定位 dlssnr_host.dll
	const FString DllDir = ResolveRuntimeDirectory(InSettings);
	if (DllDir.IsEmpty())
	{
		LastError = TEXT("dlssnr_host.dll not found. Place DLSS 5 runtime (dlssnr_host.dll + nvngx_dlssnr.dll) in Plugins/DLSS5RenderPass/Binaries/Win64, or set Runtime Directory.");
		return false;
	}

	const FString HostDllPath = FPaths::Combine(DllDir, TEXT("dlssnr_host.dll"));

	// 2. 加载 DLL 并获取导出函数
	DllHandle = FPlatformProcess::GetDllHandle(*HostDllPath);
	if (!DllHandle)
	{
		LastError = FString::Printf(TEXT("Failed to load dlssnr_host.dll from: %s"), *HostDllPath);
		return false;
	}

	InitFn = (FDLSSNR_Init)FPlatformProcess::GetDllExport(DllHandle, TEXT("dlssnr_init"));
	CreateFeatureFn = (FDLSSNR_CreateFeature)FPlatformProcess::GetDllExport(DllHandle, TEXT("dlssnr_create_feature"));
	ProcessFn = (FDLSSNR_Process)FPlatformProcess::GetDllExport(DllHandle, TEXT("dlssnr_process"));
	SetOptionsFn = (FDLSSNR_SetOptions)FPlatformProcess::GetDllExport(DllHandle, TEXT("dlssnr_set_options"));
	ResizeFn = (FDLSSNR_Resize)FPlatformProcess::GetDllExport(DllHandle, TEXT("dlssnr_resize"));
	ShutdownFn = (FDLSSNR_Shutdown)FPlatformProcess::GetDllExport(DllHandle, TEXT("dlssnr_shutdown"));

	if (!InitFn || !CreateFeatureFn || !ProcessFn || !SetOptionsFn || !ResizeFn || !ShutdownFn)
	{
		LastError = TEXT("Failed to get dlssnr_host.dll exports.");
		Shutdown();
		return false;
	}

	// 3. 定位 nvngx_dlssnr.dll（NGX 运行时，固定使用该文件）
	const FString RuntimeDllPath = FPaths::Combine(DllDir, TEXT("nvngx_dlssnr.dll"));
	if (!FPaths::FileExists(RuntimeDllPath))
	{
		LastError = FString::Printf(TEXT("nvngx_dlssnr.dll not found: %s. Check Runtime Directory."), *RuntimeDllPath);
		Shutdown();
		return false;
	}

	// 日志路径
	const FString LogFilePath = FPaths::Combine(DllDir, TEXT("dlss_run.log"));

	// 4. 初始化 NGX + 创建 Feature
	// Windows 上 TCHAR == wchar_t，直接传 *FString 即可作为 const wchar_t*
	if (InitFn(InWidth, InHeight, 1, *RuntimeDllPath, *LogFilePath) == 0)
	{
		LastError = TEXT("dlssnr_init failed. Check GPU, driver, and dlss_run.log.");
		Shutdown();
		return false;
	}

	if (CreateFeatureFn(InWidth, InHeight, 1) == 0)
	{
		LastError = TEXT("dlssnr_create_feature failed. Runtime may not be supported by this GPU (DLSS 5 requires RTX 40/50 series).");
		Shutdown();
		return false;
	}

	// 5. 设置参数
	SetOptionsFn(
		1,
		static_cast<int32>(InSettings.Style),
		InSettings.Intensity,
		InSettings.LocalToneStrength,
		InSettings.LocalStructureStrength,
		InSettings.SkinStructureStrength,
		0,
		0,
		0,
		2,
		1.0f,
		1.0f
	);

	// 6. 分配零填充的运动矢量和深度缓冲
	const int64 PixelCount = (int64)InWidth * InHeight;
	MotionBuffer.SetNumUninitialized(PixelCount * 2);
	FMemory::Memzero(MotionBuffer.GetData(), MotionBuffer.Num() * sizeof(float));
	DepthBuffer.SetNumUninitialized(PixelCount);
	FMemory::Memzero(DepthBuffer.GetData(), DepthBuffer.Num() * sizeof(float));

	Width = InWidth;
	Height = InHeight;
	bReady = true;
	LastError.Empty();
	return true;
}

bool FDLSS5NeuralRenderProcessor::ProcessFrame(const uint8* InInputRGBA8, uint8* OutOutputRGBA8, bool bResetHistory)
{
	if (!bReady || !ProcessFn)
	{
		LastError = TEXT("DLSS not initialized.");
		return false;
	}
	if (!InInputRGBA8 || !OutOutputRGBA8)
	{
		LastError = TEXT("Null input/output buffer.");
		return false;
	}

	FScopeLock Lock(&ProcessMutex);

	const int32 ResetFlag = bResetHistory ? 1 : 0;
	const int32 Result = ProcessFn(
		const_cast<uint8*>(InInputRGBA8),
		MotionBuffer.GetData(),
		DepthBuffer.GetData(),
		OutOutputRGBA8,
		ResetFlag
	);

	if (Result == 0)
	{
		LastError = TEXT("dlssnr_process failed.");
		return false;
	}
	return true;
}

bool FDLSS5NeuralRenderProcessor::Resize(int32 InWidth, int32 InHeight)
{
	if (!bReady || !ResizeFn)
	{
		LastError = TEXT("DLSS not initialized, cannot resize.");
		return false;
	}

	FScopeLock Lock(&ProcessMutex);

	if (ResizeFn(InWidth, InHeight, 1) == 0)
	{
		LastError = TEXT("dlssnr_resize failed.");
		return false;
	}

	const int64 PixelCount = (int64)InWidth * InHeight;
	MotionBuffer.SetNumUninitialized(PixelCount * 2);
	FMemory::Memzero(MotionBuffer.GetData(), MotionBuffer.Num() * sizeof(float));
	DepthBuffer.SetNumUninitialized(PixelCount);
	FMemory::Memzero(DepthBuffer.GetData(), DepthBuffer.Num() * sizeof(float));

	Width = InWidth;
	Height = InHeight;
	return true;
}

void FDLSS5NeuralRenderProcessor::Shutdown()
{
	if (bReady && ShutdownFn)
	{
		ShutdownFn();
	}
	if (DllHandle)
	{
		FPlatformProcess::FreeDllHandle(DllHandle);
		DllHandle = nullptr;
	}
	InitFn = nullptr;
	CreateFeatureFn = nullptr;
	ProcessFn = nullptr;
	SetOptionsFn = nullptr;
	ResizeFn = nullptr;
	ShutdownFn = nullptr;
	MotionBuffer.Empty();
	DepthBuffer.Empty();
	Width = 0;
	Height = 0;
	bReady = false;
}
