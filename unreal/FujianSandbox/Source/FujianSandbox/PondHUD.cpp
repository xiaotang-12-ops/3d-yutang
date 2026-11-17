// A屏边缘界面：复用地图选择与有限镜头状态，不保存另一套业务状态。
// 图稿v4仅作为样式基线，地理、塘名和面积始终来自Map。-codex，2026-10-21
#include "FujianMap.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

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
    bool PondSiteProjection(AFujianController* C,const FPondHUDLayout& L,const FDemoSite& Site,FVector2D& Q)
    {
        FVector2D P;
        if(!C->ProjectWorldLocationToScreen(Site.Position+FVector(0,0,40),P)||!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y))return false;
        Q=(P-L.Offset)/L.Scale;
        // 网格与投影共存时，投影不能穿进网格，也不能生成屏外命中框。-codex，2026-10-21
        return Q.X>80&&Q.X<1840&&Q.Y>193&&Q.Y<783&&!(Q.X>1606&&Q.Y<511);
    }
}

void AFujianHUD::DrawShowcase()
{
    // 每帧先清空，失败投影及切层都不能留下上一帧的幽灵点击区域。-codex，2026-10-21
    CityButtons.Empty(); SiteButtons.Empty();
    HomeButton=ResetButton=TopButton=DataButton=FBox2D(ForceInit);
    auto* C=Cast<AFujianController>(GetOwningPlayerController());
    if(!Canvas||!GEngine||!C||!C->Map||Canvas->ClipX<=0||Canvas->ClipY<=0)return;
    auto* Map=C->Map.Get();
    UFont* Font=GEngine->GetSmallFont();
    if(!Font)return;
    const FPondHUDLayout L(Canvas);
    const FLinearColor Cyan(.18f,.82f,.9f),Muted(.42f,.63f,.71f),White(.83f,.94f,.97f),Gold(1.f,.69f,.23f);
    const FLinearColor Panel(.005f,.025f,.047f,.88f);
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
    auto Line=[&](float X,float Y,float X2,float Y2,FLinearColor Color,float Width=1.f)
    {
        const FVector2D A=L.Point(X,Y),B=L.Point(X2,Y2); DrawLine(A.X,A.Y,B.X,B.Y,Color,Width*L.Scale);
    };
    auto Text=[&](const FString& Value,float X,float Y,float Size,FLinearColor Color,float MaxWidth=1800.f)
    {
        // 以实际字体高度换算目标逻辑像素，避免SmallFont的原生字高让窗口内文字过小。-codex，2026-10-21
        float BaseWidth=0,BaseHeight=0; GetTextSize(TEXT("国Ag"),BaseWidth,BaseHeight,Font,1.f);
        const float FontScale=Size*20.f/FMath::Max(BaseHeight,1.f);
        float TW=0,TH=0; GetTextSize(Value,TW,TH,Font,FontScale);
        const float Fit=TW>MaxWidth?MaxWidth/TW:1.f;
        const FVector2D P=L.Point(X,Y); DrawText(Value,Color,P.X,P.Y,Font,FontScale*Fit*L.Scale);
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
            if(!Project(Item.Center+FVector(0,0,70),P,Width*.5f,19.f))continue;
            const FVector2D Q=(P-L.Offset)/L.Scale;
            CityButtons[I]=L.Box(Q.X-Width*.5f,Q.Y-19,Width,38);
            const bool Hover=PondHit(CityButtons[I],Mouse);
            Frame(Q.X-Width*.5f,Q.Y-19,Width,38,Hover?White:FLinearColor(.09f,.48f,.59f));
            Text(Item.Name,Q.X-Width*.5f+16,Q.Y-10,1.12f,Hover?White:Cyan,Width-28);
        }
    }
    else
    {
        SiteButtons.Init(FBox2D(ForceInit),City->Sites.Num());
        for(int32 I=0;I<City->Sites.Num();++I)
        {
            FVector2D Q;
            if(!PondSiteProjection(C,L,City->Sites[I],Q))continue;
            const bool Selected=I==Map->SelectedSite;
            const bool Hover=PondHit(L.Box(Q.X-20,Q.Y-22,70,44),Mouse);
            const FLinearColor Color=Selected?Gold:(Hover?White:Cyan);
            const float Radius=Selected?11.f:6.f;
            for(int32 Segment=0;Segment<20;++Segment)
            {
                const float A=2.f*PI*Segment/20.f,B=2.f*PI*(Segment+1)/20.f;
                Line(Q.X+Radius*FMath::Cos(A),Q.Y+Radius*FMath::Sin(A),Q.X+Radius*FMath::Cos(B),Q.Y+Radius*FMath::Sin(B),Color,Selected?2.f:1.5f);
            }
            Rect(Q.X-2,Q.Y-2,4,4,Color);
            Text(FString::Printf(TEXT("%02d"),I+1),Q.X+13,Q.Y-11,Selected?1.12f:.9f,Color,35);
        }
        // 只呈现同一真实数组的快捷索引；被山体遮住或被镜头裁切的塘仍能直接选择。
        // 当前演示每市最多24塘，不增设独立选择状态。-codex，2026-10-21
        Frame(1656,184,228,300,FLinearColor(.07f,.36f,.46f));
        Text(TEXT("市域塘位 · 快速选择"),1670,199,.88f,Cyan,200);
        int32 HoverSite=INDEX_NONE;
        for(int32 I=0;I<FMath::Min(24,City->Sites.Num());++I)
        {
            const float X=1670.f+(I%4)*52.f,Y=236.f+(I/4)*36.f;
            SiteButtons[I]=L.Box(X,Y,44,28);
            const bool Hover=PondHit(SiteButtons[I],Mouse),Selected=I==Map->SelectedSite;
            if(Hover)HoverSite=I;
            const FLinearColor Color=Selected?Gold:(Hover?White:Cyan);
            Frame(X,Y,44,28,Color);
            if(Hover||Selected)Rect(X+1,Y+1,42,26,Selected?FLinearColor(.34f,.20f,.035f,.8f):FLinearColor(.025f,.23f,.30f,.85f));
            Text(FString::Printf(TEXT("%02d"),I+1),X+8,Y+4,.95f,Color,28);
        }
        const int32 CaptionIndex=City->Sites.IsValidIndex(HoverSite)?HoverSite:Map->SelectedSite;
        Text(City->Sites.IsValidIndex(CaptionIndex)?City->Sites[CaptionIndex].Name:TEXT("悬浮查看塘名 · 点击定位"),1670,461,.78f,Muted,200);
    }

    Rect(0,0,1920,104,FLinearColor(.003f,.016f,.035f,.88f));
    Line(36,88,620,88,FLinearColor(.06f,.40f,.52f));
    Line(1300,88,1884,88,FLinearColor(.06f,.40f,.52f));
    Text(TEXT("福建 · 山海养殖"),750,21,1.8f,White,520);
    Text(TEXT("FUJIAN AQUACULTURE  /  三维交互屏 A"),750,64,.86f,Cyan,650);
    Text(TEXT("模拟演示"),1744,29,1.f,Cyan,130);
    FString Path=TEXT("福建省");
    if(City)Path+=TEXT("  /  ")+City->Name;
    if(Site)Path+=TEXT("  /  ")+Site->Name;
    Text(Path,38,120,1.05f,White,1320);
    const FString Phase(C->CameraPhase());
    const FString PhaseText=Phase==TEXT("approach")?TEXT("镜头 · 定位中"):(Phase==TEXT("orbit")?TEXT("镜头 · 短幅环绕"):TEXT("镜头 · 已停稳"));
    Text(PhaseText,1640,120,.95f,Muted,245);

    Frame(36,838,410,168,FLinearColor(.07f,.48f,.59f));
    Line(36,838,134,838,Site?Gold:Cyan,2.f);
    Text(Site?Site->Name:(City?City->Name:TEXT("福建省 · 三维地形")),58,858,1.25f,White,365);
    Text(Site?Site->Id:(City?FString::Printf(TEXT("%d  /  %d 个演示塘"),City->Code,City->Sites.Num()):TEXT("省 → 市 → 养殖塘")),58,898,1.04f,Site?Gold:Cyan,365);
    Line(58,932,422,932,FLinearColor(.08f,.28f,.36f));
    Text(Site?FString::Printf(TEXT("%s  ·  %d 亩"),*Site->Kind,Site->AreaMu):(City?TEXT("点击塘位查看同对象资料"):TEXT("点击城市进入市域")),58,951,.98f,Muted,365);
    Text(TEXT("点位与面积为演示数据"),58,981,.72f,Muted,365);

    const FString BackLabel=Site?TEXT("返回市域"):(City?TEXT("返回全省"):TEXT("全省总览"));
    HomeButton=Button(BackLabel,1248,930,154);
    ResetButton=Button(TEXT("视角复位"),1414,930,154);
    TopButton=Button(TEXT("俯视"),1580,930,120);
    DataButton=Button(TEXT("数据屏"),1712,930,172,true);
    Text(C->DataWindowStatus(),1248,895,.82f,Muted,636);
    Text(TEXT("拖动旋转  /  滚轮缩放  /  ESC 分级返回"),1248,994,.85f,Muted,636);
    Text(TEXT("公开开发版 · 示意地形，使用自备数据时请核对来源"),36,1042,.73f,Muted,1180);
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
    if(PondHit(HomeButton,Position)){C->Back();return true;}
    if(PondHit(ResetButton,Position)){C->FitMap(false);return true;}
    if(PondHit(TopButton,Position)){C->FitMap(true);return true;}
    if(PondHit(DataButton,Position)){C->OpenData();return true;}
    auto* Map=C->Map.Get();
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
        for(int32 I=SiteButtons.Num()-1;I>=0;--I)
            if(Sites.IsValidIndex(I)&&PondHit(SiteButtons[I],Position)){Map->SelectSite(I);return true;}
        if(PondHit(L.Box(1656,184,228,300),Position))return true;
        for(int32 I=Sites.Num()-1;I>=0;--I)
        {
            FVector2D Q;
            if(PondSiteProjection(C,L,Sites[I],Q)&&PondHit(L.Box(Q.X-20,Q.Y-22,70,44),Position))
            {Map->SelectSite(I);return true;}
        }
    }
    const FVector2D Q=(Position-L.Offset)/L.Scale;
    // 边缘信息块消费点击，避免从小卡或标题穿透拾取后面的地形。-codex，2026-10-21
    return Q.Y<166||(Q.Y>838&&Q.X<446)||Q.Y>1030||(Q.X>1230&&Q.Y>880);
}
