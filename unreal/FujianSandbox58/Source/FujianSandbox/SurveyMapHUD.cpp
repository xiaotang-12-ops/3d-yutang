// 龙岩渔场总览：远景七处代表波纹，放大后补齐屏内各塘提示。
// 点选光柱复用世界锚点与抵达淡出，单场资料仅保留右侧一张卡。-codex，2026-11-03
#include "FujianMap.h"
#include "XinluoSurveyControl.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"

namespace
{
bool Hit(const FBox2D& B,FVector2D P){return B.bIsValid&&B.IsInsideOrOn(P);}
constexpr int32 RippleSiteCount=7;
constexpr int32 RippleSegments=64;
constexpr float RipplePeriod=2.8f;
float EffectEase(float T){T=FMath::Clamp(T,0.f,1.f);return T*T*(3.f-2.f*T);}
}

void AFujianHUD::DrawSurvey()
{
    auto* C=Cast<AFujianController>(GetOwningPlayerController());
    if(!C||!C->SurveyControl||C->SurveyControl->Navigation().City!=Xinluo::CityCode){SurveyHovered.Empty();SurveyNearLabels.Empty();SurveyLabelAlpha.Empty();return;}
    const auto& D=C->SurveyControl->Dataset();const auto& N=C->SurveyControl->Navigation();
    const float Scale=FMath::Min(Canvas->ClipX/1920.f,Canvas->ClipY/1080.f);
    const FVector2D Offset((Canvas->ClipX-1920*Scale)*.5f,(Canvas->ClipY-1080*Scale)*.5f);
    auto Point=[&](float X,float Y){return Offset+FVector2D(X,Y)*Scale;};
    auto Box=[&](float X,float Y,float W,float H){return FBox2D(Point(X,Y),Point(X+W,Y+H));};
    static const auto Font=MakeShared<FCompositeFont>(NAME_None,FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const float Probe=FMath::Max(float(Measure->Measure(FStringView(TEXT("国Ag")),FSlateFontInfo(Font,20)).Y),1.f);
    const FLinearColor White=FLinearColor::FromSRGBColor(FColor(232,242,244)),Muted=FLinearColor::FromSRGBColor(FColor(169,190,197));
    const FLinearColor Mint=FLinearColor::FromSRGBColor(FColor(134,228,212)),Panel=FLinearColor::FromSRGBColor(FColor(11,32,41));
    auto Rect=[&](float X,float Y,float W,float H,FLinearColor Col){const auto P=Point(X,Y);DrawRect(Col,P.X,P.Y,W*Scale,H*Scale);};
    // 原生实看20/Probe只对齐度量框、可见字形偏小；沿用28因子校准字形，行距保留真实字框，长名不缩字。-codex，2026-11-03
    auto FontInfo=[&](float Size){return FSlateFontInfo(Font,FMath::Max(1.f,28.f*Size*Scale/Probe));};
    auto LineHeight=[&](float Size){return FMath::Max(Size+4.f,float(Measure->Measure(FStringView(TEXT("国Ag")),FontInfo(Size)).Y)/Scale);};
    auto Wrap=[&](const FString& Value,float Size,float Width)
    {
        TArray<FString> Lines;FString Line;const auto Info=FontInfo(Size);
        for(TCHAR Ch:Value)
        {
            if(Ch==TEXT('\n')){Lines.Add(Line);Line.Empty();continue;}
            if(!Line.IsEmpty()&&Measure->Measure(Line+FString::Chr(Ch),Info).X>Width*Scale)
            {
                // 闭括号等避头标点与前一字一起移行，避免全名末尾仅一个“）”；不丢字、不超宽硬塞。-codex，2026-11-03
                const bool AvoidHead=FString(TEXT("）)]】》」』〉〕｝，。、：；？！％‰")).Contains(FString::Chr(Ch));
                if(AvoidHead&&Line.Len()>1){const FString Carry=Line.Right(1);Line.LeftChopInline(1);Lines.Add(Line);Line=Carry;}
                else {Lines.Add(Line);Line.Empty();}
            }
            Line.AppendChar(Ch);
        }
        if(!Line.IsEmpty())Lines.Add(Line);return Lines;
    };
    auto Text=[&](const FString& Value,float X,float Y,float Size,FLinearColor Col,float Width)
    {
        const auto Info=FontInfo(Size);const auto Lines=Wrap(Value,Size,Width);const float Advance=LineHeight(Size);float Row=0;
        for(const auto& Line:Lines){FCanvasTextItem T(Point(X,Y+Row),FText::FromString(Line),GEngine->GetSmallFont(),Col);T.SlateFontInfo=Info;Canvas->DrawItem(T);Row+=Advance;}
        return Row;
    };

    // 与geo.js及ToUnreal同一映射；只在数据版本变化时拾取现有地形高程。
    // 无交点明确留作资料入口，绝不使用固定高度冒充定位成功。-codex，2026-10-29
    const FString GeometryKey=D.Version+TEXT(":")+FString::FromInt(C->Map->SurveyFarmGeometryRevision());
    if(SurveyPointVersion!=GeometryKey)
    {
        SurveyWorldPoints.Empty();SurveyPointVersion=GeometryKey;
        for(const auto& S:D.Subjects)if(S.HasCoordinate)
        {
            FVector P((S.Latitude-26.05)*1000.,(S.Longitude-118.2)*1000.*FMath::Cos(FMath::DegreesToRadians(26.05)),0);
            // 局部塘建成后只更新锚点Z，XY仍取原经纬度；近景不用旧+8世界单位悬浮偏移。-codex，2026-10-30
            FVector FarmCenter;float FarmRadius=0;
            bool Found=C->Map->GetSurveyFarmFocus(S.Id,FarmCenter,FarmRadius);
            if(Found)P.Z=FarmCenter.Z+FMath::Min(FarmRadius*.03f,.08f);
            for(const auto& City:C->Map->Cities)if(!Found&&City.Terrain.IsValid())
            {
                FHitResult H;
                if(City.Terrain->LineTraceComponent(H,FVector(P.X,P.Y,5000),FVector(P.X,P.Y,-100),FCollisionQueryParams(SCENE_QUERY_STAT(SurveyGround),true)))
                {P.Z=H.ImpactPoint.Z+8;Found=true;break;}
            }
            if(Found)SurveyWorldPoints.Add(S.Id,P);
            UE_LOG(LogTemp,Display,TEXT("SurveyAnchor id=%s found=%d lon=%.8f lat=%.8f world=%s"),*S.Id,Found,S.Longitude,S.Latitude,*P.ToString());
        }
        // 从中心附近起步，每次取离已选集合最远的原场址，七点稳定覆盖南北/东西。
        // 023原资料位置待核实，不选作常驻代表点；原数据仍计入总览。-codex，2026-10-31
        SurveyRippleIds.Empty();TArray<FString> Candidates;FVector Centroid=FVector::ZeroVector;
        for(const auto& S:D.Subjects)if(S.Id!=TEXT("xinluo-023"))if(const FVector* P=SurveyWorldPoints.Find(S.Id)){Candidates.Add(S.Id);Centroid+=*P;}
        if(!Candidates.IsEmpty())
        {
            Centroid/=Candidates.Num();double Closest=TNumericLimits<double>::Max();FString First;
            for(const auto& Id:Candidates){const double Distance=FVector::DistSquaredXY(SurveyWorldPoints[Id],Centroid);if(Distance<Closest){Closest=Distance;First=Id;}}
            SurveyRippleIds.Add(First);
            while(SurveyRippleIds.Num()<FMath::Min(RippleSiteCount,Candidates.Num()))
            {
                double Farthest=-1;FString Next;
                for(const auto& Id:Candidates)if(!SurveyRippleIds.Contains(Id))
                {
                    double Nearest=TNumericLimits<double>::Max();for(const auto& Selected:SurveyRippleIds)Nearest=FMath::Min(Nearest,FVector::DistSquaredXY(SurveyWorldPoints[Id],SurveyWorldPoints[Selected]));
                    if(Nearest>Farthest){Farthest=Nearest;Next=Id;}
                }
                if(Next.IsEmpty())break;SurveyRippleIds.Add(Next);
            }
        }
        TArray<FString> RippleIds=SurveyRippleIds.Array();RippleIds.Sort();
        UE_LOG(LogTemp,Display,TEXT("SurveyRipple sites=%d ids=%s"),RippleIds.Num(),*FString::Join(RippleIds,TEXT(",")));
    }

    Text(TEXT("龙岩 · 新罗养殖分布"),260,27,24,White,650);
    SurveyHovered.Empty();SurveyNearLabels.Empty();SurveyLabelAlpha.Empty();
    SurveyRows.Init(FBox2D(ForceInit),D.Subjects.Num());SurveyPointHits.Init(FBox2D(ForceInit),D.Subjects.Num());
    SurveyOverview=Box(1740,24,156,42);Text(TEXT("数据总览"),1763,34,20,White,130);
    if(N.View==TEXT("subject")||N.View==TEXT("overview")){SurveyBack=Box(1500,24,220,42);Text(N.View==TEXT("overview")&&N.ReturnView==TEXT("subject")?TEXT("返回渔场查看"):TEXT("返回养殖分布"),1514,34,19,Muted,210);}
    Text(TEXT("面积为主体养殖面积 · 原表坐标未注明坐标系 · 位置仅供调研参考"),1030,1044,14,Muted,850);
    int64 TotalArea=0;int32 Located=0;
    for(const auto& S:D.Subjects){TotalArea+=S.AreaMuTenths;Located+=S.HasCoordinate?1:0;SurveyIds.Add(S.Id);}
    SurveyFirstRow=FMath::Clamp(SurveyFirstRow,0,FMath::Max(0,D.Subjects.Num()-SurveyVisibleRows));
    // 列表与实际塘面共用点选入口；资料仅显示当前选中主体，悬浮不弹第二张卡。-codex，2026-11-03
    if(bSurveyListExpanded)
    {
        constexpr float PanelX=1530,PanelWidth=374,NameX=1600,NameWidth=282,ListTop=266,ListHeight=350,NameSize=20;
        SurveySelector=Box(PanelX,108,PanelWidth,560);Rect(PanelX,108,PanelWidth,560,Panel.CopyWithNewOpacity(.97f));
        Text(TEXT("养殖总览"),1554,126,24,White,220);
        SurveyToggle=Box(1820,118,68,40);Text(TEXT("收起"),1834,130,17,Muted,56);
        Text(FString::Printf(TEXT("%d 家"),D.Subjects.Num()),1554,176,28,White,105);
        Text(Xinluo::Area(TotalArea)+TEXT(" 亩"),1684,176,28,White,200);
        Text(FString::Printf(TEXT("%d 处已定位 · %d 家暂无坐标"),Located,D.Subjects.Num()-Located),1554,222,16,Muted,326);
        Rect(1554,252,326,1,Muted.CopyWithNewOpacity(.24f));
        static TArray<float> RowHeights;const float NameAdvance=LineHeight(NameSize);
        // 一次记录全部真实名称及最拥挤六行窗口，供原生截图核验；超预算只报警，不偷偷缩字或省名。-codex，2026-11-03
        static FString AuditedLayout;const FString LayoutKey=D.Version+FString::Printf(TEXT(":%d:%.4f:%.3f"),D.Subjects.Num(),Scale,Probe);
        if(AuditedLayout!=LayoutKey)
        {
            AuditedLayout=LayoutKey;RowHeights.Empty();float MaxWindow=0;int32 MaxFirst=0;
            for(const auto& S:D.Subjects)RowHeights.Add(FMath::Max(42.f,Wrap(S.Name,NameSize,NameWidth).Num()*NameAdvance+10.f));
            for(int32 I=0;I<D.Subjects.Num();++I)
            {
                float Window=0;for(int32 J=I;J<FMath::Min(I+SurveyVisibleRows,D.Subjects.Num());++J)Window+=RowHeights[J];
                if(Window>MaxWindow){MaxWindow=Window;MaxFirst=I;}
                UE_LOG(LogTemp,Display,TEXT("SurveyTextLayout id=%s name=%s width=%.2f lines=%d row=%.2f"),*D.Subjects[I].Id,*D.Subjects[I].Name,float(Measure->Measure(D.Subjects[I].Name,FontInfo(NameSize)).X)/Scale,Wrap(D.Subjects[I].Name,NameSize,NameWidth).Num(),RowHeights[I]);
            }
            UE_LOG(LogTemp,Display,TEXT("SurveyTextLayout viewport=%.0fx%.0f probe=%.2f size=%.2f advance=%.2f nameWidth=%.2f maxSix=%.2f budget=%.2f first=%d fits=%d"),Canvas->ClipX,Canvas->ClipY,Probe,FontInfo(NameSize).Size,NameAdvance,NameWidth,MaxWindow,ListHeight,MaxFirst+1,MaxWindow<=ListHeight);
        }
        float VisibleHeight=0;for(int32 I=SurveyFirstRow;I<FMath::Min(SurveyFirstRow+SurveyVisibleRows,RowHeights.Num());++I)VisibleHeight+=RowHeights[I];
        const float RowGap=FMath::Max(0.f,(ListHeight-VisibleHeight)/SurveyVisibleRows);float Y=ListTop;
        for(int32 Row=0;Row<SurveyVisibleRows;++Row)
        {
            const int32 I=SurveyFirstRow+Row;if(!D.Subjects.IsValidIndex(I))break;
            const auto& S=D.Subjects[I];const float Height=RowHeights[I]+RowGap;
            const bool Selected=N.View==TEXT("subject")&&N.Subject==S.Id;
            SurveyRows[I]=Box(1540,Y,354,Height);
            if(Selected){Rect(1540,Y,354,Height,Mint.CopyWithNewOpacity(.075f));Rect(1540,Y+3,3,Height-6,Mint);}
            const float TextY=Y+(Height-Wrap(S.Name,NameSize,NameWidth).Num()*NameAdvance)*.5f;
            Text(FString::Printf(TEXT("%02d"),S.Ordinal),1554,TextY+1,18,Selected?Mint:Muted,30);
            Text(S.Name,NameX,TextY,NameSize,Selected?Mint:White,NameWidth);Y+=Height;
        }
        if(D.Subjects.Num()>SurveyVisibleRows)
        {
            const float ThumbHeight=ListHeight*SurveyVisibleRows/D.Subjects.Num();
            const float ThumbY=ListTop+(ListHeight-ThumbHeight)*SurveyFirstRow/(D.Subjects.Num()-SurveyVisibleRows);
            Rect(1888,ListTop,2,ListHeight,Muted.CopyWithNewOpacity(.18f));Rect(1888,ThumbY,2,ThumbHeight,Muted.CopyWithNewOpacity(.8f));
            Text(TEXT("滚轮浏览全部名单"),1554,632,16,Muted,300);
        }
    }
    else {SurveyToggle=Box(1770,112,126,40);Text(TEXT("养殖总览"),1782,124,16,White,110);}

    // 地图不叠加第二份名称。模型实际投影范围用于点选，小塘保留最小触点；右侧名单覆盖所有26家。-codex，2026-11-03
    for(int32 I=0;I<D.Subjects.Num();++I)
    {
        const auto& S=D.Subjects[I];if(!C->Map->IsSurveyFarmDisplayed(S.Id))continue;
        FVector Anchor;FVector2D Half;if(!C->Map->GetSurveyFarmWaterAnchor(S.Id,Anchor,Half))continue;
        FVector2D P;if(!C->ProjectWorldLocationToScreen(Anchor,P))continue;const auto Q=(P-Offset)/Scale;
        if(Q.X<24||Q.X>1896||Q.Y<100||Q.Y>1010)continue;
        FBox2D Bounds=Box(Q.X-10,Q.Y-10,20,20);
        if(SurveySelector.bIsValid&&Bounds.Intersect(SurveySelector))continue;
        SurveyPointHits[I]=Bounds;
    }
    const int32 SelectedIndex=D.Find(N.Subject);
    if(N.View==TEXT("subject")&&D.Subjects.IsValidIndex(SelectedIndex))
    {
        const auto& S=D.Subjects[SelectedIndex];SurveyPanel=Box(1530,684,374,300);
        Rect(1530,684,374,300,Panel.CopyWithNewOpacity(.98f));
        float Y=704;Y+=Text(S.Name,1554,Y,22,White,326)+12;
        Y+=Text(Xinluo::Area(S.AreaMuTenths)+TEXT(" 亩"),1554,Y,28,White,326)+12;
        auto Field=[&](const FString& Label,const FString& Value)
        {Text(Label,1554,Y,18,Muted,94);Y+=FMath::Max(LineHeight(18),Text(Value,1658,Y,20,White,222))+8;};
        Field(TEXT("品种"),S.Species);Field(TEXT("养殖模式"),S.Mode);Field(TEXT("地址"),S.Address);
        if(!C->SurveyCameraStatus().IsEmpty())Text(C->SurveyCameraStatus(),1554,Y+4,14,Muted,326);
        else if(!S.HasCoordinate)Text(TEXT("暂无坐标 · 仅显示资料"),1554,Y+4,14,Muted,326);
    }
    // 七处原位波纹恢复原有的可辨尺度与扩散节奏；中心始终取塘面世界锚点。-codex，2026-11-03
    const double Now=GetWorld()->GetTimeSeconds();
    FVector Eye;FRotator EyeRotation;C->GetPlayerViewPoint(Eye,EyeRotation);
    FSceneViewProjectionData Projection;const auto* Player=C->GetLocalPlayer();
    if(!Player||!Player->ViewportClient||!Player->GetProjectionData(Player->ViewportClient->Viewport,Projection))return;
    const FMatrix Matrix=Projection.ComputeViewProjectionMatrix();TArray<FCanvasUVTri> LightTriangles;LightTriangles.Reserve(13000);
    auto Project=[&](const FVector& P,FVector2D& Screen)
    {return FSceneView::ProjectWorldToScreen(P,Projection.GetConstrainedViewRect(),Matrix,Screen)&&C->PostProcessWorldToScreen(P,Screen,false)&&FMath::IsFinite(Screen.X)&&FMath::IsFinite(Screen.Y);};
    FBox2D TitleBounds=Box(250,18,670,65),FootnoteBounds=Box(1020,1036,876,34);
    auto CoveredByControls=[&](const FBox2D& Bounds)
    {for(const FBox2D* B:{&HomeButton,&SurveyBack,&SurveyOverview,&SurveySelector,&SurveyToggle,&SurveyPanel,&TitleBounds,&FootnoteBounds})if(B->bIsValid&&Bounds.Intersect(*B))return true;return false;};
    auto Tri=[&](FVector2D A,FVector2D B,FVector2D E,FLinearColor CA,FLinearColor CB,FLinearColor CE)
    {
        FBox2D Bounds(ForceInit);Bounds+=A;Bounds+=B;Bounds+=E;
        if(Bounds.Max.X<0||Bounds.Min.X>Canvas->ClipX||Bounds.Max.Y<0||Bounds.Min.Y>Canvas->ClipY||CoveredByControls(Bounds))return;
        FCanvasUVTri T;T.V0_Pos=A;T.V1_Pos=B;T.V2_Pos=E;T.V0_UV=T.V1_UV=T.V2_UV=FVector2D::ZeroVector;T.V0_Color=CA;T.V1_Color=CB;T.V2_Color=CE;LightTriangles.Add(T);
    };
    TArray<FCanvasUVTri> BeamTriangles;bool DrawingBeam=false;
    if(N.View==TEXT("subject")&&C->SurveyBeaconAge(N.Subject)>=0&&C->SurveyArrivalAge(N.Subject)<FSurveyCameraFlight::FadeDuration)BeamTriangles.Reserve(24000);
    auto ProjectLight=Project;
    auto LightTri=[&](FVector2D A,FVector2D B,FVector2D E,FLinearColor CA,FLinearColor CB,FLinearColor CE)
    {
        if(FMath::Max3(CA.A,CB.A,CE.A)<.003f)return;
        FBox2D Bounds(ForceInit);Bounds+=A;Bounds+=B;Bounds+=E;
        if(Bounds.Max.X<0||Bounds.Min.X>Canvas->ClipX||CoveredByControls(Bounds)||(SurveyPanel.bIsValid&&Bounds.Intersect(SurveyPanel)))return;
        struct FLightVertex{FVector2D P;FLinearColor C;};TArray<FLightVertex,TInlineAllocator<8>> Polygon{{A,CA},{B,CB},{E,CE}};
        // 大世界光效靠近后自然出画，裁三角形边界而非整片丢掉，避免镜头运动中断闪。-codex，2026-11-02
        if(DrawingBeam&&(Bounds.Min.X<0||Bounds.Max.X>Canvas->ClipX||Bounds.Min.Y<0||Bounds.Max.Y>Canvas->ClipY))for(int32 Edge=0;Edge<4&&!Polygon.IsEmpty();++Edge)
        {
            const float Limit=Edge==0?0.f:Edge==1?Canvas->ClipX:Edge==2?0.f:Canvas->ClipY;
            auto Value=[&](const FLightVertex& V){return float(Edge<2?V.P.X:V.P.Y)-Limit;};
            auto Inside=[&](float V){return Edge==0||Edge==2?V>=0:V<=0;};TArray<FLightVertex,TInlineAllocator<8>> Clipped;
            for(int32 K=0;K<Polygon.Num();++K)
            {
                const auto& U=Polygon[K];const auto& V=Polygon[(K+1)%Polygon.Num()];const float DU=Value(U),DV=Value(V);
                if(Inside(DU))Clipped.Add(U);
                if(Inside(DU)!=Inside(DV)){const float T=DU/(DU-DV);Clipped.Add({FMath::Lerp(U.P,V.P,T),FMath::Lerp(U.C,V.C,T)});}
            }
            Polygon=MoveTemp(Clipped);
        }
        for(int32 K=1;K+1<Polygon.Num();++K)
        {
            const auto& U=Polygon[0];const auto& V=Polygon[K];const auto& W=Polygon[K+1];
            FCanvasUVTri T;T.V0_Pos=U.P;T.V1_Pos=V.P;T.V2_Pos=W.P;T.V0_UV=T.V1_UV=T.V2_UV=FVector2D::ZeroVector;
            // UE Additive为One+One，必须预乘Alpha，否则淡出仍留下不透明亮片。-codex，2026-11-02
            T.V0_Color=DrawingBeam?U.C*U.C.A:U.C;T.V1_Color=DrawingBeam?V.C*V.C.A:V.C;T.V2_Color=DrawingBeam?W.C*W.C.A:W.C;
            if(DrawingBeam)BeamTriangles.Add(T);else LightTriangles.Add(T);
        }
    };
    auto LightQuad=[&](FVector A,FVector B,FVector E,FVector F,FLinearColor CA,FLinearColor CB,FLinearColor CE,FLinearColor CF)
    {FVector2D P,Q,R,S;if(ProjectLight(A,P)&&ProjectLight(B,Q)&&ProjectLight(E,R)&&ProjectLight(F,S)){LightTri(P,Q,R,CA,CB,CE);LightTri(P,R,S,CA,CE,CF);}};
    const FLinearColor NormalWave=FLinearColor::FromSRGBColor(FColor(68,136,157));
    // 远景保留七点，放大时平滑显露其他渔场，不因白名单遗漏可见塘。-codex，2026-11-03
    for(int32 I=0;I<D.Subjects.Num();++I)
    {
        const auto& S=D.Subjects[I];const bool Selected=N.View==TEXT("subject")&&N.Subject==S.Id;
        if(!C->Map->IsSurveyFarmDisplayed(S.Id))continue;
        FVector Anchor;FVector2D Half;if(!C->Map->GetSurveyFarmWaterAnchor(S.Id,Anchor,Half))continue;
        const float Range=float(FVector::Distance(Eye,Anchor)),NearbyAlpha=EffectEase((2600.f-Range)/600.f);
        const float Presence=SurveyRippleIds.Contains(S.Id)||Selected?1.f:NearbyAlpha;
        if(Presence<.001f)continue;
        FVector2D ScreenAnchor;if(!Project(Anchor,ScreenAnchor)||ScreenAnchor.X<0||ScreenAnchor.X>Canvas->ClipX||ScreenAnchor.Y<0||ScreenAnchor.Y>Canvas->ClipY)continue;
        // 只缩普通圈15%，选中沿原14世界单位；颜色与名单绑定同一真实ID，不改变锚点、周期或光柱。-codex，2026-11-03
        const FLinearColor Wave=Selected?Mint:NormalWave;
        const float Outer=Selected?14.f:FMath::Clamp(Range*.022f,7.f,100.f)*.85f,Width=Outer*.055f;
        for(int32 Ring=0;Ring<2;++Ring)
        {
            const float Phase=FMath::Frac(float(Now)/RipplePeriod+I*.137f+Ring*.5f),Radius=Outer*(.18f+.82f*Phase);
            const float Alpha=EffectEase(Phase/.12f)*FMath::Pow(1-Phase,1.25f)*.64f*Presence;
            for(int32 Segment=0;Segment<RippleSegments;++Segment)
            {
                const float A=2*PI*Segment/RippleSegments,B=2*PI*(Segment+1)/RippleSegments;
                const FVector U(FMath::Cos(A),FMath::Sin(A),0),V(FMath::Cos(B),FMath::Sin(B),0);
                for(int32 Band=0;Band<2;++Band)
                {
                    const float Inner=Radius+Width*(Band-1),Outside=Inner+Width;
                    FVector2D P,Q,R,T;if(!Project(Anchor+U*Inner,P)||!Project(Anchor+V*Inner,Q)||!Project(Anchor+V*Outside,R)||!Project(Anchor+U*Outside,T))continue;
                    const auto CI=Wave.CopyWithNewOpacity(Band?Alpha:0),CO=Wave.CopyWithNewOpacity(Band?0:Alpha);
                    Tri(P,Q,R,CI,CI,CO);Tri(P,R,T,CI,CO,CO);
                }
            }
        }
        const FVector& EffectPoint=Anchor;const FVector2D& WaterHalf=Half;
        const float Age=C->SurveyBeaconAge(S.Id);
        if(!Selected||Age<0)continue;
        bool Occluded=false;
        for(const auto& City:C->Map->Cities)if(City.Terrain.IsValid())
        {
            FHitResult Hit;
            if(City.Terrain->LineTraceComponent(Hit,Eye,EffectPoint,FCollisionQueryParams(SCENE_QUERY_STAT(SurveyBeamSight),true))&&Hit.Distance<FVector::Distance(Eye,EffectPoint)-.005){Occluded=true;break;}
        }
        FHitResult LocalHit;
        if(C->Map->TraceSurveyFarmTerrain(LocalHit,Eye,EffectPoint)&&LocalHit.Distance<FVector::Distance(Eye,EffectPoint)-.005)Occluded=true;
        if(Occluded)continue;
        const float Arrival=C->SurveyArrivalAge(S.Id);if(Arrival>=FSurveyCameraFlight::FadeDuration)continue;
        const float Rise=EffectEase(Age/.30f),Height=C->SurveyBeaconHeight(S.Id),Base=C->SurveyBeaconRadius(S.Id);
        if(Height<=0||Base<=0)continue;
        auto Opacity=[&](float Duration){return Arrival<0?1.f:1-EffectEase(Arrival/Duration);};
        const float CoreFade=Opacity(.60f),GlowFade=Opacity(.72f),RingFade=Opacity(FSurveyCameraFlight::FadeDuration);
        const FLinearColor CyanLight(.008f,.58f,.54f),Core(.15f,.85f,.82f);
        DrawingBeam=true;
        const float Heading=FMath::DegreesToRadians(C->SurveyBeaconYaw(S.Id));
        const FVector DepthAxis(FMath::Cos(Heading),FMath::Sin(Heading),0),SideAxis(-FMath::Sin(Heading),FMath::Cos(Heading),0);
        auto At=[&](float Angle,float Radius,float Z){return EffectPoint+SideAxis*(FMath::Cos(Angle)*Radius)+DepthAxis*(FMath::Sin(Angle)*Radius*.34f)+FVector(0,0,Z);};
        // 线段中心及软边一次提交，避免硬白细线沿圈形成锯齿串；世界端点不随镜头改变。-codex，2026-11-02
        auto Stroke=[&](const FVector& A,const FVector& B,float Width,const FLinearColor& Color)
        {
            FVector2D P,Q;if(!ProjectLight(A,P)||!ProjectLight(B,Q))return;
            const FVector2D Direction=Q-P;if(Direction.SizeSquared()<.001)return;
            const FVector2D Normal=FVector2D(-Direction.Y,Direction.X).GetSafeNormal();
            const float CoreWidth=Width*Scale*.5f,Feather=FMath::Max(.8f*Scale,CoreWidth*.8f);
            const float Stops[]={-CoreWidth-Feather,-CoreWidth,CoreWidth,CoreWidth+Feather};
            for(int32 J=0;J<3;++J)
            {
                const FVector2D L=Normal*Stops[J],R=Normal*Stops[J+1];
                const auto CL=Color.CopyWithNewOpacity(J==0?0.f:Color.A),CR=Color.CopyWithNewOpacity(J==2?0.f:Color.A);
                LightTri(P+L,Q+L,Q+R,CL,CL,CR);LightTri(P+L,Q+R,P+R,CL,CR,CR);
            }
        };
        auto Ring=[&](float Radius,float Z,float Alpha,float Width)
        {
            for(int32 J=0;J<192;++J)
            {
                const float A=J*2*PI/192,B=(J+1)*2*PI/192;
                Stroke(At(A,Radius,Z),At(B,Radius,Z),Width+7.f,CyanLight.CopyWithNewOpacity(Alpha*.13f));
                Stroke(At(A,Radius,Z),At(B,Radius,Z),Width,CyanLight.CopyWithNewOpacity(Alpha));
            }
        };
        // 柔光采用固定世界径向薄片，顶端渐弱，边缘透明；不在画面顶部94px生硬裁出矩形。-codex，2026-11-02
        for(int32 Plane=0;Plane<6;++Plane)for(int32 Y=0;Y<32;++Y)for(int32 Band=0;Band<10;++Band)
        {
            const float B=FMath::Square(Y/32.f),T=FMath::Square((Y+1)/32.f);if(B>Rise)continue;const float Top=FMath::Min(T,Rise);
            const float U=-1.f+Band/5.f,V=-1.f+(Band+1)/5.f,Angle=Plane*PI/6;
            const FVector Axis(FMath::Cos(Angle),FMath::Sin(Angle),0);
            const float RB=Base*.20f*EffectEase(B/.10f),RT=Base*.20f*EffectEase(Top/.10f);
            auto Soft=[&](float X,float Z){return CyanLight.CopyWithNewOpacity(.033f*GlowFade*FMath::Pow(1-X*X,3.f)*FMath::Square(1-Z)*EffectEase(Z/.016f));};
            LightQuad(EffectPoint+Axis*(RB*U)+FVector(0,0,B*Height),EffectPoint+Axis*(RB*V)+FVector(0,0,B*Height),EffectPoint+Axis*(RT*V)+FVector(0,0,Top*Height),EffectPoint+Axis*(RT*U)+FVector(0,0,Top*Height),Soft(U,B),Soft(V,B),Soft(V,Top),Soft(U,Top));
        }
        for(int32 Y=0;Y<80;++Y)
        {
            const float B=FMath::Square(Y/80.f),T=FMath::Min(FMath::Square((Y+1)/80.f),Rise);if(B>=T)continue;
            const FVector P=EffectPoint+FVector(0,0,Height*B),Q=EffectPoint+FVector(0,0,Height*T);
            const float Alpha=CoreFade*(.4f+.6f*EffectEase(B/.02f))*(1-EffectEase((B-.70f)/.30f));
            Stroke(P,Q,11.f,CyanLight.CopyWithNewOpacity(Alpha*.15f));Stroke(P,Q,1.5f,Core.CopyWithNewOpacity(Alpha*.75f));
        }
        if(Rise>.40f)Ring(Base*.28f,Height*.40f,RingFade*EffectEase((Age-.10f)/.20f)*.80f,1.35f);
        if(Rise>.70f)Ring(Base*.13f,Height*.70f,RingFade*EffectEase((Age-.16f)/.20f)*.65f,1.1f);
        const float Open=EffectEase(Age/.28f)*RingFade;
        const float Protection=float(WaterHalf.Size())*1.05f;
        const float Radii[]={.20f,.34f,.56f,.78f,1.f},Strengths[]={.75f,.65f,.27f,.12f,.06f};
        for(int32 K=0;K<5;++K)if(Base*Radii[K]>Protection)Ring(Base*Radii[K],.006f,Open*Strengths[K],K<2?1.6f:1.f);
        // 地盘宽环拥有软边与青色面层，中心塘面保持可见。-codex，2026-11-02
        for(int32 K=0;K<2;++K)for(int32 J=0;J<128;++J)for(int32 Band=0;Band<6;++Band)
        {
            const float A=J*2*PI/128,B=(J+1)*2*PI/128,Middle=Base*(K?.56f:.30f),BandWidth=Base*(K?.055f:.065f);
            const float U=-1.f+Band/3.f,V=-1.f+(Band+1)/3.f,Inner=Middle+U*BandWidth,BandOuter=Middle+V*BandWidth;if(Inner<Protection)continue;
            const auto CL=CyanLight.CopyWithNewOpacity(Open*(K?.10f:.24f)*(1-U*U)),CR=CyanLight.CopyWithNewOpacity(Open*(K?.10f:.24f)*(1-V*V));
            LightQuad(At(A,Inner,.004f),At(B,Inner,.004f),At(B,BandOuter,.004f),At(A,BandOuter,.004f),CL,CL,CR,CR);
        }
        for(int32 K=0;K<80;++K)
        {
            const float A=K*2*PI/80,Inner=Base*.62f,TickOuter=Base*(K%5==0?.685f:.645f);if(Inner<Protection)continue;
            Stroke(At(A,Inner,.009f),At(A,TickOuter,.009f),K%5==0?1.1f:.7f,CyanLight.CopyWithNewOpacity(Open*(K%5==0?.55f:.23f)));
        }
        // 粒子分布扩到光柱两侧：快慢两层上升，柔光粒子头与短尾成对，循环首尾零透明防跳闪。-codex，2026-11-02
        const float ParticleFade=Opacity(.65f);
        for(int32 K=0;K<120&&ParticleFade>.001f;++K)
        {
            const float Seed=FMath::Frac(K*.618034f),P=FMath::Frac(Age*(.40f+.30f*FMath::Frac(K*.137f))+Seed);
            const float A=K*2.399963f+.18f*FMath::Sin(Age*1.4f+K),Radius=Base*(.14f+.42f*FMath::Frac(K*.37f));
            const float Z=.03f+.85f*P;if(Z>Rise)continue;
            const FVector Tip=At(A,Radius,Z*Height),Tail=Tip-FVector(0,0,Height*(.004f+.007f*FMath::Frac(K*.43f)));
            const float Alpha=ParticleFade*FMath::Square(FMath::Sin(P*PI))*(.45f+.5f*FMath::Frac(K*.19f));
            Stroke(Tail,Tip,1.f,CyanLight.CopyWithNewOpacity(Alpha*.3f));
            FVector2D Center;if(!ProjectLight(Tip,Center))continue;
            const float PixelRadius=(1.4f+1.6f*FMath::Frac(K*.53f))*Scale;
            for(int32 J=0;J<12;++J)
            {
                const float U=J*2*PI/12,V=(J+1)*2*PI/12;
                const FVector2D P0=Center+FVector2D(FMath::Cos(U),FMath::Sin(U))*PixelRadius,P1=Center+FVector2D(FMath::Cos(V),FMath::Sin(V))*PixelRadius;
                LightTri(Center,P0,P1,CyanLight.CopyWithNewOpacity(Alpha),CyanLight.CopyWithNewOpacity(0),CyanLight.CopyWithNewOpacity(0));
            }
        }
        DrawingBeam=false;
    }
    if(!LightTriangles.IsEmpty()){FCanvasTriangleItem Light(LightTriangles,GWhiteTexture);Light.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Light);}
    if(!BeamTriangles.IsEmpty()){FCanvasTriangleItem Light(BeamTriangles,GWhiteTexture);Light.BlendMode=SE_BLEND_Additive;Canvas->DrawItem(Light);}
}

