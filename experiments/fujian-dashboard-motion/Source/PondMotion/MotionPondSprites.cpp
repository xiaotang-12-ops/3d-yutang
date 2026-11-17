// Slate不是UObject，Brush资源指针不会代替GC引用；由本对象显式强持有贴图和MID。-codex，2026-11-03
#include "MotionPondSprites.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogMotionPondSprites, Log, All);

FMotionPondSprites::FMotionPondSprites()
{
    Material.Reset(LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Motion/PondSprites/M_PondSpriteWater.M_PondSpriteWater")));
    if (Material.IsValid() && !Material->IsUIMaterial())
    {
        UE_LOG(LogMotionPondSprites, Error, TEXT("Pond sprite material is not UI domain; using static PNGs"));
        Material.Reset();
    }
    if (!Material.IsValid())
    {
        UE_LOG(LogMotionPondSprites, Warning, TEXT("Pond sprite UI material unavailable; using static PNGs"));
    }

    static const TCHAR* Paths[VariantCount] = {
        TEXT("/Game/Motion/PondSprites/T_PondHero.T_PondHero"),
        TEXT("/Game/Motion/PondSprites/T_PondOval.T_PondOval"),
        TEXT("/Game/Motion/PondSprites/T_PondRect.T_PondRect"),
        TEXT("/Game/Motion/PondSprites/T_PondSmartBean.T_PondSmartBean"),
        TEXT("/Game/Motion/PondSprites/T_PondSmartRound.T_PondSmartRound"),
        TEXT("/Game/Motion/PondSprites/T_PondSmartRect.T_PondSmartRect")
    };
    for (int32 Index = 0; Index < VariantCount; ++Index)
    {
        Textures[Index].Reset(LoadObject<UTexture2D>(nullptr, Paths[Index]));
        if (!Textures[Index].IsValid() || Textures[Index]->GetSizeX() <= 0 || Textures[Index]->GetSizeY() <= 0)
        {
            UE_LOG(LogMotionPondSprites, Error, TEXT("Pond sprite texture missing or empty: %s"), Paths[Index]);
            Textures[Index].Reset();
            continue;
        }
        UObject* Resource = Textures[Index].Get();
        if (Material.IsValid())
        {
            Instances[Index].Reset(UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage()));
            if (Instances[Index].IsValid())
            {
                Instances[Index]->SetTextureParameterValue(TEXT("PondTexture"), Textures[Index].Get());
                Instances[Index]->SetScalarParameterValue(TEXT("ShapeType"), static_cast<float>(Index));
                Instances[Index]->SetScalarParameterValue(TEXT("Phase"), static_cast<float>(Index) * 2.173f);
                Resource = Instances[Index].Get();
            }
            else
            {
                UE_LOG(LogMotionPondSprites, Warning, TEXT("Pond sprite MID failed for variant %d; using static PNG"), Index);
            }
        }
        FSlateBrush& Result = Brushes[Index];
        Result.DrawAs = ESlateBrushDrawType::Image;
        Result.ImageType = ESlateBrushImageType::FullColor;
        Result.Tiling = ESlateBrushTileType::NoTile;
        Result.SetImageSize(FVector2D(Textures[Index]->GetSizeX(), Textures[Index]->GetSizeY()));
        Result.SetResourceObject(Resource);
    }
}

FMotionPondSprites::~FMotionPondSprites() = default;

bool FMotionPondSprites::IsReady() const
{
    for (const auto& Texture : Textures)
    {
        if (!Texture.IsValid()) return false;
    }
    return true;
}

const FSlateBrush* FMotionPondSprites::Brush(int32 Variant) const
{
    return Variant >= 0 && Variant < VariantCount && Textures[Variant].IsValid() ? &Brushes[Variant] : nullptr;
}

FVector2D FMotionPondSprites::AspectSize(int32 Variant) const
{
    return Variant >= 0 && Variant < VariantCount && Textures[Variant].IsValid()
        ? FVector2D(Textures[Variant]->GetSizeX(), Textures[Variant]->GetSizeY()) : FVector2D::ZeroVector;
}

FVector2D FMotionPondSprites::PresentationSize(int32 Variant) const
{
    // 保留调用API，但尺寸始终使用原图宽高比；界面只做等比contain，不能纵向压扁树和设备。
    // manifest六款系数均为1，城市/详情的容器布局由各自绘制职责维护。-codex，2026-11-03
    return AspectSize(Variant);
}
