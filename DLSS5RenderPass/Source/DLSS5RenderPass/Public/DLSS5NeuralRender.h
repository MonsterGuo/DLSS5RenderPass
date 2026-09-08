//Copyright MonsterGuoGuo. All Rights Reserved.2026
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/**
 * DLSS 5 神经渲染风格（对应 dlssnr_set_options 的 style 参数 0-3）。
 */
UENUM(BlueprintType)
enum class EDLSS5Style : uint8
{
	/** 默认 */
	Default UMETA(DisplayName = "默认 (0)"),
	/** 自然 */
	Natural UMETA(DisplayName = "自然 (1)"),
	/** 电影 */
	Cinematic UMETA(DisplayName = "电影 (2)"),
	/** 风格 3 */
	Style3 UMETA(DisplayName = "风格 3 (3)"),
};

/**
 * DLSS 5 神经渲染参数配置。
 * 对应 dlssnr_host.dll 的 dlssnr_set_options 接口。
 */
struct FDLSS5Settings
{
	/** 是否启用 DLSS 5 神经渲染 */
	bool bEnabled = false;

	/** 渲染风格 */
	EDLSS5Style Style = EDLSS5Style::Cinematic;

	/** 强度 (0.0-1.0) */
	float Intensity = 1.0f;

	/** 局部色调强度 (0.0-1.0) */
	float LocalToneStrength = 1.0f;

	/** 局部结构强度 (0.0-1.0) */
	float LocalStructureStrength = 1.0f;

	/** 皮肤结构强度 (0.0-1.0) */
	float SkinStructureStrength = 0.1f;

	/** nvngx_dlssnr.dll 所在目录（含 dlssnr_host.dll）。留空则按默认顺序搜索插件目录。 */
	FString RuntimeDirectory;
};

/**
 * 封装 dlssnr_host.dll 的 C ABI，提供 DLSS 5 神经渲染（3D-Guided Neural Rendering）同分辨率细节增强。
 * 线程安全：内部用互斥锁序列化所有 NGX 调用，因为 DLSS 会话含有时序历史状态。
 */
class FDLSS5NeuralRenderProcessor
{
public:
	FDLSS5NeuralRenderProcessor();
	~FDLSS5NeuralRenderProcessor();

	FDLSS5NeuralRenderProcessor(const FDLSS5NeuralRenderProcessor&) = delete;
	FDLSS5NeuralRenderProcessor& operator=(const FDLSS5NeuralRenderProcessor&) = delete;

	/**
	 * 初始化 DLSS 会话。加载 dlssnr_host.dll 并按指定分辨率创建 Feature。
	 * @param InWidth  输入/输出宽度
	 * @param InHeight 输入/输出高度
	 * @param InSettings DLSS 配置
	 * @return true 初始化成功
	 */
	bool Initialize(int32 InWidth, int32 InHeight, const FDLSS5Settings& InSettings);

	/**
	 * 处理一帧 RGBA8 图像。输入输出分辨率必须与 Initialize 时一致（或已 Resize）。
	 * @param InInputRGBA8  输入像素数据（RGBA8，sRGB），大小 = Width*Height*4
	 * @param OutOutputRGBA8 输出像素数据（RGBA8，sRGB），需预先分配 Width*Height*4
	 * @param bResetHistory 是否重置时序历史（序列第一帧传 true）
	 * @return true 处理成功
	 */
	bool ProcessFrame(const uint8* InInputRGBA8, uint8* OutOutputRGBA8, bool bResetHistory);

	/**
	 * 切换 DLSS 会话分辨率。不同 Shot 分辨率不同时调用。
	 */
	bool Resize(int32 InWidth, int32 InHeight);

	/** 销毁 DLSS 会话，卸载 DLL。 */
	void Shutdown();

	/** 是否已经成功初始化。 */
	bool IsReady() const { return bReady; }

	/** 当前会话宽度。 */
	int32 GetWidth() const { return Width; }

	/** 当前会话高度。 */
	int32 GetHeight() const { return Height; }

	/** 获取最后错误信息。 */
	const FString& GetLastError() const { return LastError; }

private:
	/** dlssnr_host.dll 的 C ABI 函数指针类型 */
	using FDLSSNR_Init = int32(*)(int32, int32, int32, const wchar_t*, const wchar_t*);
	using FDLSSNR_CreateFeature = int32(*)(int32, int32, int32);
	using FDLSSNR_Process = int32(*)(uint8*, float*, float*, uint8*, int32);
	using FDLSSNR_SetOptions = void(*)(int32, int32, float, float, float, float, int32, int32, int32, int32, float, float);
	using FDLSSNR_Resize = int32(*)(int32, int32, int32);
	using FDLSSNR_Shutdown = void(*)();

	/** 按 RuntimeDirectory 与默认插件目录顺序查找 dlssnr_host.dll / nvngx_dlssnr.dll 所在目录 */
	FString ResolveRuntimeDirectory(const FDLSS5Settings& InSettings) const;

	void* DllHandle = nullptr;
	FDLSSNR_Init InitFn = nullptr;
	FDLSSNR_CreateFeature CreateFeatureFn = nullptr;
	FDLSSNR_Process ProcessFn = nullptr;
	FDLSSNR_SetOptions SetOptionsFn = nullptr;
	FDLSSNR_Resize ResizeFn = nullptr;
	FDLSSNR_Shutdown ShutdownFn = nullptr;

	/** 零填充的运动矢量与深度缓冲（离线模式无引擎数据） */
	TArray<float> MotionBuffer;
	TArray<float> DepthBuffer;

	int32 Width = 0;
	int32 Height = 0;
	bool bReady = false;
	FString LastError;

	/** 序列化所有 DLSS 调用，保护时序历史状态 */
	FCriticalSection ProcessMutex;
};
