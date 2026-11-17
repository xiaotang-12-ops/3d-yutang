// 省览保留原巡游；城市双层光区在真实地形材质内运动，仅保留细化单中心定位光。
// 不画城市环绕/半球/接力弧或Surface膜；省态Canvas原路径保持。-codex，2026-10-30
#include "ProvinceEffects.h"
#include "FujianMap.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"

namespace
{
float Smooth(float X){X=FMath::Clamp(X,0.f,1.f);return X*X*(3-2*X);}
float Pulse(float Distance,float Width){const float X=FMath::Clamp(1-FMath::Abs(Distance)/Width,0.f,1.f);return X*X;}
const FLinearColor Cyan(.12f,.82f,1.f),Ice(.68f,.96f,1.f),Gold(.85f,.72f,.48f);
FLinearColor Tint(FLinearColor C,float A){C.A=FMath::Clamp(A,0.f,1.f);return C;}

// 同一帧批量提交透明面，只有有亮度的部分才投影；避免每条光线单独绘制。-codex，2026-10-27
struct FLightPainter
{
    AFujianController* Owner;UCanvas* Canvas;float Scale;TArray<FCanvasUVTri> Triangles;
    FMatrix ViewProjection;FIntRect ViewRect;bool HasProjection=false;
    FLightPainter(AFujianController* C,UCanvas* In):Owner(C),Canvas(In),Scale(FMath::Min(In->ClipX/1920.f,In->ClipY/1080.f))
    {
        Triangles.Reserve(40000);
        // 与引擎ProjectWorldToScreen使用相同投影，只将本帧不变的相机矩阵计算一次。
        // 数万顶点逐个GetProjectionData会重复构造视图，导致面状光效CPU开销过高。-codex，2026-10-27
        const auto* Player=C->GetLocalPlayer();FSceneViewProjectionData Data;
        if(Player&&Player->ViewportClient&&Player->GetProjectionData(Player->ViewportClient->Viewport,Data))
        {ViewProjection=Data.ComputeViewProjectionMatrix();ViewRect=Data.GetConstrainedViewRect();HasProjection=true;}
    }
    bool Project(FVector V,FVector2D& P)const
    {return HasProjection&&FSceneView::ProjectWorldToScreen(V,ViewRect,ViewProjection,P)&&Owner->PostProcessWorldToScreen(V,P,false)&&FMath::IsFinite(P.X)&&FMath::IsFinite(P.Y);}
    void ScreenTri(FVector2D A,FVector2D B,FVector2D C,FLinearColor CA,FLinearColor CB,FLinearColor CC)
    {
        if(FMath::Max3(CA.A,CB.A,CC.A)<.003f)return;
        if((A.X<0&&B.X<0&&C.X<0)||(A.Y<0&&B.Y<0&&C.Y<0)||(A.X>Canvas->ClipX&&B.X>Canvas->ClipX&&C.X>Canvas->ClipX)||(A.Y>Canvas->ClipY&&B.Y>Canvas->ClipY&&C.Y>Canvas->ClipY))return;
        FCanvasUVTri T;T.V0_Pos=A;T.V1_Pos=B;T.V2_Pos=C;T.V0_UV=T.V1_UV=T.V2_UV=FVector2D::ZeroVector;T.V0_Color=CA;T.V1_Color=CB;T.V2_Color=CC;Triangles.Add(T);
    }
    void Tri(FVector A,FVector B,FVector C,FLinearColor CA,FLinearColor CB,FLinearColor CC)
    {
        if(FMath::Max3(CA.A,CB.A,CC.A)<.003f)return;FVector2D P,Q,R;
        if(Project(A,P)&&Project(B,Q)&&Project(C,R)&&(P-Q).Size()<Canvas->ClipX*1.5f&&(P-R).Size()<Canvas->ClipX*1.5f)ScreenTri(P,Q,R,CA,CB,CC);
    }
    void Quad(FVector A,FVector B,FVector C,FVector D,FLinearColor CA,FLinearColor CB,FLinearColor CC,FLinearColor CD)
    {Tri(A,B,C,CA,CB,CC);Tri(A,C,D,CA,CC,CD);}
    void Stroke(FVector2D P,FVector2D Q,FLinearColor Color,float Width,float Alpha)
    {
        const FVector2D V=(Q-P).GetSafeNormal(),N(-V.Y*Width*Scale*.5f,V.X*Width*Scale*.5f);const auto C=Tint(Color,Alpha);
        ScreenTri(P-N,P+N,Q+N,C,C,C);ScreenTri(P-N,Q+N,Q-N,C,C,C);
    }
    void Line(FVector A,FVector B,FLinearColor Color,float Width,float Alpha,bool Glow=true)
    {
        FVector2D P,Q;if(Alpha<.005f||!Project(A,P)||!Project(B,Q)||(P-Q).Size()>Canvas->ClipX*.75f)return;
        if(Glow){Stroke(P,Q,Color,Width+14,Alpha*.035f);Stroke(P,Q,Color,Width+6,Alpha*.13f);}
        Stroke(P,Q,Color,Width,Alpha);
    }
    void Star(FVector V,float Radius,FLinearColor Color,float Alpha)
    {
        FVector2D P;if(!Project(V,P))return;const int32 N=12;
        for(int32 I=0;I<N;I++)
        {
            const float A=2*PI*I/N,B=2*PI*(I+1)/N;
            ScreenTri(P,P+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius*Scale,P+FVector2D(FMath::Cos(B),FMath::Sin(B))*Radius*Scale,Tint(Color,Alpha),Tint(Color,0),Tint(Color,0));
        }
        Stroke(P-FVector2D(Radius*.55f,0)*Scale,P+FVector2D(Radius*.55f,0)*Scale,Color,1,Alpha*.7f);
    }
    void Circle(FVector C,float Radius,float Phase,FLinearColor Color,float Width,float Alpha,bool Dashed=false)
    {
        for(int32 I=0;I<96;I++)
        {
            if(Dashed&&I%12>7)continue;const float A=2*PI*I/96+Phase,B=2*PI*(I+1)/96+Phase;
            Line(C+FVector(FMath::Cos(A),FMath::Sin(A),0)*Radius,C+FVector(FMath::Cos(B),FMath::Sin(B),0)*Radius,Color,Width,Alpha);
        }
    }
    void Flush(){if(!Triangles.IsEmpty()){FCanvasTriangleItem Item(Triangles,GWhiteTexture);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);Triangles.Reset();}}
};
}

