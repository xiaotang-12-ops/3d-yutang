// A屏边缘界面：复用地图选择与有限镜头状态，不保存另一套业务状态。
// 图稿v4仅作为样式基线，地理、塘名和面积始终来自Map。-codex，2026-10-21
#include "FujianMap.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

namespace
{
    // 等比逻辑画布在非16:9窗口内居中；绘图与点击框共享同一坐标换算。-codex，2026-10-21
    struct FPondHUDLayout
    {
        float Scale;
        FVector2D Offset;
        FPondHUDLayout(float Width,float Height)
        {
            Scale=FMath::Min(Width/1920.f,Height/1080.f);
            Offset=FVector2D((Width-1920.f*Scale)*.5f,(Height-1080.f*Scale)*.5f);
        }
        explicit FPondHUDLayout(const UCanvas* Canvas):FPondHUDLayout(Canvas->ClipX,Canvas->ClipY) {}
        FVector2D Point(float X,float Y) const { return Offset+FVector2D(X,Y)*Scale; }
        FBox2D Box(float X,float Y,float W,float H) const { return FBox2D(Point(X,Y),Point(X+W,Y+H)); }
    };
    bool PondHit(const FBox2D& Box,const FVector2D& Point) { return Box.bIsValid&&Box.IsInsideOrOn(Point); }
    // 与下方不透明底块相同的逻辑坐标，底卡/操作块不能穿透拾取地形。-codex，2026-10-22
    bool PondBottomHit(const FVector2D& Q,bool bSite)
    {
        return Q.Y>=1030||(Q.X>=36&&Q.X<=(bSite?576:446)&&Q.Y>=838&&Q.Y<=1016)
            ||(Q.X>=1230&&Q.X<=1902&&Q.Y>=874&&Q.Y<=1028);
    }
    bool PondSiteProjection(AFujianController* C,const FPondHUDLayout& L,const FDemoSite& Site,FVector2D& Q)
    {
        FVector2D P;
        // HUD圈、引线与实体球共用中心；抬高40曾造成“一塘两点”的悬浮错位。-codex，2026-10-22
        if(!C->ProjectWorldLocationToScreen(Site.Position,P)||!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y))return false;
        Q=(P-L.Offset)/L.Scale;
        // 网格与投影共存时，投影不能穿进网格，也不能生成屏外命中框。-codex，2026-10-21
        return Q.X>64&&Q.X<1610&&Q.Y>192&&Q.Y<790;
    }
}

