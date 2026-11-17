// 本次调研按主体计数；面积使用十分亩整数聚合，含水库，不推算产量/金额。-codex，2026-10-28
#include "MotionData.h"
bool FMotionData::Load(){const bool Ok=Survey.Load(Xinluo::DataPath());Error=Survey.Error;return Ok;}
FMotionSnapshot FMotionData::Snapshot(int32 City,int32 Site)const
{
 FMotionSnapshot S;S.City=City;S.Site=Site;S.Title=City>=0?TEXT("新罗区"):TEXT("福建省");S.Id=City>=0?TEXT("350802"):TEXT("FUJIAN");
 if(City<0)return S;
 const TArray<FString> Order={TEXT("鳗鲡"),TEXT("草鱼"),TEXT("鲢鱼"),TEXT("淡水鲈鱼"),TEXT("乌鳢"),TEXT("鲟鱼"),TEXT("其他")};
 for(const auto& K:Order)S.SpeciesValues.Add({K,0});S.Modes={{TEXT("池塘养殖"),0},{TEXT("其他"),0}};
 for(int32 I=0;I<Survey.Subjects.Num();++I)
 {
  if(Site>=0&&I!=Site)continue;const auto& R=Survey.Subjects[I];++S.Count;S.AreaMuTenths+=R.AreaMuTenths;
  for(auto& G:S.SpeciesValues)if(G.Key==R.Species)++G.Value;for(auto& G:S.Modes)if(G.Key==R.Mode)++G.Value;
  if(Site==I){S.Title=R.Name;S.Id=R.Id;}
 }
 return S;
}
