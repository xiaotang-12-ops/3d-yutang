// B展示层独占的无鱼PNG与UI水面材质；固定六个MID，不参与业务数据或逐帧资源创建。-codex，2026-11-03
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class UTexture2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;

class FMotionPondSprites final
{
public:
    static constexpr int32 VariantCount = 6;
    FMotionPondSprites();
    ~FMotionPondSprites();
    bool IsReady() const;
    const FSlateBrush* Brush(int32 Variant) const;
    FVector2D AspectSize(int32 Variant) const;
    FVector2D PresentationSize(int32 Variant) const;

private:
    TStrongObjectPtr<UMaterialInterface> Material;
    TStrongObjectPtr<UTexture2D> Textures[VariantCount];
    TStrongObjectPtr<UMaterialInstanceDynamic> Instances[VariantCount];
    FSlateBrush Brushes[VariantCount];
};
