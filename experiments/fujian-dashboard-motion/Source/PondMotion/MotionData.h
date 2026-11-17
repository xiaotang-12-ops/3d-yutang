// 展示层只读取共用真实调查契约；不保留模拟经营指标作为缺省值。-codex，2026-10-28
#pragma once
#include "XinluoSurveyData.h"
using FMotionSite=Xinluo::FSubject;
struct FMotionSnapshot
{
 FString Title,Id;int32 City=-1,Site=-1,Count=0;int64 AreaMuTenths=0;
 TArray<TPair<FString,int64>> SpeciesValues,Modes;
};
struct FMotionData
{
 Xinluo::FDataset Survey;FString Error;
 bool Load();FMotionSnapshot Snapshot(int32 City,int32 Site=-1) const;
};
