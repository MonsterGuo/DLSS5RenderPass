//Copyright MonsterGuoGuo. All Rights Reserved.2026
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "MoviePipelineOutputBuilder.h"
#include "MovieRenderPipelineDataTypes.h"
#include "DLSS5NeuralRender.h"

/**
 * 继承引擎的 FMoviePipelineOutputMerger，作为 Movie Pipeline 的 OutputBuilder 使用。
 * 在渲染通道的最终混合帧送入原生输出流程之前，对主通道应用 DLSS 5 神经渲染；
 * 其他通道（蒙版、ID、后期材质等数据输出）原样透传，不做神经网络重绘。
 */
class FDLSS5OutputMerger : public FMoviePipelineOutputMerger
{
public:
	FDLSS5OutputMerger(UMoviePipeline* InOwningMoviePipeline, const FDLSS5Settings& InDLSSSettings, const FMoviePipelinePassIdentifier& InMainPassIdentifier);

	virtual void OnCompleteRenderPassDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData) override;

private:
	/** 对最终输出帧应用 DLSS 5（线性 FLinearColor 像素），处理失败或未启用时返回 false 且不修改数据 */
	bool ApplyDLSS(TArray64<FLinearColor>& InOutPixels, const FIntPoint& InSize, int32 InFrameNumber);

	/** DLSS 配置 */
	FDLSS5Settings DLSSSettings;

	/** 主通道标识。DLSS 只作用于主通道的最终画面。 */
	FMoviePipelinePassIdentifier MainPassIdentifier;

	/** DLSS 处理器（懒加载，首次使用时初始化） */
	TUniquePtr<FDLSS5NeuralRenderProcessor> DLSSProcessor;

	/** DLSS 是否已尝试初始化（用于避免重复失败重试） */
	bool bDLSSInitAttempted = false;

	/** 上一帧输出帧编号，用于判断是否需要重置 DLSS 时序历史 */
	int32 LastOutputFrameNumber = -1;

	/**
	 * 序列化懒加载初始化、Resize 与逐帧处理。OnCompleteRenderPassDataAvailable_AnyThread
	 * 会在多个任务线程并发调用：首帧初始化（DLL 加载 + NGX Feature 创建）耗时数秒，
	 * 期间到达的帧会看到半初始化状态（Width=0）而误走 Resize 分支并被跳过。
	 */
	FCriticalSection DLSSMutex;
};