bool AFujianHUD::SurveyBlocksWheel(FVector2D P)const
{return Hit(HomeButton,P)||Hit(SurveySelector,P)||Hit(SurveyToggle,P)||Hit(SurveyPanel,P)||Hit(SurveyBack,P)||Hit(SurveyOverview,P);}
bool AFujianHUD::SurveyListAction(FVector2D P)const
{return Hit(SurveySelector,P)||Hit(SurveyToggle,P);}
bool AFujianHUD::ScrollSurveyList(float WheelDelta,FVector2D P)
{
    if(!bSurveyListExpanded||!Hit(SurveySelector,P)||Hit(SurveyToggle,P))return false;
    const int32 Before=SurveyFirstRow,Steps=FMath::Max(1,FMath::RoundToInt(FMath::Abs(WheelDelta)));
    if(!FMath::IsNearlyZero(WheelDelta))SurveyFirstRow=FMath::Clamp(SurveyFirstRow+(WheelDelta>0?-Steps:Steps),0,FMath::Max(0,SurveyIds.Num()-SurveyVisibleRows));
    if(Before!=SurveyFirstRow)UE_LOG(LogTemp,Display,TEXT("SurveyList scroll first=%d last=%d count=%d"),SurveyFirstRow+1,FMath::Min(SurveyFirstRow+SurveyVisibleRows,SurveyIds.Num()),SurveyIds.Num());
    return true;
}
bool AFujianHUD::SurveyHit(FVector2D P)const{if(SurveyBlocksWheel(P))return true;for(const auto& B:SurveyPointHits)if(Hit(B,P))return true;return false;}
bool AFujianHUD::ClickSurvey(FVector2D P)
{
    auto* C=Cast<AFujianController>(GetOwningPlayerController());if(!C||!C->SurveyControl)return false;
    if(Hit(HomeButton,P)){C->Home();return true;}
    if(Hit(SurveyToggle,P)){bSurveyListExpanded=!bSurveyListExpanded;return true;}
    if(Hit(SurveyBack,P)){C->CancelCamera(TEXT("survey-back"));C->SurveyControl->Back();return true;}
    if(Hit(SurveyOverview,P)){C->CancelCamera(TEXT("survey-overview"));C->SurveyControl->Overview();return true;}
    int32 Pick=INDEX_NONE;
    for(int32 I=0;I<SurveyRows.Num();++I)if(Hit(SurveyRows[I],P)){Pick=I;break;}
    if(Pick==INDEX_NONE&&!Hit(SurveySelector,P)&&!Hit(SurveyPanel,P))
    {float Closest=TNumericLimits<float>::Max();for(int32 I=0;I<SurveyPointHits.Num();++I)if(Hit(SurveyPointHits[I],P)){const float Dist=FVector2D::DistSquared(P,SurveyPointHits[I].GetCenter());if(Dist<Closest){Closest=Dist;Pick=I;}}}
    if(SurveyIds.IsValidIndex(Pick))
    {
        const FString Id=SurveyIds[Pick];C->CancelCamera(TEXT("survey-select"));C->SurveyControl->SelectSubject(Id);
        if(const FVector* World=SurveyWorldPoints.Find(Id))C->FocusSurveyPoint(Id,*World);
        UE_LOG(LogTemp,Display,TEXT("SurveyClick id=%s x=%.1f y=%.1f"),*Id,P.X,P.Y);return true;
    }
    return Hit(SurveySelector,P)||Hit(SurveyPanel,P);
}