void FProvinceEffects::BuildSurface()
{
    auto* C=Owner.Get();if(!C||!C->Map)return;Surface.Reset();
    // 仅从已有真实顶点选点；约60单位一格，避免每帧遍历百万三角形。每市独立，不跨市/岛拼接大面。-codex，2026-10-27
    constexpr float Cell=60.f;
    for(int32 I=0;I<C->Map->Cities.Num();I++)
    {
        const auto& City=C->Map->Cities[I];auto* Mesh=City.Terrain.Get();const auto* S=Mesh?Mesh->GetProcMeshSection(0):nullptr;if(!S)continue;
        TMap<FIntPoint,FVector> Samples;const FTransform Transform=Mesh->GetComponentTransform();
        for(const auto& Vertex:S->ProcVertexBuffer)
        {
            const FVector P=Transform.TransformPosition(Vertex.Position);const FIntPoint K(FMath::RoundToInt(P.X/Cell),FMath::RoundToInt(P.Y/Cell));
            auto* Old=Samples.Find(K);const FVector Grid(K.X*Cell,K.Y*Cell,0);
            if(!Old||FVector::DistSquaredXY(P,Grid)<FVector::DistSquaredXY(*Old,Grid))Samples.Add(K,P);
        }
        for(const auto& Pair:Samples)
        {
            const auto* B=Samples.Find(Pair.Key+FIntPoint(1,0));const auto* D=Samples.Find(Pair.Key+FIntPoint(0,1));const auto* E=Samples.Find(Pair.Key+FIntPoint(1,1));
            if(!B||!D||!E)continue;const FVector A=Pair.Value;
            // 不让抽样面跨过海湾缝隙；窄岛缺格时宁可不画扫光，原地形不会被改动。-codex，2026-10-27
            if(FVector::DistXY(A,*E)>Cell*1.8f||FVector::DistXY(*B,*D)>Cell*1.8f)continue;
            const FVector Lift(0,0,10);Surface.Add({A+Lift,*B+Lift,*E+Lift,I});Surface.Add({A+Lift,*E+Lift,*D+Lift,I});
        }
    }
    UE_LOG(LogTemp,Display,TEXT("ProvinceFX surface patches=%d (read-only terrain samples)"),Surface.Num());
}