void AFujianHUD::DrawShowcase()
{
    // 每帧先清空，失败投影及切层都不能留下上一帧的幽灵点击区域。-codex，2026-10-21
    CityButtons.Empty(); SiteButtons.Empty(); SiteMarkerButtons.Empty(); QuickSiteButtons.Empty();
    ProvinceButton=FBox2D(ForceInit);
    HomeButton=ResetButton=TopButton=DataButton=FBox2D(ForceInit);
    auto* C=Cast<AFujianController>(GetOwningPlayerController());
    if(!Canvas||!GEngine||!C||!C->Map||Canvas->ClipX<=0||Canvas->ClipY<=0)return;
    auto* Map=C->Map.Get();
    // 旧字体仅保留省级城市框测宽，避免本批文字清晰度调整改变原布局与命中框。-codex，2026-10-22
    UFont* Font=GEngine->GetSmallFont();
    if(!Font||!FSlateApplication::IsInitialized())return;
    const FPondHUDLayout L(Canvas);
    static const auto Chinese=MakeShared<FCompositeFont>(NAME_None,FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const FSlateFontInfo Probe(Chinese,20.f);
    const float ProbeHeight=FMath::Max(static_cast<float>(Measure->Measure(FStringView(TEXT("国Ag")),Probe).Y),1.f);
    const float CanvasDPI=Canvas->GetDPIScale();
    const FLinearColor Cyan(.18f,.82f,.9f),Muted(.42f,.63f,.71f),White(.83f,.94f,.97f),Gold(1.f,.69f,.23f);
    const FLinearColor Panel(.003f,.018f,.034f,.92f);
    const bool bCity=Map->Cities.IsValidIndex(Map->SelectedCity);
    const FDemoCity* City=bCity?&Map->Cities[Map->SelectedCity]:nullptr;
    const FDemoSite* Site=City&&City->Sites.IsValidIndex(Map->SelectedSite)?&City->Sites[Map->SelectedSite]:nullptr;
    float MouseX=-10000.f,MouseY=-10000.f;
    C->GetMousePosition(MouseX,MouseY);
    const FVector2D Mouse(MouseX,MouseY);
    auto Rect=[&](float X,float Y,float W,float H,FLinearColor Color)
    {
        const FVector2D P=L.Point(X,Y); DrawRect(Color,P.X,P.Y,W*L.Scale,H*L.Scale);
    };
    auto OpaqueRect=[&](float X,float Y,float W,float H,FLinearColor Color)
    {
        // 普通DrawRect固定使用Translucent；底部操作文字需真正不透明，不能只增大Panel的Alpha。-codex，2026-10-22
        Color.A=1.f;
        FCanvasTileItem Tile(L.Point(X,Y),FVector2D(W,H)*L.Scale,Color);
        Tile.BlendMode=SE_BLEND_Opaque;Canvas->DrawItem(Tile);
    };
    auto Line=[&](float X,float Y,float X2,float Y2,FLinearColor Color,float Width=1.f)
    {
        const FVector2D A=L.Point(X,Y),B=L.Point(X2,Y2); DrawLine(A.X,A.Y,B.X,B.Y,Color,Width*L.Scale);
    };
    auto Text=[&](const FString& Value,float X,float Y,float Size,FLinearColor Color,float MaxWidth=1800.f)
    {
        // Slate字号是点数：先以实际字高换算原逻辑像素，再用同一Canvas DPI测宽；不放大小字形贴图。-codex，2026-10-22
        FSlateFontInfo Info(Chinese,FMath::Max(1.f,20.f*Size*20.f*L.Scale/ProbeHeight));
        const float Limit=FMath::Max(1.f,MaxWidth*L.Scale*CanvasDPI);
        float Width=Measure->Measure(Value,Info,CanvasDPI).X;
        if(Width>Limit)
        {
            Info.Size=FMath::Max(1.f,Info.Size*Limit/Width);
            // hinting会量化字宽，重测后只收小字号，避免适配后仍越过原文本边界。-codex，2026-10-22
            while(Info.Size>1.f&&Measure->Measure(Value,Info,CanvasDPI).X>Limit)
                Info.Size=FMath::Max(1.f,Info.Size-.25f);
        }
        // Canvas仍要求有效UFont载体；字形/字号由SlateFontInfo覆盖，避免复合字体构造时Font为空不绘制。-codex，2026-10-22
        FCanvasTextItem Item(L.Point(X,Y),FText::FromString(Value),Font,Color);
        Item.SlateFontInfo=Info;
        Item.Scale=FVector2D(1.f,1.f);Canvas->DrawItem(Item);
    };
    auto Frame=[&](float X,float Y,float W,float H,FLinearColor Color)
    {
        Rect(X,Y,W,H,Panel); Line(X,Y,X+W,Y,Color); Line(X,Y+H,X+W,Y+H,Color);
        Line(X,Y,X,Y+H,Color); Line(X+W,Y,X+W,Y+H,Color);
    };
    auto Button=[&](const FString& Label,float X,float Y,float W,bool Active=false)
    {
        const FBox2D Box=L.Box(X,Y,W,48);
        const bool Hover=PondHit(Box,Mouse);
        Frame(X,Y,W,48,Hover||Active?Cyan:FLinearColor(.10f,.35f,.44f));
        if(Hover)Rect(X+1,Y+1,W-2,46,FLinearColor(.03f,.24f,.31f,.72f));
        Text(Label,X+16,Y+13,1.05f,Hover||Active?White:Muted,W-30);
        return Box;
    };
    auto Project=[&](const FVector& World,FVector2D& P,float HalfW,float HalfH)
    {
        if(!C->ProjectWorldLocationToScreen(World,P))return false;
        if(!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y))return false;
        const FVector2D Local=(P-L.Offset)/L.Scale;
        // 留出标题、路径及底部小卡/操作区；不把屏外投影夹到屏边伪装真实位置。-codex，2026-10-21
        return Local.X>30+HalfW&&Local.X<1890-HalfW&&Local.Y>166+HalfH&&Local.Y<810-HalfH;
    };

    CityButtons.Init(FBox2D(ForceInit),Map->Cities.Num());
    if(!bCity)
    {
        for(int32 I=0;I<Map->Cities.Num();++I)
        {
            const FDemoCity& Item=Map->Cities[I];
            float BW=0,BH=0; GetTextSize(TEXT("国Ag"),BW,BH,Font,1.f);
            float TW=0,TH=0; GetTextSize(Item.Name,TW,TH,Font,1.12f*20.f/FMath::Max(BH,1.f));
            const float Width=FMath::Clamp(TW+36.f,94.f,180.f);
            FVector2D P;
            if(!Project(Item.Center+FVector(0,0,70),P,Width*.5f,24.f))continue;
            const FVector2D Q=(P-L.Offset)/L.Scale;
            CityButtons[I]=L.Box(Q.X-Width*.5f,Q.Y-24,Width,48);
            const bool Hover=PondHit(CityButtons[I],Mouse);
            Frame(Q.X-Width*.5f,Q.Y-24,Width,48,Hover?White:FLinearColor(.09f,.48f,.59f));
            Text(Item.Name,Q.X-Width*.5f+16,Q.Y-10,1.12f,Hover?White:Cyan,Width-28);
        }
    }
    else
    {
        SiteButtons.Init(FBox2D(ForceInit),City->Sites.Num());
        SiteMarkerButtons.Init(FBox2D(ForceInit),City->Sites.Num());
        QuickSiteButtons.Init(FBox2D(ForceInit),City->Sites.Num());
        TArray<FVector2D> Anchors;Anchors.Init(FVector2D::ZeroVector,City->Sites.Num());
        TArray<bool> Projected;Projected.Init(false,City->Sites.Num());
        for(int32 I=0;I<City->Sites.Num();++I)
        {
            Projected[I]=PondSiteProjection(C,L,City->Sites[I],Anchors[I]);
            if(Projected[I])SiteMarkerButtons[I]=L.Box(Anchors[I].X-24,Anchors[I].Y-24,48,48);
        }
        TArray<FBox2D> Occupied;
        // 先排选中塘，再排其他点；标签允许有限偏移，但引线永远指向原始投影。
        // 不能排下的点仍保留定位圆点并经右侧入口可达，不把标签夹到屏边冒充位置。-codex，2026-10-22
        TArray<int32> Order;
        if(City->Sites.IsValidIndex(Map->SelectedSite))Order.Add(Map->SelectedSite);
        for(int32 I=0;I<City->Sites.Num();++I)if(I!=Map->SelectedSite)Order.Add(I);
        for(const int32 I:Order)
        {
            if(!Projected[I])continue;
            const FVector2D Q=Anchors[I];
            const bool Selected=I==Map->SelectedSite;
            const float LabelWidth=Selected?222.f:48.f;
            FBox2D Label(ForceInit);
            const FVector2D Candidates[]={FVector2D(16,-54),FVector2D(16,12),FVector2D(-LabelWidth-16,-54),FVector2D(-LabelWidth-16,12),FVector2D(-LabelWidth*.5f,-78),FVector2D(-LabelWidth*.5f,34)};
            for(const FVector2D& Offset:Candidates)
            {
                const FBox2D Candidate(Q+Offset,Q+Offset+FVector2D(LabelWidth,44));
                if(Candidate.Min.X<54||Candidate.Max.X>1624||Candidate.Min.Y<184||Candidate.Max.Y>802)continue;
                bool Overlap=false;
                for(const FBox2D& Existing:Occupied)if(Candidate.ExpandBy(7.f).Intersect(Existing)){Overlap=true;break;}
                // 只避让标签仍会盖住别塘实体点；所有可见点的24×24核心都保留，不改点的位置。-codex，2026-10-22
                if(!Overlap)for(int32 J=0;J<Anchors.Num();++J)
                {
                    const float ProtectedRadius=J==Map->SelectedSite?28.f:12.f;
                    const FVector2D Margin(ProtectedRadius,ProtectedRadius);
                    if(J!=I&&Projected[J]&&Candidate.Intersect(FBox2D(Anchors[J]-Margin,Anchors[J]+Margin))){Overlap=true;break;}
                }
                if(!Overlap){Label=Candidate;Occupied.Add(Candidate);break;}
            }
            if(Label.bIsValid)SiteButtons[I]=L.Box(Label.Min.X,Label.Min.Y,LabelWidth,44);
            const bool Hover=PondHit(SiteButtons[I],Mouse)||PondHit(SiteMarkerButtons[I],Mouse);
            const FLinearColor Color=Selected?Gold:(Hover?White:Cyan);
            const float Radius=Selected?15.f:6.f;
            for(int32 Segment=0;Segment<32;++Segment)
            {
                const float A=2.f*PI*Segment/32.f,B=2.f*PI*(Segment+1)/32.f;
                Line(Q.X+Radius*FMath::Cos(A),Q.Y+Radius*FMath::Sin(A),Q.X+Radius*FMath::Cos(B),Q.Y+Radius*FMath::Sin(B),Color,Selected?2.f:1.5f);
                if(Selected)Line(Q.X+26.f*FMath::Cos(A),Q.Y+13.f*FMath::Sin(A),Q.X+26.f*FMath::Cos(B),Q.Y+13.f*FMath::Sin(B),FLinearColor(.7f,.40f,.1f,.75f),1.f);
            }
            if(!Label.bIsValid)continue;
            const FVector2D Join(FMath::Clamp(Q.X,Label.Min.X,Label.Max.X),FMath::Clamp(Q.Y,Label.Min.Y,Label.Max.Y));
            Line(Q.X,Q.Y,Join.X,Join.Y,Selected?Gold:FLinearColor(.12f,.52f,.60f,.55f),Selected?1.5f:.7f);
            if(Selected)Frame(Label.Min.X,Label.Min.Y,LabelWidth,44,Gold);
            else
            {
                // 触摸框仍为48×44，视觉只留36×28低对比底片；减轻24个普通点同时盖住山体的块感。-codex，2026-10-22
                Rect(Label.Min.X+6,Label.Min.Y+8,36,28,Hover?Panel:FLinearColor(.003f,.018f,.034f,.5f));
                if(Hover)Line(Label.Min.X+6,Label.Min.Y+36,Label.Min.X+42,Label.Min.Y+36,Cyan);
            }
            Text(Selected?City->Sites[I].Name:FString::Printf(TEXT("%02d"),I+1),Label.Min.X+10,Label.Min.Y+10,Selected?1.05f:1.f,Color,LabelWidth-18);
        }
        // 只呈现同一真实数组的快捷索引；被山体遮住或被镜头裁切的塘仍能直接选择。
        // 当前演示每市最多24塘，不增设独立选择状态。-codex，2026-10-21
        Frame(1656,184,228,448,FLinearColor(.07f,.36f,.46f));
        Text(TEXT("市域塘位"),1670,201,1.05f,White,200);
        Text(FString::Printf(TEXT("%d 个演示点 · 点击定位"),City->Sites.Num()),1670,233,.78f,Muted,200);
        int32 HoverSite=INDEX_NONE;
        for(int32 I=0;I<FMath::Min(24,City->Sites.Num());++I)
        {
            const float X=1670.f+(I%4)*52.f,Y=271.f+(I/4)*52.f;
            QuickSiteButtons[I]=L.Box(X,Y,48,48);
            const bool Hover=PondHit(QuickSiteButtons[I],Mouse),Selected=I==Map->SelectedSite;
            if(Hover)HoverSite=I;
            const FLinearColor Color=Selected?Gold:(Hover?White:Cyan);
            Rect(X,Y,48,48,Selected?FLinearColor(.22f,.12f,.025f,.92f):FLinearColor(.007f,.043f,.064f,.94f));
            Line(X,Y+48,X+48,Y+48,Selected?Gold:FLinearColor(.04f,.23f,.30f),Selected?2.f:1.f);
            if(Hover)Rect(X,Y,48,48,FLinearColor(.03f,.24f,.31f,.65f));
            Text(FString::Printf(TEXT("%02d"),I+1),X+12,Y+14,1.05f,Color,30);
        }
        const int32 CaptionIndex=City->Sites.IsValidIndex(HoverSite)?HoverSite:Map->SelectedSite;
        Text(City->Sites.IsValidIndex(CaptionIndex)?City->Sites[CaptionIndex].Name:TEXT("点位与面积为演示数据"),1670,603,.78f,Muted,200);
    }

    Rect(0,0,1920,104,FLinearColor(.003f,.016f,.035f,.88f));
    Line(36,88,620,88,FLinearColor(.06f,.40f,.52f));
    Line(1300,88,1884,88,FLinearColor(.06f,.40f,.52f));
    Text(TEXT("福建 · 山海养殖"),750,21,1.8f,White,520);
    Text(TEXT("FUJIAN AQUACULTURE  /  三维交互屏 A"),750,64,.86f,Cyan,650);
    Text(TEXT("模拟演示"),1744,29,1.f,Cyan,130);
    ProvinceButton=L.Box(38,112,132,44);
    const bool HomeHover=PondHit(ProvinceButton,Mouse);
    Rect(38,112,132,44,HomeHover?FLinearColor(.025f,.16f,.22f,.9f):Panel);
    Line(38,156,170,156,FLinearColor(.06f,.40f,.52f));
    Text(TEXT("全省首页"),53,124,1.02f,HomeHover?White:Cyan,110);
    FString Path=City?City->Name:TEXT("福建省 · 省域总览");
    if(Site)Path+=TEXT("  /  ")+Site->Name;
    Text(Path,190,124,1.05f,White,1240);
    const FString Phase(C->CameraPhase());
    const FString PhaseText=Phase==TEXT("approach")?TEXT("镜头 · 定位中"):(Phase==TEXT("orbit")?TEXT("镜头 · 短幅环绕"):TEXT("镜头 · 已停稳"));
    Text(PhaseText,1640,120,.95f,Muted,245);

    const float CardWidth=Site?540.f:410.f;
    // 保留既有卡片和按钮位置；独立实底遮住其后岸线/世界点，不把整条底边都盖成面板。-codex，2026-10-22
    OpaqueRect(36,838,CardWidth,178,Panel);
    OpaqueRect(1230,874,672,154,Panel);
    OpaqueRect(0,1030,1920,50,FLinearColor(.003f,.016f,.035f,1.f));
    Frame(36,838,CardWidth,178,FLinearColor(.07f,.40f,.49f));
    Line(36,838,134,838,Site?Gold:Cyan,2.f);
    Text(Site?Site->Name:(City?City->Name:TEXT("福建省 · 三维地形")),58,857,1.25f,White,Site?340.f:365.f);
    Text(Site?Site->Id:(City?FString::Printf(TEXT("%d  /  %d 个演示塘"),City->Code,City->Sites.Num()):TEXT("省 → 市 → 养殖塘")),58,895,1.04f,Site?Gold:Cyan,330);
    Line(58,928,Site?374.f:422.f,928,FLinearColor(.08f,.28f,.36f));
    Text(Site?FString::Printf(TEXT("%s  ·  %d 亩"),*Site->Kind,Site->AreaMu):(City?TEXT("点击塘位查看同对象资料"):TEXT("点击城市进入市域")),58,947,.98f,Muted,330);
    Text(TEXT("点位与面积为演示数据"),58,985,.72f,Muted,330);
    if(Site)
    {
        // 只有演示中心点，没有真实塘界。平面卡使用固定水面符号并就近标明示意，不能按亩数推造边界。-codex，2026-10-22
        Rect(402,875,146,84,FLinearColor(.009f,.10f,.14f,.94f));
        Line(402,875,548,875,Gold,1.5f);Line(402,875,402,959,Gold,1.5f);
        Line(548,875,548,959,Gold,1.5f);Line(402,959,548,959,Gold,1.5f);
        for(int32 I=0;I<3;I++)Line(419,900+I*15,531,900+I*15,FLinearColor(.04f,.30f,.36f),1.f);
        Text(FString::Printf(TEXT("%02d"),Map->SelectedSite+1),459,906,1.1f,Gold,38);
        Text(TEXT("通用平面示意"),406,973,.72f,Muted,146);
        Text(TEXT("非真实塘界"),419,994,.65f,Muted,132);
    }

    const FString BackLabel=Site?TEXT("返回市域"):(City?TEXT("返回全省"):TEXT("全省总览"));
    HomeButton=Button(BackLabel,1248,930,154);
    ResetButton=Button(TEXT("视角复位"),1414,930,154);
    TopButton=Button(TEXT("俯视"),1580,930,120);
    DataButton=Button(TEXT("数据屏"),1712,930,172,true);
    Text(C->DataWindowStatus(),1248,895,.82f,Muted,636);
    Text(TEXT("拖动旋转  /  滚轮缩放  /  ESC 分级返回"),1248,994,.85f,Muted,636);
    Text(TEXT("公开开发版 · 示意地形，使用自备数据时请核对来源"),36,1042,.68f,Muted,1180);
    if(!Map->Error.IsEmpty())Text(Map->Error,500,151,.76f,FLinearColor(1.f,.5f,.35f),1340);
}

