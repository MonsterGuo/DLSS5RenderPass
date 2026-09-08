//Copyright MonsterGuoGuo. All Rights Reserved.2026

#include "DLSS5OutputMerger.h"
#include "MoviePipeline.h"
#include "MovieRenderPipelineCoreModule.h"
#include "ImagePixelData.h"

FDLSS5OutputMerger::FDLSS5OutputMerger(UMoviePipeline* InOwningMoviePipeline, const FDLSS5Settings& InDLSSSettings, const FMoviePipelinePassIdentifier& InMainPassIdentifier)
	: FMoviePipelineOutputMerger(InOwningMoviePipeline)
	, DLSSSettings(InDLSSSettings)
	, MainPassIdentifier(InMainPassIdentifier)
{
}

void FDLSS5OutputMerger::OnCompleteRenderPassDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData)
{
	// 只对主通道的最终混合帧应用 DLSS 5，蒙版 / ID / 后期材质等数据通道原样透传
	if (DLSSSettings.bEnabled && InData.IsValid())
	{
		if (const FImagePixelDataPayload* Payload = InData->GetPayload<FImagePixelDataPayload>())
		{
			//UE_LOG(LogMovieRenderPipeline, Warning, TEXT("DLSS 处理器：进主通道前:%s:%s"),*Payload->PassIdentifier.CameraName,*MainPassIdentifier.CameraName);
			if (Payload->PassIdentifier.Name == MainPassIdentifier.Name)
			{
				// UE_LOG(LogMovieRenderPipeline, Warning, TEXT("DLSS 处理器：进主通道"));
				const int32 FrameNumber = Payload->SampleState.OutputState.OutputFrameNumber;
				if (InData->GetType() == EImagePixelType::Float32)
				{
					TImagePixelData<FLinearColor>* LinearData = static_cast<TImagePixelData<FLinearColor>*>(InData.Get());
					ApplyDLSS(LinearData->Pixels, LinearData->GetSize(), FrameNumber);
				}
				else if (InData->GetType() == EImagePixelType::Float16)
				{
					TImagePixelData<FFloat16Color>* HalfData = static_cast<TImagePixelData<FFloat16Color>*>(InData.Get());
					// Float16 数据先提升为线性 Float32 处理，再写回
					TArray64<FLinearColor> LinearPixels;
					LinearPixels.SetNumUninitialized(HalfData->Pixels.Num());
					for (int64 i = 0; i < HalfData->Pixels.Num(); ++i)
					{
						LinearPixels[i] = FLinearColor(HalfData->Pixels[i]);
					}
					if (ApplyDLSS(LinearPixels, HalfData->GetSize(), FrameNumber))
					{
						for (int64 i = 0; i < HalfData->Pixels.Num(); ++i)
						{
							HalfData->Pixels[i] = FFloat16Color(LinearPixels[i]);
						}
					}
				}
			}
		}
	}

	FMoviePipelineOutputMerger::OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(InData));
}

bool FDLSS5OutputMerger::ApplyDLSS(TArray64<FLinearColor>& InOutPixels, const FIntPoint& InSize, int32 InFrameNumber)
{
	if (!DLSSSettings.bEnabled)
	{
		
		return false;
	}

	
	// 完成帧会在多个任务线程并发进入此函数。首帧懒加载初始化（DLL 加载 + NGX Feature
	// 创建）耗时数秒，期间到达的帧在此等待初始化完成，而不是看到半初始化状态
	//（Width=0）误判为分辨率变化、调用 Resize 报 "DLSS not initialized" 并被跳过。
	FScopeLock Lock(&DLSSMutex);

	// 懒加载 DLSS 处理器（首次使用时初始化）
	if (!DLSSProcessor.IsValid() && !bDLSSInitAttempted)
	{
		bDLSSInitAttempted = true;
		DLSSProcessor = MakeUnique<FDLSS5NeuralRenderProcessor>();
		if (!DLSSProcessor->Initialize(InSize.X, InSize.Y, DLSSSettings))
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[DLSS5RenderPass] Initialization failed: %s. DLSS 5 will be skipped for this render."), *DLSSProcessor->GetLastError());
			DLSSProcessor.Reset();
			return false;
		}
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("[DLSS5RenderPass] DLSS 5 initialized at %dx%d"), InSize.X, InSize.Y);
	}

	if (!DLSSProcessor.IsValid())
	{
		return false;
	}

	// 分辨率变化时 Resize（不同 Shot 分辨率可能不同）
	if (DLSSProcessor->GetWidth() != InSize.X || DLSSProcessor->GetHeight() != InSize.Y)
	{
		if (!DLSSProcessor->Resize(InSize.X, InSize.Y))
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[DLSS5RenderPass] Resize to %dx%d failed: %s"), InSize.X, InSize.Y, *DLSSProcessor->GetLastError());
			return false;
		}
	}

	const int64 PixelCount = (int64)InSize.X * InSize.Y;
	if (InOutPixels.Num() < PixelCount)
	{
		return false;
	}

	// 1. FLinearColor (线性 HDR) → FColor (sRGB LDR RGBA8)
	TArray<uint8> InputRGBA8;
	InputRGBA8.SetNumUninitialized(PixelCount * 4);
	{
		const FLinearColor* Src = InOutPixels.GetData();
		uint8* Dst = InputRGBA8.GetData();
		for (int64 i = 0; i < PixelCount; ++i)
		{
			const FColor C = Src[i].ToFColor(true);
			Dst[i * 4 + 0] = C.R;
			Dst[i * 4 + 1] = C.G;
			Dst[i * 4 + 2] = C.B;
			Dst[i * 4 + 3] = C.A;
		}
	}

	// 2. 调用 DLSS 5 神经渲染
	TArray<uint8> OutputRGBA8;
	OutputRGBA8.SetNumUninitialized(PixelCount * 4);

	// 判断是否需要重置时序历史：帧编号不连续（如新 Shot 首帧）时重置
	const bool bResetHistory = (LastOutputFrameNumber < 0) || (InFrameNumber <= LastOutputFrameNumber);
	LastOutputFrameNumber = InFrameNumber;

	if (!DLSSProcessor->ProcessFrame(InputRGBA8.GetData(), OutputRGBA8.GetData(), bResetHistory))
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[DLSS5RenderPass] ProcessFrame failed: %s"), *DLSSProcessor->GetLastError());
		return false;
	}

	// 3. FColor (sRGB LDR RGBA8) → FLinearColor (线性 HDR)
	{
		const uint8* Src = OutputRGBA8.GetData();
		FLinearColor* Dst = InOutPixels.GetData();
		for (int64 i = 0; i < PixelCount; ++i)
		{
			const FColor C(Src[i * 4 + 0], Src[i * 4 + 1], Src[i * 4 + 2], Src[i * 4 + 3]);
			// sRGB → 线性 转换
			auto sRGBToLinear = [](float In) -> float
			{
				if (In <= 0.04045f) return In / 12.92f;
				return FMath::Pow((In + 0.055f) / 1.055f, 2.4f);
			};
			Dst[i].R = sRGBToLinear(C.R / 255.0f);
			Dst[i].G = sRGBToLinear(C.G / 255.0f);
			Dst[i].B = sRGBToLinear(C.B / 255.0f);
			Dst[i].A = C.A / 255.0f;
		}
	}

	return true;
}
