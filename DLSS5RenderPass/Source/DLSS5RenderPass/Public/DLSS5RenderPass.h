//  Copyright MonsterGuoGuo. All Rights Reserved.2026

#pragma once
#include "MoviePipelineDeferredPasses.h"
#include "DLSS5NeuralRender.h"
#include "DLSS5RenderPass.generated.h"

class FDLSS5OutputMerger;

/**
 * 基于 Deferred Rendering（UMoviePipelineDeferredPassBase）的电影渲染通道，
 * 在最终输出帧上应用 NVIDIA DLSS 5 神经渲染（3D-Guided Neural Rendering）细节增强。
 *
 * 工作方式与 HighResolutionRenderPass 中的 DLSS 集成一致：
 * Setup 时把 Movie Pipeline 的 OutputBuilder 替换为带 DLSS 5 处理的 FDLSS5OutputMerger，
 * 仅对本通道输出的最终混合画面做神经网络重绘，蒙版 / ID / 后期材质等数据通道原样透传。
 *
 * 注意：OutputBuilder 在整个 pipeline 生命周期内保持替换状态（每个 Shot 的 Setup 只会
 * 幂等地安装一次），因为 pipeline 在所有通道 Teardown 之后才最终排空 OutputBuilder 的
 * FinishedFrames 队列，提前恢复会丢失尚在队列中的输出帧。
 */
UCLASS(BlueprintType)
class UDLSS5RenderPass : public UMoviePipelineDeferredPassBase
{
	GENERATED_BODY()

public:
	UDLSS5RenderPass();

#if WITH_EDITOR
	virtual FText GetDisplayText() const override
	{
		return NSLOCTEXT("DLSS5RenderPass", "DLSS5RenderPass_Deferred", "DLSS5 Deferred Rendering");
	}
#endif

protected:
	virtual void SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;

public:
	/** 获取 DLSS 5 神经渲染配置（从 UPROPERTY 转换为运行时结构体） */
	FDLSS5Settings GetDLSS5Settings() const;

private:
	/** 将 pipeline 的 OutputBuilder 替换为带 DLSS 5 处理的包装器（幂等，可多 Shot 重复调用） */
	void InstallDLSS5OutputMerger();

public:
	/** 是否启用 DLSS 5 神经渲染细节增强（同分辨率，作用于最终输出画面） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DLSS 5 Neural Render")
	bool bEnableDLSS5 = false;

	/** DLSS 5 渲染风格 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DLSS 5 Neural Render", meta = (EditCondition = "bEnableDLSS5"))
	EDLSS5Style DLSS5Style = EDLSS5Style::Cinematic;

	/** DLSS 强度 (0.0-1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DLSS 5 Neural Render", meta = (EditCondition = "bEnableDLSS5", ClampMin = "0.0", ClampMax = "1.0"))
	float DLSS5Intensity = 0.5f;

	/** 局部色调强度 (0.0-1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DLSS 5 Neural Render", meta = (EditCondition = "bEnableDLSS5", ClampMin = "0.0", ClampMax = "1.0"))
	float DLSS5LocalToneStrength = 0.5f;

	/** 局部结构强度 (0.0-1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DLSS 5 Neural Render", meta = (EditCondition = "bEnableDLSS5", ClampMin = "0.0", ClampMax = "1.0"))
	float DLSS5LocalStructureStrength = 0.5f;

	/** 皮肤结构强度 (0.0-1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DLSS 5 Neural Render", meta = (EditCondition = "bEnableDLSS5", ClampMin = "0.0", ClampMax = "1.0"))
	float DLSS5SkinStructureStrength = 0.1f;

	/** nvngx_dlssnr.dll 与 dlssnr_host.dll 所在目录。留空则依次搜索本插件与 HighResolutionRenderPass 插件的 Binaries/Win64 目录。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "DLSS 5 Neural Render", meta = (EditCondition = "bEnableDLSS5"))
	FString DLSS5RuntimeDirectory;

private:
	/** Setup 前的原始 OutputBuilder。保持其存活（管道不再恢复使用它，但防止其他弱引用悬空）。 */
	TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OriginalOutputBuilder;

	/** 已安装的 DLSS 包装器，用于多 Shot Setup 的幂等判断 */
	TWeakPtr<FDLSS5OutputMerger, ESPMode::ThreadSafe> InstalledWrapper;

	/** 当前 DLSS 配置快照（Setup 时缓存，供 OutputMerger 使用） */
	FDLSS5Settings CachedDLSS5Settings;
};