bool AFujianHUD::ClickShowcase(FVector2D Position)
{
    auto* C=Cast<AFujianController>(GetOwningPlayerController());
    if(!C||!C->Map)return false;
    // Canvas只在PostRender内有效，引擎在每帧绘制后会清空它；输入必须使用视口尺寸。
    // 按钮保持最近一帧的像素框，投影与空白区域则复用同一布局换算。-codex，2026-10-21
    int32 Width=0,Height=0;
    C->GetViewportSize(Width,Height);
    if(Width<=0||Height<=0)return false;
    const FPondHUDLayout L(static_cast<float>(Width),static_cast<float>(Height));
    if(PondHit(ProvinceButton,Position)){C->Home();return true;}
    if(PondHit(HomeButton,Position)){C->Back();return true;}
    if(PondHit(ResetButton,Position)){C->FitMap(false);return true;}
    if(PondHit(TopButton,Position)){C->FitMap(true);return true;}
    if(PondHit(DataButton,Position)){C->OpenData();return true;}
    auto* Map=C->Map.Get();
    const FVector2D Q=(Position-L.Offset)/L.Scale;
    if(PondBottomHit(Q,Map->SelectedSite!=INDEX_NONE))return true;
    const bool bCity=Map->Cities.IsValidIndex(Map->SelectedCity);
    if(!bCity)
    {
        for(int32 I=CityButtons.Num()-1;I>=0;--I)
            if(Map->Cities.IsValidIndex(I)&&PondHit(CityButtons[I],Position)){Map->ShowCity(I);return true;}
    }
    else
    {
        // 与绘制顺序相反命中，重叠塘位优先响应最上层标签。镜头由控制器检测选择后驱动。-codex，2026-10-21
        const auto& Sites=Map->Cities[Map->SelectedCity].Sites;
        for(int32 I=QuickSiteButtons.Num()-1;I>=0;--I)
            if(Sites.IsValidIndex(I)&&PondHit(QuickSiteButtons[I],Position)){Map->SelectSite(I);return true;}
        if(PondHit(L.Box(1656,184,228,448),Position))return true;
        // 选中标签最先排版，不与其余标签重叠；命中只使用同帧实际绘制框。-codex，2026-10-22
        for(int32 I=SiteButtons.Num()-1;I>=0;--I)
            if(Sites.IsValidIndex(I)&&PondHit(SiteButtons[I],Position)){Map->SelectSite(I);return true;}
        // 隐藏的是放不下的编号，不是实际可选点；同帧点框重叠时选离触点最近的中心。-codex，2026-10-22
        int32 Closest=INDEX_NONE;double Best=TNumericLimits<double>::Max();
        for(int32 I=0;I<SiteMarkerButtons.Num();++I)
            if(Sites.IsValidIndex(I)&&PondHit(SiteMarkerButtons[I],Position))
            {
                const double Distance=(Position-SiteMarkerButtons[I].GetCenter()).SizeSquared();
                if(Distance<Best){Best=Distance;Closest=I;}
            }
        if(Closest!=INDEX_NONE){Map->SelectSite(Closest);return true;}
    }
    // 边缘信息块消费点击，避免从小卡或标题穿透拾取后面的地形。-codex，2026-10-21
    return Q.Y<166;
}