void FProvinceEffects::Draw(UCanvas* Canvas)
{
    auto* C=Owner.Get();if(!C||!C->Map||!Canvas||!ProvinceBounds.IsValid)return;
    FLightPainter P(C,Canvas);const int32 Selected=C->Map->SelectedCity;
    const bool Fading=Selected==INDEX_NONE&&C->Map->Cities.IsValidIndex(RetiringCity)&&Time-RetireTime<.6f;
    const int32 Active=Selected!=INDEX_NONE?Selected:(Fading?RetiringCity:INDEX_NONE);
    const float Age=Fading?RetireAge+Time-RetireTime:Time-SelectionTime,Fade=Fading?1-Smooth((Time-RetireTime)/.6f):1.f;
    const float ProvinceSize=ProvinceBounds.GetSize().GetMax();const FVector ProvinceCenter=ProvinceBounds.GetCenter();
    const float Idle=Selected==INDEX_NONE?1.f:.25f;
    const FVector SweepDir(.65,.76,0);const float Span=ProvinceBounds.GetSize().X*.65f+ProvinceBounds.GetSize().Y*.76f;
    const float Sweep=(FMath::Fmod(Time/6.f,1.f)-.5f)*Span*1.3f;
    const FVector ActiveCenter=Active!=INDEX_NONE?C->Map->Cities[Active].Center:FVector::ZeroVector;
    const float D=Active!=INDEX_NONE?float(C->Map->Cities[Active].Bounds.GetSize().GetMax()):1.f;
    const float Radius=FMath::Clamp(D*.43f,220.f,1100.f);

    // 先画贴地宽光潮。它是独立投影覆盖层，峰值限制在低透明，保留山体纹理。-codex，2026-10-27
    auto GroundAlpha=[&](FVector V,int32 City)
    {
        const float Offset=float((V-ProvinceCenter)|SweepDir)-Sweep,Width=ProvinceSize*.085f;
        float A=Active==INDEX_NONE?Idle*(.24f*Pulse(Offset,Offset>0?Width*.25f:Width)+.12f*Pulse(Offset,Width*.17f)):0.f;
        // 悬浮仅提示城市名称，避免稀疏Canvas地表膜与真实山体重影。-codex，2026-11-03
        return FMath::Min(A,.40f);
    };
    // 城市态不叠Canvas地表膜；选区受光由原地形材质承担。省待机保持原路径。-codex，2026-10-30
    if(Active==INDEX_NONE)for(const auto& T:Surface)
    {
        const float A=GroundAlpha(T.A,T.City),B=GroundAlpha(T.B,T.City),E=GroundAlpha(T.C,T.City);
        P.Tri(T.A,T.B,T.C,Tint(Cyan,A),Tint(Cyan,B),Tint(Cyan,E));
    }
    P.Flush();

    // 长尾按世界弧长匀速行进，不按稠密不均的顶点序号跳动；小岛不复制巡游光头。-codex，2026-10-27
    for(const auto& Ring:Rings)
    {
        const bool IsCity=Ring.City==Active&&Active!=INDEX_NONE;
        const bool IsHover=Ring.City==HoverCity&&HoverCity!=INDEX_NONE;
        // 切城只呈现新选择，避免旧新行政界同时叠亮。-codex，2026-10-29
        // 市界只由原地形材质绘制；独立rings高程与三角面不同，禁止叠第二套悬浮轮廓。-codex，2026-11-03
        if(Ring.City!=INDEX_NONE)continue;
        const int32 N=Ring.Points.Num();if(N<5)continue;
        const bool Province=Ring.City==INDEX_NONE;
        const float Size=Province?ProvinceSize:float(C->Map->Cities[Ring.City].Bounds.GetSize().GetMax());
        const bool Large=Ring.Length>Size*.45f;
        const FLinearColor Color=IsCity?Gold:Cyan;
        // hover全轮廓稳定渐入，保留既有HoverAlpha而不追逐指向描绘前沿。-codex，2026-10-29
        const float Strength=Province?(Active==INDEX_NONE?Idle:.12f):(IsHover?HoverAlpha*.42f:Fade*Smooth(Age/1.4f));
        for(int32 I=1;I<N;I++)
        {
            const FVector A=Ring.Points[I-1],B=Ring.Points[I];
            const float Base=Strength*(Province?.08f:(IsHover?.65f:.30f));
            P.Line(A,B,Color,IsHover?2.f:(IsCity?1.8f:1.6f),Base);
            // 市界只沿真实顶边发光；竖起光墙在斜视时会投影到市区内部。省装饰保持。-codex，2026-11-02
            if(Province)
            {
                const float Height=Size*.004f*(Large?1.f:.3f);
                P.Quad(A,B,B+FVector(0,0,Height),A+FVector(0,0,Height),Tint(Color,Base*.6f),Tint(Color,Base*.6f),Tint(Color,0),Tint(Color,0));
            }
        }
        if(Large&&Province&&Active==INDEX_NONE&&!IsHover)
        {
            auto At=[&](float Fraction)
            {
                const float S=FMath::Frac(Fraction+10.f)*Ring.Length;int32 Lo=1,Hi=N-1;
                while(Lo<Hi){const int32 Mid=(Lo+Hi)/2;if(Ring.Arc[Mid]<S)Lo=Mid+1;else Hi=Mid;}
                const float T=(S-Ring.Arc[Lo-1])/FMath::Max(.001f,Ring.Arc[Lo]-Ring.Arc[Lo-1]);
                return FMath::Lerp(Ring.Points[Lo-1],Ring.Points[Lo],T)+FVector(0,0,2);
            };
            const int32 Count=Province?3:2;const float Tail=Province?.13f:.22f;
            for(int32 Trail=0;Trail<Count;Trail++)for(int32 J=0;J<80;J++)
            {
                const float T=J/80.f,H=(J+1)/80.f,Head=Time/(Province?11.f:7.f)+Trail/float(Count);
                const float Alpha=Strength*FMath::Pow(T,1.6f)*(1-Smooth((T-.90f)/.10f));
                const FVector A=At(Head-Tail*(1-T)),B=At(Head-Tail*(1-H));
                const float Width=.15f+.85f*T;
                P.Line(A,B,Color,(Province?18.f:13.f)*Width,Alpha*.28f,false);
                P.Line(A,B,IsCity?Gold:Ice,(Province?5.f:3.5f)*Width,Alpha*.95f);
            }
        }
    }
    P.Flush();

    if(HoverCity!=INDEX_NONE&&HoverLabel.bIsValid)
    {
        const float S=P.Scale;const FVector2D A(HoverLabel.Min.X+12*S,HoverLabel.Max.Y-7*S);
        const FVector2D B(HoverLabel.Max.X-12*S,A.Y);
        P.Stroke(A,B,Cyan,2.f,HoverAlpha*.42f);
    }
    for(int32 I=0;I<C->Map->Cities.Num();I++)
    {
        const FVector Center=C->Map->Cities[I].Center+FVector(0,0,8);const float Phase=FMath::Fmod(Time*.23f+I*.17f,1.f);
        if(Selected==INDEX_NONE)
        {
            P.Circle(Center,40+Phase*150,0,Cyan,1.7f,(1-Phase)*.65f);
        }
    }
    if(Active==INDEX_NONE){P.Flush();return;}

    // 城市也不再画冲天定位柱，保留既有贴地受光与名称提示。-codex，2026-11-03
    P.Flush();
}
