//  Copyright MonsterGuoGuo. All Rights Reserved.2026

#include "DLSS5RenderPass.h"
#include "DLSS5OutputMerger.h"
#include "MoviePipeline.h"

#define LOCTEXT_NAMESPACE "DLSS5RenderPass"

UDLSS5RenderPass::UDLSS5RenderPass()
{
	// UMoviePipelineDeferredPassBase 构造函数已设置 PassIdentifier
}

FDLSS5Settings UDLSS5RenderPass::GetDLSS5Settings() const
{
	FDLSS5Settings Settings;
	Settings.bEnabled = bEnableDLSS5;
	Settings.Style = DLSS5Style;
	Settings.Intensity = DLSS5Intensity;
	Settings.LocalToneStrength = DLSS5LocalToneStrength;
	Settings.LocalStructureStrength = DLSS5LocalStructureStrength;
	Settings.SkinStructureStrength = DLSS5SkinStructureStrength;
	Settings.RuntimeDirectory = DLSS5RuntimeDirectory;
	return Settings;
}

void UDLSS5RenderPass::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InPassInitSettings);

	// DLSS 5 只在本通道启用时接管输出管线。每个 Shot 都会调用 Setup，
	// InstallDLSS5OutputMerger 内部做幂等处理，已安装时直接跳过。
	if (bEnableDLSS5)
	{
		InstallDLSS5OutputMerger();
	}
}

void UDLSS5RenderPass::InstallDLSS5OutputMerger()
{
	UMoviePipeline* Pipeline = GetPipeline();
	if (!Pipeline || !Pipeline->OutputBuilder.IsValid())
	{
		return;
	}

	// 已安装（后续 Shot 的 Setup 再次执行到这里）时跳过，
	// 保持同一个包装器以延续 DLSS 时序历史与输出队列。
	if (InstalledWrapper.IsValid() && Pipeline->OutputBuilder == InstalledWrapper.Pin())
	{
		return;
	}

	CachedDLSS5Settings = GetDLSS5Settings();
	CachedDLSS5Settings.bEnabled = true;

	OriginalOutputBuilder = Pipeline->OutputBuilder;

	TSharedRef<FDLSS5OutputMerger, ESPMode::ThreadSafe> Wrapper =
		MakeShared<FDLSS5OutputMerger, ESPMode::ThreadSafe>(Pipeline, CachedDLSS5Settings, PassIdentifier);
	InstalledWrapper = Wrapper;

	Pipeline->OutputBuilder = Wrapper;
}

#undef LOCTEXT_NAMESPACE
